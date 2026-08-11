#pragma once

#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Wire protocol between the unprivileged client and the privileged helper.
// Deliberately tiny: the helper runs as root, so its parsing surface should be
// readable in one sitting.
//
//   SETUP <ifname>\n            -> "OK <ifname>\n" plus a tun fd over SCM_RIGHTS
//   SCRIPT <reason>\n<K=V>\n... -> "OK\n" or "ERR <message>\n" (env block ends
//                                  with a blank line)
//   MTU <ifname> <bytes>\n      -> "OK\n" or "ERR <message>\n"
//   BYE\n                       -> helper exits
namespace TunProtocol
{
    constexpr const char *SetupCommand = "SETUP";
    constexpr const char *ScriptCommand = "SCRIPT";
    constexpr const char *MtuCommand = "MTU";
    constexpr const char *ByeCommand = "BYE";

    constexpr const char *Ok = "OK";
    constexpr const char *Error = "ERR";

    inline bool SendFd(int socket, int fd, const std::string &message)
    {
        struct iovec iov{};
        iov.iov_base = const_cast<char *>(message.data());
        iov.iov_len = message.size();

        char control[CMSG_SPACE(sizeof(int))] = {};

        struct msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        struct cmsghdr *header = CMSG_FIRSTHDR(&msg);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(header), &fd, sizeof(int));

        return sendmsg(socket, &msg, 0) >= 0;
    }

    // `fd` is -1 when the peer attached no descriptor.
    inline bool ReceiveFd(int socket, std::string &message, int &fd)
    {
        char buffer[256] = {};

        struct iovec iov{};
        iov.iov_base = buffer;
        iov.iov_len = sizeof(buffer) - 1;

        char control[CMSG_SPACE(sizeof(int))] = {};

        struct msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);

        const ssize_t count = recvmsg(socket, &msg, 0);
        if (count <= 0)
        {
            return false;
        }

        message.assign(buffer, static_cast<size_t>(count));
        fd = -1;

        for (struct cmsghdr *header = CMSG_FIRSTHDR(&msg); header != nullptr;
             header = CMSG_NXTHDR(&msg, header))
        {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS)
            {
                memcpy(&fd, CMSG_DATA(header), sizeof(int));
                break;
            }
        }

        return true;
    }

    inline bool SendLine(int socket, const std::string &line)
    {
        const std::string payload = line + "\n";
        return write(socket, payload.data(), payload.size()) ==
               static_cast<ssize_t>(payload.size());
    }

    // Bounded so a hostile peer cannot make the helper allocate without limit.
    inline bool ReadLine(int socket, std::string &line, size_t limit = 4096)
    {
        line.clear();

        char c = 0;
        while (line.size() < limit)
        {
            const ssize_t count = read(socket, &c, 1);
            if (count <= 0)
            {
                return false;
            }
            if (c == '\n')
            {
                return true;
            }
            line.push_back(c);
        }

        return false;
    }
} // namespace TunProtocol
