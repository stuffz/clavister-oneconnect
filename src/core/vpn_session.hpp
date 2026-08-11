#pragma once

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <unistd.h>

#include <openconnect.h>

#include "core/auth_prompter.hpp"
#include "core/logger.hpp"
#include "core/redactor.hpp"
#include "core/tunnel_info.hpp"
#include "core/vpnc_environment.hpp"
#include "platform/privileged_client.hpp"
#include "platform/secret_store.hpp"
#include "platform/tun_device.hpp"
#include "platform/vpnc_script.hpp"

struct SessionOptions
{
    // FQDN, not an IP: Clavister's "Host Name" restriction rejects tunnel
    // requests by IP with HTTP 500 even after TLS and authentication succeed.
    std::string gateway;

    std::string protocol = "anyconnect";
    std::string caFile;
    std::string vpncScript;
    std::string interfaceName;

    // Tun setup goes through this helper when not root; see docs/PRIVILEGE.md.
    std::string helperPath;

    std::string username;
    std::string secretAccount;
    bool rememberPassword = false;
    bool ignorePushedDns = false;

    // AnyConnect gateways content-negotiate on User-Agent: anything they do not
    // recognise as a VPN client is served the browser portal page instead of
    // the XML auth form. This default matches what the openconnect CLI sends.
    std::string userAgent = "AnyConnect-compatible OpenConnect VPN Agent";

    // One of: linux, linux-64, win, mac-intel, android, apple-ios.
    std::string reportedOs = "linux-64";

    int reconnectTimeout = 300;
    int reconnectInterval = 10;

    // 0 skips the DTLS attempt entirely.
    int dtlsAttemptPeriod = 60;

    int logLevel = PRG_INFO;
};

class VpnSession
{
public:
    // Invoked on the mainloop thread.
    using InfoHandler = std::function<void(const TunnelInfo &, const TunnelStats &)>;

    explicit VpnSession(AuthPrompter &authPrompter)
        : prompter(authPrompter), vpninfo(nullptr), cancelPipe{-1, -1}
    {
    }

    ~VpnSession()
    {
        Destroy();
    }

    VpnSession(const VpnSession &) = delete;
    VpnSession &operator=(const VpnSession &) = delete;

    // Blocks for the duration of the auth exchange; a GUI must call it off the
    // main thread.
    bool Connect(const SessionOptions &opts)
    {
        options = opts;

        if (options.gateway.empty())
        {
            LOG_ERROR("No gateway configured");
            return false;
        }

        if (options.vpncScript.empty())
        {
            options.vpncScript = VpncScript::Find();
        }

        if (options.vpncScript.empty())
        {
            LOG_ERROR("vpnc-script not found. Looked in:\n" + VpncScript::SearchedPaths() +
                      VpncScript::InstallHint());
            return false;
        }

        openconnect_init_ssl();

        vpninfo = openconnect_vpninfo_new(
            options.userAgent.c_str(), &VpnSession::ValidatePeerCertTrampoline, nullptr,
            &VpnSession::ProcessAuthFormTrampoline, &VpnSession::ProgressTrampoline, this);
        if (vpninfo == nullptr)
        {
            LOG_ERROR("openconnect_vpninfo_new failed");
            return false;
        }

        openconnect_set_loglevel(vpninfo, options.logLevel);
        openconnect_set_stats_handler(vpninfo, &VpnSession::StatsTrampoline);

        if (!options.reportedOs.empty())
        {
            openconnect_set_reported_os(vpninfo, options.reportedOs.c_str());
        }

        if (pipe(cancelPipe) == 0)
        {
            openconnect_set_cancel_fd(vpninfo, cancelPipe[0]);
        }

        if (!options.caFile.empty())
        {
            openconnect_set_cafile(vpninfo, options.caFile.c_str());
        }

        if (openconnect_set_protocol(vpninfo, options.protocol.c_str()) != 0)
        {
            LOG_ERROR("Unsupported protocol: " + options.protocol);
            return false;
        }

        openconnect_set_hostname(vpninfo, options.gateway.c_str());

        if (openconnect_obtain_cookie(vpninfo) != 0)
        {
            LOG_ERROR("Authentication failed or was cancelled");

            // Retrying a stale stored password on every connect is how an
            // account gets locked out.
            if (usedStoredPassword && !options.secretAccount.empty())
            {
                LOG_INFO("Clearing the stored password after a failed login");
                SecretStore::Clear(options.secretAccount);
            }

            pendingPassword.clear();
            return false;
        }

        if (options.rememberPassword && pendingPassword.empty() && !usedStoredPassword)
        {
            LOG_ERROR("Remember password is enabled, but no field in the auth form "
                      "qualified as the login password -- nothing was saved. The field "
                      "names logged above show what the gateway asked for.");
        }

        if (!pendingPassword.empty())
        {
            if (SecretStore::Store(options.secretAccount, pendingPassword))
            {
                LOG_INFO("Password saved to the keychain");
            }
            else
            {
                LOG_ERROR("Could not save the password: " + SecretStore::Describe());
            }
            pendingPassword.clear();
        }

        if (openconnect_make_cstp_connection(vpninfo) != 0)
        {
            LOG_ERROR("Failed to establish the CSTP tunnel");
            return false;
        }

        if (!SetupTun())
        {
            return false;
        }

        if (options.dtlsAttemptPeriod > 0)
        {
            openconnect_setup_dtls(vpninfo, options.dtlsAttemptPeriod);
        }

        {
            const std::lock_guard<std::mutex> lock(infoMutex);
            info = TunnelInfo::FromVpnInfo(vpninfo);
            info.dnsIgnored = options.ignorePushedDns;
        }

        return true;
    }

    int RunMainLoop()
    {
        if (vpninfo == nullptr)
        {
            return -1;
        }

        return openconnect_mainloop(vpninfo, options.reconnectTimeout, options.reconnectInterval);
    }

    // Async-signal-safe: only writes a byte to the pipe openconnect is polling.
    void Cancel()
    {
        SendCommand(OC_CMD_CANCEL);
    }

    // DTLS establishes inside the mainloop, after Connect() has returned, so a
    // connect-time snapshot always reports "no DTLS". This asks the mainloop
    // thread to refresh TunnelInfo rather than racing it from outside.
    void RequestRefresh()
    {
        SendCommand(OC_CMD_STATS);
    }

    void OnInfoRefreshed(InfoHandler handler)
    {
        infoHandler = std::move(handler);
    }

    TunnelInfo Info() const
    {
        const std::lock_guard<std::mutex> lock(infoMutex);
        return info;
    }

    TunnelStats Stats() const
    {
        const std::lock_guard<std::mutex> lock(infoMutex);
        return stats;
    }

    std::string InterfaceName() const
    {
        if (!resolvedInterface.empty())
        {
            return resolvedInterface;
        }
        return options.interfaceName.empty() ? std::string("tun0") : options.interfaceName;
    }

private:
    bool UseHelper() const
    {
        return !options.helperPath.empty() && geteuid() != 0;
    }

    // Not openconnect_setup_tun_device(): that builds the vpnc-script
    // environment internally, so options like ignorePushedDns could not be
    // honoured.
    bool SetupTun()
    {
        const bool viaHelper = UseHelper();
        std::string ifname = options.interfaceName;
        int tunFd = -1;

        if (viaHelper)
        {
            if (!helper.Start(options.helperPath, options.vpncScript))
            {
                LOG_ERROR("Privileged helper failed to start: " + helper.LastError());
                return false;
            }

            tunFd = helper.SetupTun(ifname);
            if (tunFd < 0)
            {
                LOG_ERROR("Helper could not create the tun device: " + helper.LastError());
                return false;
            }
        }
        else
        {
            tunFd = TunDevice::Create(ifname);
            if (tunFd < 0)
            {
                LOG_ERROR("Failed to create the tun device (need root or CAP_NET_ADMIN)");
                return false;
            }
        }

        if (openconnect_setup_tun_fd(vpninfo, tunFd) != 0)
        {
            LOG_ERROR("openconnect rejected the tun descriptor");
            close(tunFd);
            return false;
        }

        resolvedInterface = ifname;
        appliedMtu = 0;

        if (!RunVpncScript("connect"))
        {
            return false;
        }

        LOG_INFO("Tunnel configured on " + ifname +
                 (viaHelper ? " via the privileged helper" : "") +
                 (options.ignorePushedDns ? " (system DNS left untouched)" : ""));
        return true;
    }

    // openconnect does not run vpnc-script when handed a ready-made tun fd, so
    // the environment it would have built is assembled here instead.
    bool RunVpncScript(const std::string &reason)
    {
        const struct oc_ip_info *ip = nullptr;
        openconnect_get_ip_info(vpninfo, &ip, nullptr, nullptr);

        auto environment = VpncEnvironment::Build(ip, resolvedInterface, GatewayAddress(ip),
                                                  options.ignorePushedDns);

        if (UseHelper())
        {
            if (!helper.RunScript(reason, environment))
            {
                LOG_ERROR("vpnc-script failed via the helper: " + helper.LastError());
                return false;
            }
            return true;
        }

        environment["reason"] = reason;

        std::string error;
        if (!TunDevice::RunScript(options.vpncScript, environment, error))
        {
            LOG_ERROR("vpnc-script failed: " + error);
            return false;
        }
        return true;
    }

    static std::string GatewayAddress(const struct oc_ip_info *ip)
    {
        return (ip != nullptr && ip->gateway_addr != nullptr) ? ip->gateway_addr : std::string();
    }

    void SendCommand(char command)
    {
        if (cancelPipe[1] >= 0)
        {
            const ssize_t written = write(cancelPipe[1], &command, 1);
            static_cast<void>(written);
        }
    }

    void Destroy()
    {
        // The routes and DNS vpnc-script installed outlive the process unless
        // it runs again with reason=disconnect.
        if (vpninfo != nullptr && !resolvedInterface.empty() &&
            (!UseHelper() || helper.IsRunning()))
        {
            RunVpncScript("disconnect");
            resolvedInterface.clear();
        }

        helper.Stop();

        if (vpninfo != nullptr)
        {
            openconnect_vpninfo_free(vpninfo);
            vpninfo = nullptr;
        }

        for (int &fd : cancelPipe)
        {
            if (fd >= 0)
            {
                close(fd);
                fd = -1;
            }
        }
    }

    static int ValidatePeerCertTrampoline(void *privdata, const char *reason)
    {
        return static_cast<VpnSession *>(privdata)->ValidatePeerCert(reason);
    }

    static int ProcessAuthFormTrampoline(void *privdata, struct oc_auth_form *form)
    {
        return static_cast<VpnSession *>(privdata)->ProcessAuthForm(form);
    }

    static void StatsTrampoline(void *privdata, const struct oc_stats *stats)
    {
        static_cast<VpnSession *>(privdata)->RefreshFromMainloop(stats);
    }

    // Runs on the mainloop thread -- the only thread that may touch vpninfo
    // once the mainloop is pumping.
    void RefreshFromMainloop(const struct oc_stats *raw)
    {
        TunnelInfo refreshed = TunnelInfo::FromVpnInfo(vpninfo);
        refreshed.dnsIgnored = options.ignorePushedDns;

        TunnelStats counters;
        if (raw != nullptr)
        {
            counters.txPackets = raw->tx_pkts;
            counters.txBytes = raw->tx_bytes;
            counters.rxPackets = raw->rx_pkts;
            counters.rxBytes = raw->rx_bytes;
        }

        {
            const std::lock_guard<std::mutex> lock(infoMutex);
            info = refreshed;
            stats = counters;
        }

        // vpnc-script installed the gateway's advertised MTU; openconnect probes
        // the real path MTU only after the tunnel is up. Left uncorrected, TCP
        // sessions connect and then stall.
        if (refreshed.mtu > 0 && refreshed.mtu != appliedMtu && !resolvedInterface.empty())
        {
            const bool ok = UseHelper() ? helper.SetMtu(resolvedInterface, refreshed.mtu)
                                        : TunDevice::SetMtu(resolvedInterface, refreshed.mtu);
            if (ok)
            {
                LOG_INFO("Corrected " + resolvedInterface + " MTU to " +
                         std::to_string(refreshed.mtu));
                appliedMtu = refreshed.mtu;
            }
        }

        if (infoHandler)
        {
            infoHandler(refreshed, counters);
        }
    }

    // openconnect_progress_vfn is declared variadic and printf-attributed, so
    // this cannot be a parameter pack however much clang-tidy would prefer one.
    // NOLINTNEXTLINE(cert-dcl50-cpp)
    static void ProgressTrampoline(void *privdata, int level, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);

        std::vector<char> buffer(1024);
        const int needed = vsnprintf(buffer.data(), buffer.size(), fmt, args);
        va_end(args);

        if (needed < 0)
        {
            return;
        }

        std::string message(buffer.data());
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        {
            message.pop_back();
        }

        static_cast<VpnSession *>(privdata)->Progress(level, message);
    }

    // Never prompts to accept an untrusted certificate: the CA is expected in
    // the system trust store, so a failure here is not something to click
    // through.
    int ValidatePeerCert(const char *reason)
    {
        LOG_ERROR(std::string("Server certificate rejected: ") +
                  (reason != nullptr ? reason : "unknown reason"));
        return -1;
    }

    void Progress(int level, const std::string &rawMessage)
    {
        if (rawMessage.empty())
        {
            return;
        }

        // At trace level this callback carries raw HTTP, so this is the only
        // barrier between a debug session and a credential on disk.
        const std::string message = Redactor::Scrub(rawMessage);

        switch (level)
        {
        case PRG_ERR:
            LOG_ERROR(message);
            break;
        case PRG_INFO:
            LOG_INFO(message);
            break;
        default:
            LOG_DEBUG(message);
            break;
        }
    }

    int ProcessAuthForm(struct oc_auth_form *form)
    {
        if (form == nullptr)
        {
            return OC_FORM_RESULT_ERR;
        }

        ++formsSeen;

        // A field the storability rule rejects is otherwise invisible.
        if (options.rememberPassword || Logger::Instance().DebugMode())
        {
            LogFormFields(form);
        }

        prompter.ShowFormMessage(Str(form->banner), Str(form->message), Str(form->error));

        for (struct oc_form_opt *opt = form->opts; opt != nullptr; opt = opt->next)
        {
            if ((opt->flags & OC_FORM_OPT_IGNORE) != 0)
            {
                continue;
            }

            const std::string label = opt->label != nullptr ? Str(opt->label) : Str(opt->name);

            switch (opt->type)
            {
            case OC_FORM_OPT_TEXT:
            case OC_FORM_OPT_PASSWORD:
            case OC_FORM_OPT_TOKEN:
                if (!FillTextOption(opt, label))
                {
                    return OC_FORM_RESULT_CANCELLED;
                }
                break;

            case OC_FORM_OPT_SELECT:
                if (!FillChoiceOption(reinterpret_cast<struct oc_form_opt_select *>(opt), label))
                {
                    return OC_FORM_RESULT_CANCELLED;
                }
                break;

            case OC_FORM_OPT_SSO_TOKEN:
            case OC_FORM_OPT_SSO_USER:
                if (!prompter.SupportsSingleSignOn())
                {
                    LOG_ERROR("This gateway requires SAML/SSO login, which is not implemented yet");
                    return OC_FORM_RESULT_ERR;
                }
                break;

            case OC_FORM_OPT_HIDDEN:
            default:
                break;
            }
        }

        return OC_FORM_RESULT_OK;
    }

    // Field names and types only -- never values.
    void LogFormFields(const struct oc_auth_form *form) const
    {
        for (const struct oc_form_opt *opt = form->opts; opt != nullptr; opt = opt->next)
        {
            LOG_INFO("auth form " + std::to_string(formsSeen) + " field: name='" + Str(opt->name) +
                     "' label='" + Str(opt->label) + "' type=" + std::to_string(opt->type) +
                     " flags=" + std::to_string(opt->flags));
        }
    }

    bool FillTextOption(struct oc_form_opt *opt, const std::string &label)
    {
        const bool secret = opt->type != OC_FORM_OPT_TEXT;

        // Evaluated once: IsStorablePassword() has a side effect.
        const bool storable = IsStorablePassword(opt);

        if (storable)
        {
            const std::string saved = SecretStore::Lookup(options.secretAccount);
            if (!saved.empty())
            {
                LOG_INFO("Using password from the keychain");
                openconnect_set_option_value(opt, saved.c_str());
                usedStoredPassword = true;
                return true;
            }
        }

        const auto value = prompter.AskText(label, secret, SuggestionFor(opt, secret));
        if (!value)
        {
            return false;
        }

        openconnect_set_option_value(opt, value->c_str());

        // Stored only after authentication succeeds; saving a wrong password
        // guarantees a lockout later.
        if (storable && options.rememberPassword)
        {
            pendingPassword = *value;
        }

        return true;
    }

    bool FillChoiceOption(struct oc_form_opt_select *select, const std::string &label)
    {
        std::vector<AuthChoice> choices;
        choices.reserve(static_cast<size_t>(select->nr_choices));

        for (int index = 0; index < select->nr_choices; ++index)
        {
            const struct oc_choice *choice = select->choices[index];
            if (choice == nullptr)
            {
                continue;
            }
            choices.push_back({Str(choice->name), Str(choice->label)});
        }

        const auto value = prompter.AskChoice(label, choices);
        if (!value)
        {
            return false;
        }

        openconnect_set_option_value(&select->form, value->c_str());
        return true;
    }

    // The first password-type field of the exchange is the login password: a
    // RADIUS challenge arrives as a later form, so ordering already excludes
    // the one-time code. Names only exclude obvious second factors, for the
    // rare gateway that puts both in one form.
    //
    // Not a pure predicate: it marks the first password field as seen, so call
    // it once per field and reuse the result.
    bool IsStorablePassword(const struct oc_form_opt *opt)
    {
        if (options.secretAccount.empty() || opt->type != OC_FORM_OPT_PASSWORD)
        {
            return false;
        }

        if (storablePasswordSeen)
        {
            return false;
        }

        if (LooksLikeSecondFactor(Str(opt->name)) || LooksLikeSecondFactor(Str(opt->label)))
        {
            return false;
        }

        storablePasswordSeen = true;
        return true;
    }

    static bool LooksLikeSecondFactor(std::string text)
    {
        for (char &c : text)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        for (const char *needle :
             {"otp", "token", "challenge", "secondary", "second", "one-time", "onetime",
              "verification", "authenticator", "passcode", "pin", "code", "response"})
        {
            if (text.find(needle) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    // Matches the option's name, not its label: labels are localised by the
    // gateway.
    std::string SuggestionFor(const struct oc_form_opt *opt, bool secret) const
    {
        if (secret || options.username.empty() || opt->name == nullptr)
        {
            return {};
        }

        const std::string name = Str(opt->name);
        return (name == "username" || name == "user" || name == "uname") ? options.username
                                                                         : std::string();
    }

    static std::string Str(const char *value)
    {
        return value != nullptr ? std::string(value) : std::string();
    }

    AuthPrompter &prompter;
    SessionOptions options;
    struct openconnect_info *vpninfo;
    int cancelPipe[2];

    // Written on the mainloop thread, read from anywhere.
    mutable std::mutex infoMutex;
    TunnelInfo info;
    TunnelStats stats;
    InfoHandler infoHandler;

    PrivilegedClient helper;
    std::string resolvedInterface;
    int appliedMtu = 0;
    std::string pendingPassword;
    bool usedStoredPassword = false;
    int formsSeen = 0;
    bool storablePasswordSeen = false;
};
