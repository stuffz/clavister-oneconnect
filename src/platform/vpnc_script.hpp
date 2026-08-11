#pragma once

#include <string>
#include <vector>

#include <unistd.h>

// The script is portable; its location is not (/etc/vpnc on Arch and Fedora,
// /usr/share/vpnc-scripts on Debian and Ubuntu), so hardcoding either path
// breaks on the other family.
class VpncScript
{
public:
    static const std::vector<std::string> &Candidates()
    {
        static const std::vector<std::string> paths = {
            "/etc/vpnc/vpnc-script",
            "/usr/share/vpnc-scripts/vpnc-script",
            "/usr/local/etc/vpnc/vpnc-script",
            "/usr/local/share/vpnc-scripts/vpnc-script",
        };
        return paths;
    }

    static std::string Find()
    {
        for (const std::string &candidate : Candidates())
        {
            if (access(candidate.c_str(), X_OK) == 0)
            {
                return candidate;
            }
        }
        return {};
    }

    static std::string SearchedPaths()
    {
        std::string out;
        for (const std::string &candidate : Candidates())
        {
            out += "  " + candidate + "\n";
        }
        return out;
    }

    static std::string InstallHint()
    {
        return "Install it with one of:\n"
               "  pacman -S vpnc              (Arch, CachyOS)\n"
               "  apt install vpnc-scripts    (Debian, Ubuntu)\n"
               "  dnf install vpnc-script     (Fedora)";
    }
};
