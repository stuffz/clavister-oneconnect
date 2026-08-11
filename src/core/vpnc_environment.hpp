#pragma once

#include <map>
#include <string>

#include <unistd.h>

#include <openconnect.h>

#include "core/ip4.hpp"

// Builds the environment vpnc-script expects. The variable names are
// vpnc-script's public interface; keep them in step with the helper's
// allowlist, which refuses anything it does not recognise.
class VpncEnvironment
{
public:
    static std::map<std::string, std::string> Build(const struct oc_ip_info *ip,
                                                    const std::string &interfaceName,
                                                    const std::string &gatewayAddress,
                                                    bool ignoreDns = false)
    {
        std::map<std::string, std::string> env;

        env["TUNDEV"] = interfaceName;
        env["VPNGATEWAY"] = gatewayAddress;
        env["VPNPID"] = std::to_string(getpid());

        if (ip == nullptr)
        {
            return env;
        }

        Put(env, "INTERNAL_IP4_ADDRESS", ip->addr);
        Put(env, "INTERNAL_IP4_NETMASK", ip->netmask);
        Put(env, "INTERNAL_IP6_ADDRESS", ip->addr6);
        Put(env, "INTERNAL_IP6_NETMASK", ip->netmask6);

        if (ip->mtu > 0)
        {
            env["INTERNAL_IP4_MTU"] = std::to_string(ip->mtu);
        }

        if (ip->netmask != nullptr)
        {
            env["INTERNAL_IP4_NETMASKLEN"] = std::to_string(Ip4::PrefixLength(ip->netmask));
            const std::string network = Ip4::NetworkAddress(ip->addr, ip->netmask);
            if (!network.empty())
            {
                env["INTERNAL_IP4_NETADDR"] = network;
            }
        }

        // vpnc-script guards its entire resolver path on INTERNAL_IP4_DNS
        // being non-empty, so omitting the DNS variables is all it takes to
        // leave the system resolver untouched. dns[] carries v4 and v6
        // resolvers together; vpnc-script handles the mix.
        if (!ignoreDns)
        {
            Put(env, "CISCO_DEF_DOMAIN", ip->domain);
            env["INTERNAL_IP4_DNS"] = Join(ip->dns, 3);
            env["INTERNAL_IP4_NBNS"] = Join(ip->nbns, 3);
        }

        AddSplitList(env, "CISCO_SPLIT_INC", ip->split_includes);
        AddSplitList(env, "CISCO_SPLIT_EXC", ip->split_excludes);

        std::string splitDns;
        for (const struct oc_split_include *entry = ip->split_dns; entry != nullptr;
             entry = entry->next)
        {
            if (entry->route != nullptr)
            {
                splitDns += (splitDns.empty() ? "" : ",") + std::string(entry->route);
            }
        }
        if (!splitDns.empty() && !ignoreDns)
        {
            env["CISCO_SPLIT_DNS"] = splitDns;
        }

        return env;
    }

private:
    static void Put(std::map<std::string, std::string> &env, const char *key, const char *value)
    {
        if (value != nullptr && *value != '\0')
        {
            env[key] = value;
        }
    }

    template <size_t N>
    static std::string Join(const char *const (&values)[N], size_t count)
    {
        std::string out;
        for (size_t i = 0; i < count && i < N; ++i)
        {
            if (values[i] != nullptr && *values[i] != '\0')
            {
                out += (out.empty() ? "" : " ") + std::string(values[i]);
            }
        }
        return out;
    }

    // vpnc-script wants split routes indexed:
    //   CISCO_SPLIT_INC=2
    //   CISCO_SPLIT_INC_0_ADDR=10.0.10.0
    //   CISCO_SPLIT_INC_0_MASK=255.255.255.0
    //   CISCO_SPLIT_INC_0_MASKLEN=24
    static void AddSplitList(std::map<std::string, std::string> &env, const std::string &prefix,
                             const struct oc_split_include *list)
    {
        size_t index = 0;

        for (const struct oc_split_include *entry = list; entry != nullptr; entry = entry->next)
        {
            if (entry->route == nullptr)
            {
                continue;
            }

            const std::string route = entry->route;
            const size_t slash = route.find('/');
            if (slash == std::string::npos)
            {
                continue;
            }

            const std::string network = route.substr(0, slash);
            const std::string suffix = route.substr(slash + 1);

            // The gateway sends either form; Clavister sends dotted-quad masks.
            const bool dotted = suffix.find('.') != std::string::npos;
            const std::string mask = dotted ? suffix : Ip4::MaskFromPrefix(std::stoi(suffix));
            const int length = dotted ? Ip4::PrefixLength(suffix.c_str()) : std::stoi(suffix);

            const std::string base = prefix + "_" + std::to_string(index);
            env[base + "_ADDR"] = network;
            env[base + "_MASK"] = mask;
            env[base + "_MASKLEN"] = std::to_string(length);

            ++index;
        }

        if (index > 0)
        {
            env[prefix] = std::to_string(index);
        }
    }
};
