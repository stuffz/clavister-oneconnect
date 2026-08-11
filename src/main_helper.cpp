// The only privileged component: creates the tun device (fd handed back over
// SCM_RIGHTS) and runs vpnc-script. Everything else stays in the unprivileged
// client; see docs/PRIVILEGE.md. It connects *out* to a socket the client
// listens on, so no persistent root-owned endpoint exists for anything else
// to find.

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <linux/if.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "platform/tun_device.hpp"
#include "platform/tun_protocol.hpp"

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

namespace
{
    // An allowlist, not a denylist: this environment is handed to a
    // root-executed shell script, and the client is the untrusted side of the
    // boundary.
    const std::vector<std::string> &AllowedVariables()
    {
        static const std::vector<std::string> allowed = {
            "reason",
            "VPNGATEWAY",
            "TUNDEV",
            "INTERNAL_IP4_ADDRESS",
            "INTERNAL_IP4_MTU",
            "INTERNAL_IP4_NETMASK",
            "INTERNAL_IP4_NETMASKLEN",
            "INTERNAL_IP4_NETADDR",
            "INTERNAL_IP4_DNS",
            "INTERNAL_IP4_NBNS",
            "INTERNAL_IP6_ADDRESS",
            "INTERNAL_IP6_NETMASK",
            "INTERNAL_IP6_DNS",
            "CISCO_DEF_DOMAIN",
            "CISCO_BANNER",
            "CISCO_SPLIT_DNS",
            "VPNPID",
            "IDLE_TIMEOUT",
        };
        return allowed;
    }

    bool IsAllowed(const std::string &name)
    {
        for (const std::string &allowed : AllowedVariables())
        {
            if (name == allowed)
            {
                return true;
            }
        }

        // Split routes are indexed: CISCO_SPLIT_INC_0_ADDR and so on.
        static const char *prefixes[] = {"CISCO_SPLIT_INC", "CISCO_SPLIT_EXC",
                                         "CISCO_IPV6_SPLIT_INC", "CISCO_IPV6_SPLIT_EXC"};

        for (const char *prefix : prefixes)
        {
            if (name.rfind(prefix, 0) == 0)
            {
                return true;
            }
        }

        return false;
    }

    // A crafted name or value must not become a second protocol line or a
    // second shell statement.
    bool IsSaneName(const std::string &name)
    {
        if (name.empty() || name.size() > 64)
        {
            return false;
        }

        for (const char c : name)
        {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '_';
            if (!ok)
            {
                return false;
            }
        }

        return true;
    }

    bool IsSaneValue(const std::string &value)
    {
        if (value.size() > 4096)
        {
            return false;
        }

        return value.find('\n') == std::string::npos && value.find('\0') == std::string::npos;
    }

    bool HandleMtu(int socket, const std::string &argument)
    {
        const size_t space = argument.find(' ');
        if (space == std::string::npos)
        {
            return TunProtocol::SendLine(socket, std::string(TunProtocol::Error) +
                                                     " usage: MTU <ifname> <bytes>");
        }

        const std::string ifname = argument.substr(0, space);
        const int mtu = static_cast<int>(std::strtol(argument.c_str() + space + 1, nullptr, 10));

        // The bounds are the point: this value comes from the untrusted side.
        if (ifname.empty() || ifname.size() >= IFNAMSIZ || mtu < 576 || mtu > 65535)
        {
            return TunProtocol::SendLine(socket, std::string(TunProtocol::Error) +
                                                     " invalid interface or mtu");
        }

        if (!TunDevice::SetMtu(ifname, mtu))
        {
            return TunProtocol::SendLine(socket,
                                         std::string(TunProtocol::Error) + " SIOCSIFMTU failed");
        }

        std::cerr << "helper: set " << ifname << " mtu to " << mtu << "\n";
        return TunProtocol::SendLine(socket, TunProtocol::Ok);
    }

    bool HandleSetup(int socket, const std::string &argument)
    {
        std::string ifname = argument;
        const int tun = TunDevice::Create(ifname);

        if (tun < 0)
        {
            TunProtocol::SendLine(socket,
                                  std::string(TunProtocol::Error) + " could not create tun device");
            return true;
        }

        // The kernel may have picked a different name; the client needs it for
        // vpnc-script's TUNDEV.
        const bool sent =
            TunProtocol::SendFd(socket, tun, std::string(TunProtocol::Ok) + " " + ifname + "\n");
        close(tun);

        return sent;
    }

    bool HandleScript(int socket, const std::string &reason, const std::string &script)
    {
        std::map<std::string, std::string> environment;
        environment["reason"] = reason;

        std::string line;
        while (TunProtocol::ReadLine(socket, line))
        {
            if (line.empty())
            {
                break;
            }

            const size_t equals = line.find('=');
            if (equals == std::string::npos)
            {
                continue;
            }

            const std::string name = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);

            if (!IsSaneName(name) || !IsSaneValue(value) || !IsAllowed(name))
            {
                std::cerr << "helper: refusing environment variable '" << name << "'\n";
                continue;
            }

            environment[name] = value;
        }

        std::string error;
        if (!TunDevice::RunScript(script, environment, error))
        {
            return TunProtocol::SendLine(socket, std::string(TunProtocol::Error) + " " + error);
        }

        return TunProtocol::SendLine(socket, TunProtocol::Ok);
    }
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "oneconnect-helper " GIT_HASH "\n"
                  << "Not meant to be run by hand. Usage: oneconnect-helper <socket-path> "
                     "[vpnc-script]\n";
        return 2;
    }

    const std::string socketPath = argv[1];
    const std::string script = argc > 2 ? argv[2] : "/etc/vpnc/vpnc-script";

    if (geteuid() != 0)
    {
        std::cerr << "oneconnect-helper must run as root\n";
        return 1;
    }

    const int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0)
    {
        return 1;
    }

    struct sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socketPath.c_str(), sizeof(address.sun_path) - 1);

    if (connect(client, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0)
    {
        std::cerr << "helper: could not connect to " << socketPath << "\n";
        close(client);
        return 1;
    }

    // Serve only the user polkit authorised, through a socket that user owns.
    //
    // This is the check Mozilla VPN got wrong (CVE-2023-4104): their privileged
    // service asked polkit whether *the service* was authorised rather than the
    // caller, and since the service ran as root the answer was always yes. The
    // equivalent mistake here would be fetching SO_PEERCRED and not comparing
    // it to anything.
    struct ucred peer{};
    socklen_t peerLength = sizeof(peer);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peerLength) != 0)
    {
        std::cerr << "helper: could not read peer credentials\n";
        close(client);
        return 1;
    }

    // pkexec sets PKEXEC_UID to the uid it authenticated.
    if (const char *authorised = std::getenv("PKEXEC_UID"))
    {
        const uid_t expected = static_cast<uid_t>(std::strtoul(authorised, nullptr, 10));
        if (peer.uid != expected)
        {
            std::cerr << "helper: peer uid " << peer.uid << " is not the authorised uid "
                      << expected << "\n";
            close(client);
            return 1;
        }
    }

    // The socket must belong to that same user, so the helper cannot be
    // pointed at someone else's endpoint.
    struct stat socketInfo{};
    if (stat(socketPath.c_str(), &socketInfo) != 0 || socketInfo.st_uid != peer.uid)
    {
        std::cerr << "helper: socket is not owned by the connecting user\n";
        close(client);
        return 1;
    }

    std::string line;
    while (TunProtocol::ReadLine(client, line))
    {
        const size_t space = line.find(' ');
        const std::string command = space == std::string::npos ? line : line.substr(0, space);
        const std::string argument = space == std::string::npos ? "" : line.substr(space + 1);

        if (command == TunProtocol::ByeCommand)
        {
            break;
        }

        if (command == TunProtocol::SetupCommand)
        {
            if (!HandleSetup(client, argument))
            {
                break;
            }
        }
        else if (command == TunProtocol::MtuCommand)
        {
            if (!HandleMtu(client, argument))
            {
                break;
            }
        }
        else if (command == TunProtocol::ScriptCommand)
        {
            if (!HandleScript(client, argument, script))
            {
                break;
            }
        }
        else
        {
            TunProtocol::SendLine(client, std::string(TunProtocol::Error) + " unknown command");
        }
    }

    close(client);
    return 0;
}
