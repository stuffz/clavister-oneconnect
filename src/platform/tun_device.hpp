#pragma once

#include <cstring>
#include <map>
#include <string>

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

// Tun device creation and vpnc-script execution, for a process that already
// has CAP_NET_ADMIN.
class TunDevice
{
public:
    // Returns the fd, or -1. `ifname` is updated to the name the kernel
    // assigned.
    static int Create(std::string &ifname)
    {
        const int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
        if (fd < 0)
        {
            return -1;
        }

        struct ifreq request{};
        request.ifr_flags = IFF_TUN | IFF_NO_PI;

        if (!ifname.empty())
        {
            std::strncpy(request.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        }

        if (ioctl(fd, TUNSETIFF, &request) < 0)
        {
            close(fd);
            return -1;
        }

        ifname = request.ifr_name;
        return fd;
    }

    static bool SetMtu(const std::string &ifname, int mtu)
    {
        if (ifname.empty() || ifname.size() >= IFNAMSIZ || mtu < 576 || mtu > 65535)
        {
            return false;
        }

        const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            return false;
        }

        struct ifreq request{};
        std::strncpy(request.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
        request.ifr_mtu = mtu;

        const bool ok = ioctl(sock, SIOCSIFMTU, &request) == 0;
        close(sock);

        return ok;
    }

    // execl, not a shell: no chance of a value being reinterpreted as a
    // command.
    static bool RunScript(const std::string &script,
                          const std::map<std::string, std::string> &environment, std::string &error)
    {
        const pid_t child = fork();

        if (child < 0)
        {
            error = "fork failed";
            return false;
        }

        if (child == 0)
        {
            for (const auto &entry : environment)
            {
                setenv(entry.first.c_str(), entry.second.c_str(), 1);
            }

            execl(script.c_str(), script.c_str(), nullptr);
            _exit(127);
        }

        int status = 0;
        waitpid(child, &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            error = "vpnc-script exited with status " +
                    std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            return false;
        }

        return true;
    }
};
