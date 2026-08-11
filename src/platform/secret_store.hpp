#pragma once

#include <cstdlib>
#include <string>

#include <grp.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libsecret/secret.h>

// Stores the VPN password in the desktop keychain (Secret Service API).
//
// A root process cannot reach the user's keychain: D-Bus authenticates its
// peer by uid, and a user's session bus rejects uid 0 outright, whatever the
// socket permissions say. So every operation runs in a forked child that has
// dropped to the invoking user's uid, and reports back over a pipe.
class SecretStore
{
public:
    static bool Available()
    {
        return TargetUid() != static_cast<uid_t>(-1) && !BusAddress().empty();
    }

    static std::string Describe()
    {
        if (TargetUid() == static_cast<uid_t>(-1))
        {
            return "no invoking user (SUDO_USER/PKEXEC_UID unset)";
        }
        if (BusAddress().empty())
        {
            return "no session bus for uid " + std::to_string(TargetUid());
        }
        return "keychain available for uid " + std::to_string(TargetUid());
    }

    // Empty when absent or unavailable.
    static std::string Lookup(const std::string &account)
    {
        return RunAsUser(
            [&account](std::string &out)
            {
                GError *error = nullptr;
                gchar *value =
                    secret_password_lookup_sync(Schema(), nullptr, &error, "service", ServiceName,
                                                "account", account.c_str(), nullptr);

                if (error != nullptr)
                {
                    g_error_free(error);
                    return false;
                }
                if (value == nullptr)
                {
                    return false;
                }

                out.assign(value);
                secret_password_free(value);
                return true;
            });
    }

    static bool Store(const std::string &account, const std::string &password)
    {
        const std::string label = "Clavister OneConnect (" + account + ")";

        return !RunAsUser(
                    [&](std::string &out)
                    {
                        GError *error = nullptr;
                        const gboolean ok = secret_password_store_sync(
                            Schema(), SECRET_COLLECTION_DEFAULT, label.c_str(), password.c_str(),
                            nullptr, &error, "service", ServiceName, "account", account.c_str(),
                            nullptr);

                        if (error != nullptr)
                        {
                            g_error_free(error);
                            return false;
                        }

                        out = ok != FALSE ? "1" : "";
                        return ok != FALSE;
                    })
                    .empty();
    }

    static void Clear(const std::string &account)
    {
        RunAsUser(
            [&account](std::string &out)
            {
                GError *error = nullptr;
                secret_password_clear_sync(Schema(), nullptr, &error, "service", ServiceName,
                                           "account", account.c_str(), nullptr);
                if (error != nullptr)
                {
                    g_error_free(error);
                }
                out = "1";
                return true;
            });
    }

private:
    static constexpr const char *ServiceName = "clavister-oneconnect";

    static const SecretSchema *Schema()
    {
        static const SecretSchema schema = {"io.github.stuffz.oneconnect.Password",
                                            SECRET_SCHEMA_NONE,
                                            {
                                                {"service", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                                {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                                {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
                                            },
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0};

        return &schema;
    }

    // The uid whose keychain to use: whoever invoked sudo or pkexec.
    static uid_t TargetUid()
    {
        // Unprivileged is the normal case: our own keychain is the right one,
        // and a stale SUDO_USER left in the environment must not redirect us.
        if (geteuid() != 0)
        {
            return getuid();
        }

        if (const char *pkexecUid = std::getenv("PKEXEC_UID"))
        {
            if (*pkexecUid != '\0')
            {
                return static_cast<uid_t>(std::strtoul(pkexecUid, nullptr, 10));
            }
        }

        if (const char *sudoUser = std::getenv("SUDO_USER"))
        {
            if (*sudoUser != '\0')
            {
                if (const struct passwd *entry = getpwnam(sudoUser))
                {
                    return entry->pw_uid;
                }
            }
        }

        // Elevated with no record of who invoked us: a real uid survives a
        // setuid binary, but plain root has no keychain to target.
        const uid_t self = getuid();
        return self == 0 ? static_cast<uid_t>(-1) : self;
    }

    // Derived rather than read from the environment, which pkexec sanitises;
    // the socket path is predictable.
    static std::string BusAddress()
    {
        const uid_t uid = TargetUid();
        if (uid == static_cast<uid_t>(-1))
        {
            return {};
        }

        const std::string path = "/run/user/" + std::to_string(uid) + "/bus";
        if (access(path.c_str(), F_OK) != 0)
        {
            return {};
        }

        return "unix:path=" + path;
    }

    // Runs `work` in a forked child that has dropped to the target uid, and
    // returns whatever the child wrote. The parent never touches libsecret.
    template <typename Work>
    static std::string RunAsUser(Work work)
    {
        const uid_t uid = TargetUid();
        const std::string bus = BusAddress();

        if (uid == static_cast<uid_t>(-1) || bus.empty())
        {
            return {};
        }

        int pipeFds[2] = {-1, -1};
        if (pipe(pipeFds) != 0)
        {
            return {};
        }

        const pid_t child = fork();

        if (child < 0)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            return {};
        }

        if (child == 0)
        {
            close(pipeFds[0]);
            DropPrivilegesAndRun(uid, bus, pipeFds[1], work);
            _exit(0); // unreachable; DropPrivilegesAndRun exits
        }

        close(pipeFds[1]);

        std::string result;
        char buffer[512];
        ssize_t count = 0;

        while ((count = read(pipeFds[0], buffer, sizeof(buffer))) > 0)
        {
            result.append(buffer, static_cast<size_t>(count));
        }

        close(pipeFds[0]);

        int status = 0;
        waitpid(child, &status, 0);

        return result;
    }

    template <typename Work>
    [[noreturn]] static void DropPrivilegesAndRun(uid_t uid, const std::string &bus, int outFd,
                                                  Work work)
    {
        const struct passwd *entry = getpwuid(uid);
        const gid_t gid = entry != nullptr ? entry->pw_gid : uid;

        // Order matters: groups, then gid, then uid. Dropping uid first would
        // remove the privilege needed to drop the rest.
        if (entry != nullptr)
        {
            initgroups(entry->pw_name, gid);
        }

        if (setgid(gid) != 0 || setuid(uid) != 0)
        {
            _exit(1);
        }

        // Refuse to continue if privileges could be regained.
        if (setuid(0) == 0)
        {
            _exit(1);
        }

        setenv("DBUS_SESSION_BUS_ADDRESS", bus.c_str(), 1);

        std::string out;
        const bool ok = work(out);

        if (ok && !out.empty())
        {
            const ssize_t written = write(outFd, out.data(), out.size());
            static_cast<void>(written);
        }

        close(outFd);
        _exit(ok ? 0 : 1);
    }
};
