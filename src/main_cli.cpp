#include <csignal>
#include <exception>
#include <iostream>
#include <string>

#include "core/logger.hpp"
#include "core/profile_store.hpp"
#include "core/route_reconciler.hpp"
#include "core/vpn_session.hpp"
#include "platform/paths.hpp"
#include "platform/privilege.hpp"
#include "platform/single_instance.hpp"
#include "ui/console_prompter.hpp"
#include "ui/tunnel_report.hpp"

#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

using std::string;

namespace
{
    // The signal handler may only touch this through the async-signal-safe
    // Cancel().
    VpnSession *activeSession = nullptr;

    void HandleSignal(int)
    {
        if (activeSession != nullptr)
        {
            activeSession->Cancel();
        }
    }

    void PrintUsage()
    {
        std::cout
            << "oneconnect -- a Clavister OneConnect / AnyConnect VPN client\n\n"
            << "Usage:\n"
            << "  oneconnect --gateway HOST [options]\n"
            << "  oneconnect --profile NAME [options]\n\n"
            << "Connections are shared with the GUI, in\n"
            << "~/.config/clavister-oneconnect/connections.yaml\n\n"
            << "Options:\n"
            << "  -P, --profile NAME     Use a saved connection\n"
            << "  -l, --list             List saved connections and exit\n"
            << "  -g, --gateway HOST     Gateway FQDN. Use the name, never an IP:\n"
            << "                         Clavister rejects IP-addressed tunnel requests\n"
            << "                         when its Host Name restriction is set.\n"
            << "  -p, --protocol NAME    anyconnect (default), nc, gp, pulse, f5, fortinet, array\n"
            << "  -c, --cafile PATH      Extra CA certificate to trust\n"
            << "  -i, --interface NAME   Tunnel interface name (default: kernel picks)\n"
            << "  -s, --script PATH      vpnc-script (default: found automatically)\n"
            << "      --no-dtls          Skip the DTLS/UDP attempt\n"
            << "      --no-dns           Leave the system resolver alone. Use when the\n"
            << "                         gateway pushes a public resolver that displaces\n"
            << "                         your own without resolving anything internal.\n"
            << "      --user-agent STR   Override the User-Agent. The default announces an\n"
            << "                         AnyConnect-compatible client; gateways that do not\n"
            << "                         recognise one serve a portal page instead of the\n"
            << "                         XML auth form.\n"
            << "      --os NAME          Reported client OS: linux, linux-64 (default), win,\n"
            << "                         mac-intel, android, apple-ios\n"
            << "  -d, --debug            Verbose openconnect progress output\n"
            << "      --trace            Dump raw HTTP too. Credentials are scrubbed\n"
            << "                         on a best-effort basis -- read before sharing.\n"
            << "  -v, --version          Print version and exit\n"
            << "  -h, --help             This text\n";
    }

    void PrintVersion()
    {
        std::cout << "oneconnect " << GIT_HASH << " (built " << BUILD_DATE << ")\n";
    }

    bool NeedsValue(int argc, int index)
    {
        return index + 1 < argc;
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        SessionOptions options;
        bool debug = false;
        bool trace = false;
        string profileName;
        bool listProfiles = false;

        const string storePath = Paths::ConnectionsFile();
        ProfileStore store;
        if (!store.Load(storePath))
        {
            std::cerr << "warning: could not read " << storePath << ": " << store.LastError()
                      << "\n";
        }

        for (int i = 1; i < argc; ++i)
        {
            const string arg = argv[i];

            if (arg == "-h" || arg == "--help")
            {
                PrintUsage();
                return 0;
            }
            if (arg == "-v" || arg == "--version")
            {
                PrintVersion();
                return 0;
            }
            if (arg == "-l" || arg == "--list")
            {
                listProfiles = true;
            }
            else if ((arg == "-P" || arg == "--profile") && NeedsValue(argc, i))
            {
                profileName = argv[++i];
            }
            else if ((arg == "-g" || arg == "--gateway") && NeedsValue(argc, i))
            {
                options.gateway = argv[++i];
            }
            else if ((arg == "-p" || arg == "--protocol") && NeedsValue(argc, i))
            {
                options.protocol = argv[++i];
            }
            else if ((arg == "-c" || arg == "--cafile") && NeedsValue(argc, i))
            {
                options.caFile = argv[++i];
            }
            else if ((arg == "-i" || arg == "--interface") && NeedsValue(argc, i))
            {
                options.interfaceName = argv[++i];
            }
            else if ((arg == "-s" || arg == "--script") && NeedsValue(argc, i))
            {
                options.vpncScript = argv[++i];
            }
            else if (arg == "--no-dtls")
            {
                options.dtlsAttemptPeriod = 0;
            }
            else if (arg == "--no-dns")
            {
                options.ignorePushedDns = true;
            }
            else if (arg == "--user-agent" && NeedsValue(argc, i))
            {
                options.userAgent = argv[++i];
            }
            else if (arg == "--os" && NeedsValue(argc, i))
            {
                options.reportedOs = argv[++i];
            }
            else if (arg == "-d" || arg == "--debug")
            {
                debug = true;
                options.logLevel = PRG_DEBUG;
            }
            else if (arg == "--trace")
            {
                debug = true;
                trace = true;
                options.logLevel = PRG_TRACE;
            }
            else
            {
                std::cerr << "unknown or incomplete argument: " << arg << "\n\n";
                PrintUsage();
                return 2;
            }
        }

        if (listProfiles)
        {
            if (store.Profiles().empty())
            {
                std::cout << "no saved connections in " << storePath << "\n";
                return 0;
            }

            for (const ConnectionProfile &profile : store.Profiles())
            {
                std::cout << "  " << profile.name << "\t" << profile.gateway
                          << (profile.autoConnect ? "\t[autoconnect]" : "") << "\n";
            }
            return 0;
        }

        // A named profile supplies the defaults; explicit arguments override it.
        if (!profileName.empty())
        {
            const ConnectionProfile *profile = store.Find(profileName);
            if (profile == nullptr)
            {
                std::cerr << "error: no connection called '" << profileName << "'\n"
                          << "       --list shows what is saved\n";
                return 2;
            }

            SessionOptions fromProfile = profile->ToSessionOptions();

            if (!options.gateway.empty())
            {
                fromProfile.gateway = options.gateway;
            }
            fromProfile.logLevel = options.logLevel;

            options = fromProfile;
        }

        if (options.gateway.empty())
        {
            std::cerr << "error: no gateway given\n\n"
                      << "Use one of:\n"
                      << "  oneconnect --gateway vpn.example.com\n"
                      << "  oneconnect --profile <name>          (--list to see them)\n\n"
                      << "Connections are stored in " << storePath << "\n"
                      << "and shared with the GUI.\n";
            return 2;
        }

        const std::string logPath = Paths::LogFile();
        Logger::Instance().SetLogFile(logPath);
        Paths::ReturnOwnership(logPath);
        Logger::Instance().SetDebugMode(debug);

        LOG_INFO("---- oneconnect " GIT_HASH " starting ----");
        LOG_INFO(profileName.empty() ? "ad-hoc connection (no profile)"
                                     : "profile=" + profileName + " from " + storePath);
        LOG_INFO("gateway=" + options.gateway + " protocol=" + options.protocol +
                 " loglevel=" + std::to_string(options.logLevel));

        if (trace)
        {
            std::cerr << "warning: --trace dumps raw HTTP. Credentials are scrubbed on a\n"
                      << "         best-effort basis; read " << logPath << " before sharing it.\n";
        }

        if (!Privilege::HasNetworkAdmin())
        {
            std::cerr << "error: insufficient privileges\n" << Privilege::Explanation() << "\n";
            return 1;
        }

        SingleInstanceLock instanceLock;

        if (instanceLock.Acquire() == SingleInstanceLock::Result::AlreadyRunning)
        {
            std::cerr << "error: " << instanceLock.HolderDescription() << " is already connected.\n"
                      << "       Only one tunnel at a time -- the console client and the GUI\n"
                      << "       share this lock.\n";
            LOG_ERROR("Refusing to start: " + instanceLock.HolderDescription() + " holds the lock");
            return 1;
        }

        ConsolePrompter prompter;
        VpnSession session(prompter);
        activeSession = &session;

        static_cast<void>(std::signal(SIGINT, HandleSignal));
        static_cast<void>(std::signal(SIGTERM, HandleSignal));

        std::cerr << "connecting to " << options.gateway << " ...\n";

        if (!session.Connect(options))
        {
            std::cerr << "\nconnection failed.\n"
                      << "  log:   " << logPath << "\n"
                      << "  retry: --debug for the openconnect exchange, --trace for raw HTTP\n";
            LOG_ERROR("Connection failed");
            return 1;
        }

        const RouteReport routes =
            RouteReconciler::Reconcile(session.Info(), session.InterfaceName());
        TunnelReport::Print(session.Info(), routes, session.InterfaceName());

        LOG_INFO("tunnel up on " + session.InterfaceName() + " addr=" + session.Info().address +
                 " mtu=" + std::to_string(session.Info().mtu) +
                 (session.Info().IsSplitTunnel() ? " split-tunnel" : " full-tunnel") +
                 (session.Info().dtlsCipher.empty() ? " dtls=none"
                                                    : " dtls=" + session.Info().dtlsCipher));
        LOG_INFO("routes: " + std::to_string(routes.CountOf(RouteState::Applied)) + " applied, " +
                 std::to_string(routes.CountOf(RouteState::Missing)) + " missing, " +
                 std::to_string(routes.CountOf(RouteState::Unexpected)) + " unexpected");

        for (const ReconciledRoute &route : routes.routes)
        {
            if (route.state != RouteState::Applied)
            {
                LOG_INFO(std::string("  route ") + route.StateName() + ": " + route.cidr);
            }
        }

        std::cerr << "tunnel is up -- press Ctrl-C to disconnect\n";

        const int result = session.RunMainLoop();

        activeSession = nullptr;
        std::cerr << "\ndisconnected\n";

        return result == 0 ? 0 : 1;
    }
    catch (const std::exception &ex)
    {
        LOG_ERROR(string("Unhandled exception: ") + ex.what());
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        return 1;
    }
    catch (...)
    {
        LOG_ERROR("Unhandled unknown exception");
        std::cerr << "Unhandled unknown exception\n";
        return 1;
    }
}
