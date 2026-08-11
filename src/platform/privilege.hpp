#pragma once

#include <string>
#include <vector>

#include <unistd.h>

// The console client runs as root; the GUI delegates tun setup to
// oneconnect-helper and stays unprivileged, since a root process cannot reach
// the session bus for the tray icon or the keychain. See docs/PRIVILEGE.md.
class Privilege
{
public:
    static bool HasNetworkAdmin()
    {
        return geteuid() == 0;
    }

    // In search order; the build directory last, so a freshly compiled tree
    // works without installing.
    static const std::vector<std::string> &Candidates()
    {
        static const std::vector<std::string> paths = {
            "/usr/local/lib/oneconnect/oneconnect-helper",
            "/usr/lib/oneconnect/oneconnect-helper",
            "/usr/local/bin/oneconnect-helper",
            "/usr/bin/oneconnect-helper",
            "build/release/oneconnect-helper",
        };
        return paths;
    }

    static std::string HelperPath()
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

    static std::string Explanation()
    {
        return "Creating the tun device and installing routes requires root.\n"
               "Re-run with sudo, or grant the binary CAP_NET_ADMIN:\n"
               "  sudo setcap cap_net_admin+ep /usr/local/bin/oneconnect";
    }
};
