#pragma once

#include <cstring>
#include <map>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/logger.hpp"
#include "platform/tun_protocol.hpp"

// Launches oneconnect-helper through pkexec and waits for it to connect back
// to a socket in $XDG_RUNTIME_DIR. Listening rather than connecting is
// deliberate: no long-lived root-owned socket sits on the system. The helper
// checks who owns the socket it was pointed at; this side checks that the
// peer is uid 0.
class PrivilegedClient
{
public:
    PrivilegedClient() = default;

    ~PrivilegedClient()
    {
        Stop();
    }

    PrivilegedClient(const PrivilegedClient &) = delete;
    PrivilegedClient &operator=(const PrivilegedClient &) = delete;

    const std::string &LastError() const
    {
        return lastError;
    }

    bool IsRunning() const
    {
        return peerFd >= 0;
    }

    // Blocks until the helper connects back or the attempt fails, which
    // includes the user dismissing the polkit prompt.
    bool Start(const std::string &helperPath, const std::string &vpncScript)
    {
        Stop();

        socketPath = RuntimeDirectory() + "/oneconnect-helper.sock";
        unlink(socketPath.c_str());

        listenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listenFd < 0)
        {
            lastError = "could not create socket";
            return false;
        }

        struct sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, socketPath.c_str(), sizeof(address.sun_path) - 1);

        if (bind(listenFd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0 ||
            listen(listenFd, 1) != 0)
        {
            lastError = "could not bind " + socketPath;
            Stop();
            return false;
        }

        chmod(socketPath.c_str(), 0600);

        helperPid = fork();

        if (helperPid < 0)
        {
            lastError = "fork failed";
            Stop();
            return false;
        }

        if (helperPid == 0)
        {
            // pkexec, unlike sudo, prompts graphically and needs no terminal.
            execlp("pkexec", "pkexec", helperPath.c_str(), socketPath.c_str(), vpncScript.c_str(),
                   nullptr);
            _exit(127);
        }

        peerFd = accept(listenFd, nullptr, nullptr);

        if (peerFd < 0)
        {
            lastError = "helper did not connect (authentication cancelled?)";
            Stop();
            return false;
        }

        if (!PeerIsRoot())
        {
            lastError = "helper connected but is not root";
            Stop();
            return false;
        }

        LOG_INFO("Privileged helper connected");
        return true;
    }

    void Stop()
    {
        if (peerFd >= 0)
        {
            TunProtocol::SendLine(peerFd, TunProtocol::ByeCommand);
            close(peerFd);
            peerFd = -1;
        }

        if (listenFd >= 0)
        {
            close(listenFd);
            listenFd = -1;
        }

        if (!socketPath.empty())
        {
            unlink(socketPath.c_str());
            socketPath.clear();
        }

        if (helperPid > 0)
        {
            waitpid(helperPid, nullptr, WNOHANG);
            helperPid = -1;
        }
    }

    // Returns the tun fd, or -1. `ifname` is updated to the name the kernel
    // assigned.
    int SetupTun(std::string &ifname)
    {
        if (peerFd < 0)
        {
            lastError = "helper is not running";
            return -1;
        }

        if (!TunProtocol::SendLine(peerFd, std::string(TunProtocol::SetupCommand) + " " + ifname))
        {
            lastError = "could not send SETUP";
            return -1;
        }

        std::string reply;
        int fd = -1;

        if (!TunProtocol::ReceiveFd(peerFd, reply, fd))
        {
            lastError = "no reply to SETUP";
            return -1;
        }

        if (reply.rfind(TunProtocol::Ok, 0) != 0 || fd < 0)
        {
            lastError = Trim(reply);
            return -1;
        }

        // "OK <ifname>\n"
        const size_t space = reply.find(' ');
        if (space != std::string::npos)
        {
            ifname = Trim(reply.substr(space + 1));
        }

        return fd;
    }

    bool SetMtu(const std::string &ifname, int mtu)
    {
        if (peerFd < 0)
        {
            return false;
        }

        if (!TunProtocol::SendLine(peerFd, std::string(TunProtocol::MtuCommand) + " " + ifname +
                                               " " + std::to_string(mtu)))
        {
            return false;
        }

        std::string reply;
        if (!TunProtocol::ReadLine(peerFd, reply))
        {
            return false;
        }

        if (reply.rfind(TunProtocol::Ok, 0) != 0)
        {
            lastError = Trim(reply);
            return false;
        }

        return true;
    }

    bool RunScript(const std::string &reason, const std::map<std::string, std::string> &environment)
    {
        if (peerFd < 0)
        {
            lastError = "helper is not running";
            return false;
        }

        if (!TunProtocol::SendLine(peerFd, std::string(TunProtocol::ScriptCommand) + " " + reason))
        {
            return false;
        }

        for (const auto &entry : environment)
        {
            if (!TunProtocol::SendLine(peerFd, entry.first + "=" + entry.second))
            {
                return false;
            }
        }

        TunProtocol::SendLine(peerFd, "");

        std::string reply;
        if (!TunProtocol::ReadLine(peerFd, reply))
        {
            lastError = "no reply to SCRIPT";
            return false;
        }

        if (reply.rfind(TunProtocol::Ok, 0) != 0)
        {
            lastError = Trim(reply);
            return false;
        }

        return true;
    }

private:
    bool PeerIsRoot() const
    {
        struct ucred peer{};
        socklen_t length = sizeof(peer);

        if (getsockopt(peerFd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0)
        {
            return false;
        }

        return peer.uid == 0;
    }

    static std::string RuntimeDirectory()
    {
        if (const char *dir = std::getenv("XDG_RUNTIME_DIR"))
        {
            if (*dir != '\0')
            {
                return dir;
            }
        }

        return "/tmp";
    }

    static std::string Trim(const std::string &value)
    {
        std::string out = value;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        {
            out.pop_back();
        }
        return out;
    }

    int listenFd = -1;
    int peerFd = -1;
    pid_t helperPid = -1;
    std::string socketPath;
    std::string lastError;
};
