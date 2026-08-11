#pragma once

#include <cerrno>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "core/logger.hpp"

// One tunnel at a time, across the console client and the GUI. An abstract
// unix socket rather than a lock file because the two binaries run as
// different users, abstract names have no owner or permissions, and the
// kernel releases the name however the process dies. The holder also listens,
// so a second instance can ask which pid is in the way.
class SingleInstanceLock
{
public:
    enum class Result
    {
        Acquired,
        AlreadyRunning,
        Unavailable
    };

    static constexpr const char *DefaultName = "clavister-oneconnect";

    SingleInstanceLock() = default;

    ~SingleInstanceLock()
    {
        Release();
    }

    SingleInstanceLock(const SingleInstanceLock &) = delete;
    SingleInstanceLock &operator=(const SingleInstanceLock &) = delete;

    Result Acquire(const std::string &name = DefaultName)
    {
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (fd < 0)
        {
            LOG_ERROR(std::string("Could not create the instance socket: ") + std::strerror(errno));
            // Fail open: refusing to start because the guard is unavailable is
            // a worse outcome than running without it.
            return Result::Unavailable;
        }

        struct sockaddr_un address{};
        const socklen_t length = FillAddress(address, name);

        if (bind(fd, reinterpret_cast<struct sockaddr *>(&address), length) == 0)
        {
            listen(fd, 4);
            return Result::Acquired;
        }

        const int error = errno;

        if (error == EADDRINUSE)
        {
            holder = AskHolder(name);
            Release();
            return Result::AlreadyRunning;
        }

        LOG_ERROR(std::string("Could not bind the instance socket: ") + std::strerror(error));
        Release();
        return Result::Unavailable;
    }

    // Answers a waiting second instance with our pid; does nothing when nobody
    // is asking.
    void ServePending()
    {
        if (fd < 0)
        {
            return;
        }

        while (true)
        {
            const int peer = accept(fd, nullptr, nullptr);
            if (peer < 0)
            {
                return;
            }

            const std::string reply = std::to_string(getpid()) + "\n";
            const ssize_t written = write(peer, reply.data(), reply.size());
            static_cast<void>(written);
            close(peer);
        }
    }

    const std::string &HolderPid() const
    {
        return holder;
    }

    std::string HolderDescription() const
    {
        return holder.empty() ? std::string("another instance")
                              : "another instance (pid " + holder + ")";
    }

private:
    static socklen_t FillAddress(struct sockaddr_un &address, const std::string &name)
    {
        address.sun_family = AF_UNIX;
        address.sun_path[0] = '\0'; // abstract namespace
        std::strncpy(address.sun_path + 1, name.c_str(), sizeof(address.sun_path) - 2);

        return static_cast<socklen_t>(offsetof(struct sockaddr_un, sun_path) + 1 + name.size());
    }

    static std::string AskHolder(const std::string &name)
    {
        const int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (probe < 0)
        {
            return {};
        }

        struct sockaddr_un address{};
        const socklen_t length = FillAddress(address, name);

        std::string pid;

        if (connect(probe, reinterpret_cast<struct sockaddr *>(&address), length) == 0)
        {
            char buffer[32] = {};
            const ssize_t count = read(probe, buffer, sizeof(buffer) - 1);
            if (count > 0)
            {
                pid.assign(buffer, static_cast<size_t>(count));
                while (!pid.empty() && (pid.back() == '\n' || pid.back() == '\r'))
                {
                    pid.pop_back();
                }
            }
        }

        close(probe);
        return pid;
    }

    void Release()
    {
        if (fd >= 0)
        {
            close(fd);
            fd = -1;
        }
    }

    int fd = -1;
    std::string holder;
};
