#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "core/route_reconciler.hpp"
#include "core/tunnel_info.hpp"

// Renders the tunnel state to stdout.
class TunnelReport
{
public:
    static void Print(const TunnelInfo &info, const RouteReport &routes,
                      const std::string &interfaceName)
    {
        std::cout << "\n";
        Heading("tunnel");

        Field("interface", interfaceName);
        Field("address", info.address + (info.netmask.empty() ? "" : " mask " + info.netmask));

        if (!info.address6.empty())
        {
            Field("address6", info.address6);
        }

        Field("mtu", std::to_string(info.mtu));
        Field("gateway", info.gatewayAddress);

        if (!info.cstpCipher.empty())
        {
            Field("cstp", info.cstpCipher + Suffix(info.cstpCompression));
        }

        Field("dtls", info.dtlsDisabled         ? "disabled after repeated failure (TLS only)"
                      : info.dtlsCipher.empty() ? "not established (TLS only)"
                                                : info.dtlsCipher + Suffix(info.dtlsCompression));

        std::cout << "\n";
        Heading("dns");

        Field("servers", info.dnsServers.empty() ? "none pushed" : Join(info.dnsServers));
        Field("domain", info.domain.empty() ? "none pushed" : info.domain);

        if (!info.splitDns.empty())
        {
            Field("split-dns", Join(info.splitDns));
        }

        std::cout << "\n";
        Heading("routes");
        PrintRoutes(routes);

        std::cout << std::endl;
    }

private:
    static void PrintRoutes(const RouteReport &routes)
    {
        if (routes.routes.empty())
        {
            std::cout << "  no routes on the tunnel interface\n";
            return;
        }

        if (routes.fullTunnel)
        {
            std::cout << "  full tunnel -- a default route points at this interface\n\n";
        }
        else
        {
            std::cout << "  split tunnel -- the default route is untouched\n\n";
        }

        size_t width = 0;
        for (const ReconciledRoute &route : routes.routes)
        {
            width = std::max(width, route.cidr.size());
        }

        for (const ReconciledRoute &route : routes.routes)
        {
            std::cout << "  " << Pad(route.cidr, width) << "  " << Pad(route.StateName(), 10);

            if (route.state != RouteState::Missing && route.gateway != "0.0.0.0")
            {
                std::cout << "via " << route.gateway;
            }

            std::cout << "\n";
        }

        std::cout << "\n  " << routes.CountOf(RouteState::Applied) << " applied, "
                  << routes.CountOf(RouteState::Missing) << " missing, "
                  << routes.CountOf(RouteState::Unexpected) << " unexpected\n";

        if (!routes.IsClean())
        {
            std::cout << "\n  Routes the gateway pushed are missing from the kernel. That\n"
                      << "  usually means vpnc-script could not install them -- check for a\n"
                      << "  conflicting route or a malformed netmask.\n";
        }
    }

    static void Heading(const std::string &title)
    {
        std::cout << title << "\n";
        std::cout << std::string(title.size(), '-') << "\n";
    }

    static void Field(const std::string &name, const std::string &value)
    {
        if (value.empty())
        {
            return;
        }
        std::cout << "  " << Pad(name, 10) << "  " << value << "\n";
    }

    static std::string Pad(const std::string &value, size_t width)
    {
        if (value.size() >= width)
        {
            return value;
        }
        return value + std::string(width - value.size(), ' ');
    }

    static std::string Suffix(const std::string &compression)
    {
        return compression.empty() ? "" : " (" + compression + ")";
    }

    static std::string Join(const std::vector<std::string> &values)
    {
        std::string out;
        for (const std::string &value : values)
        {
            out += (out.empty() ? "" : ", ") + value;
        }
        return out;
    }
};
