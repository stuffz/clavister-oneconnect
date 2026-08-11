#pragma once

#include <string>

#include "core/vpn_session.hpp"
#include "platform/vpnc_script.hpp"

// Deliberately carries no credentials; a remembered password lives in the
// keychain.
struct ConnectionProfile
{
    std::string name;
    std::string gateway;
    std::string protocol = "anyconnect";
    std::string caFile;
    std::string interfaceName;
    std::string vpncScript;
    std::string username;
    std::string userAgent = "AnyConnect-compatible OpenConnect VPN Agent";
    std::string reportedOs = "linux-64";

    bool useDtls = true;
    bool autoConnect = false;

    // The password only; one-time codes are never stored.
    bool rememberPassword = false;

    bool ignorePushedDns = false;

    int reconnectTimeout = 300;
    int reconnectInterval = 10;

    bool IsValid() const
    {
        return !name.empty() && !gateway.empty();
    }

    SessionOptions ToSessionOptions() const
    {
        SessionOptions options;

        options.gateway = gateway;
        options.protocol = protocol;
        options.caFile = caFile;
        options.interfaceName = interfaceName;
        options.vpncScript = vpncScript.empty() ? VpncScript::Find() : vpncScript;
        options.username = username;
        options.rememberPassword = rememberPassword;
        options.ignorePushedDns = ignorePushedDns;
        options.secretAccount = name;
        options.userAgent = userAgent;
        options.reportedOs = reportedOs;
        options.reconnectTimeout = reconnectTimeout;
        options.reconnectInterval = reconnectInterval;
        options.dtlsAttemptPeriod = useDtls ? 60 : 0;

        return options;
    }
};
