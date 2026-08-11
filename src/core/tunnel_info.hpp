#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openconnect.h>

struct TunnelStats
{
    uint64_t txPackets = 0;
    uint64_t txBytes = 0;
    uint64_t rxPackets = 0;
    uint64_t rxBytes = 0;
};

// Everything in `struct oc_ip_info` is owned by libopenconnect and only valid
// until the next reconnect, so this snapshot copies what it needs into owned
// storage.
struct TunnelInfo
{
    std::string address;
    std::string netmask;
    std::string address6;
    std::string netmask6;
    std::string domain;
    std::string gatewayAddress;
    int mtu = 0;

    // The gateway still *sends* DNS servers when they are ignored; the UI
    // needs to distinguish pushed from in-use.
    bool dnsIgnored = false;

    std::vector<std::string> dnsServers;
    std::vector<std::string> nbnsServers;

    // Empty splitIncludes with a tunnel up means the gateway asked for a full
    // tunnel, not that it sent nothing.
    std::vector<std::string> splitIncludes;
    std::vector<std::string> splitExcludes;
    std::vector<std::string> splitDns;

    std::string cstpCipher;
    std::string dtlsCipher;
    std::string cstpCompression;
    std::string dtlsCompression;

    bool IsSplitTunnel() const
    {
        return !splitIncludes.empty();
    }

    static TunnelInfo FromVpnInfo(struct openconnect_info *vpninfo)
    {
        TunnelInfo out;

        const struct oc_ip_info *ip = nullptr;
        if (openconnect_get_ip_info(vpninfo, &ip, nullptr, nullptr) != 0 || ip == nullptr)
        {
            return out;
        }

        out.address = Str(ip->addr);
        out.netmask = Str(ip->netmask);
        out.address6 = Str(ip->addr6);
        out.netmask6 = Str(ip->netmask6);
        out.domain = Str(ip->domain);
        out.gatewayAddress = Str(ip->gateway_addr);
        out.mtu = ip->mtu;

        for (const char *dns : ip->dns)
        {
            if (dns != nullptr)
            {
                out.dnsServers.emplace_back(dns);
            }
        }

        for (const char *nbns : ip->nbns)
        {
            if (nbns != nullptr)
            {
                out.nbnsServers.emplace_back(nbns);
            }
        }

        out.splitIncludes = Collect(ip->split_includes);
        out.splitExcludes = Collect(ip->split_excludes);
        out.splitDns = Collect(ip->split_dns);

        out.cstpCipher = Str(openconnect_get_cstp_cipher(vpninfo));
        out.dtlsCipher = Str(openconnect_get_dtls_cipher(vpninfo));
        out.cstpCompression = Str(openconnect_get_cstp_compression(vpninfo));
        out.dtlsCompression = Str(openconnect_get_dtls_compression(vpninfo));

        return out;
    }

private:
    static std::string Str(const char *value)
    {
        return value != nullptr ? std::string(value) : std::string();
    }

    static std::vector<std::string> Collect(const struct oc_split_include *list)
    {
        std::vector<std::string> out;
        for (const struct oc_split_include *entry = list; entry != nullptr; entry = entry->next)
        {
            if (entry->route != nullptr)
            {
                out.emplace_back(entry->route);
            }
        }
        return out;
    }
};
