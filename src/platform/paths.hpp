#pragma once

#include <cstdlib>
#include <string>

#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Log and config locations. Under sudo, $HOME is /root, so the invoking user
// wins: SUDO_USER is resolved back to their home and ownership is handed back.
class Paths
{
public:
    static std::string LogFile()
    {
        const std::string directory = StateDirectory();

        if (!EnsureDirectory(directory))
        {
            return "/tmp/clavister-oneconnect.log";
        }

        return directory + "/oneconnect.log";
    }

    static std::string ConnectionsFile()
    {
        const std::string directory = ConfigDirectory();
        EnsureDirectory(directory);
        return directory + "/connections.yaml";
    }

    static std::string ConfigDirectory()
    {
        const std::string home = InvokingUserHome();

        if (home.empty())
        {
            return "/etc/clavister-oneconnect";
        }

        return home + "/.config/clavister-oneconnect";
    }

    // Best effort: a file created while root should be readable by the user
    // who ran sudo.
    static void ReturnOwnership(const std::string &path)
    {
        const char *sudoUser = std::getenv("SUDO_USER");
        if (sudoUser == nullptr)
        {
            return;
        }

        const struct passwd *entry = getpwnam(sudoUser);
        if (entry == nullptr)
        {
            return;
        }

        static_cast<void>(chown(path.c_str(), entry->pw_uid, entry->pw_gid));
    }

private:
    static std::string StateDirectory()
    {
        const std::string home = InvokingUserHome();

        if (!home.empty())
        {
            const char *xdgState = std::getenv("XDG_STATE_HOME");

            // Under sudo, XDG_STATE_HOME may still point at another user's
            // path.
            if (xdgState != nullptr && *xdgState != '\0' && std::getenv("SUDO_USER") == nullptr)
            {
                return std::string(xdgState) + "/clavister-oneconnect";
            }

            return home + "/.local/state/clavister-oneconnect";
        }

        return "/tmp";
    }

    static std::string InvokingUserHome()
    {
        // A stale SUDO_USER in an unprivileged process (what `sudo -u someone`
        // leaves behind) must not redirect files to the wrong home.
        const char *sudoUser = geteuid() == 0 ? std::getenv("SUDO_USER") : nullptr;

        if (sudoUser != nullptr && *sudoUser != '\0')
        {
            const struct passwd *entry = getpwnam(sudoUser);
            if (entry != nullptr && entry->pw_dir != nullptr)
            {
                return entry->pw_dir;
            }
        }

        const char *home = std::getenv("HOME");
        return home != nullptr ? std::string(home) : std::string();
    }

    static bool EnsureDirectory(const std::string &path)
    {
        std::string partial;

        for (size_t i = 0; i <= path.size(); ++i)
        {
            if (i == path.size() || path[i] == '/')
            {
                if (partial.size() > 1)
                {
                    if (mkdir(partial.c_str(), 0755) != 0 && errno != EEXIST)
                    {
                        return false;
                    }
                }
            }

            if (i < path.size())
            {
                partial += path[i];
            }
        }

        return true;
    }
};
