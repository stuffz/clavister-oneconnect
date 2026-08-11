#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/ip4.hpp"
#include "core/route_table.hpp"
#include "core/tunnel_info.hpp"

// Missing means vpnc-script failed to install a pushed route. Unexpected is
// often legitimate (the interface's own /32, the gateway host route) but is
// reported rather than filtered: silently hiding routes would defeat the
// comparison.
enum class RouteState
{
    Applied,   // pushed by the gateway and present in the kernel
    Missing,   // pushed by the gateway but absent from the kernel
    Unexpected // present in the kernel but never pushed
};

struct ReconciledRoute
{
    std::string cidr;
    RouteState state = RouteState::Applied;
    int metric = 0;
    std::string gateway;

    const char *StateName() const
    {
        switch (state)
        {
        case RouteState::Applied:
            return "applied";
        case RouteState::Missing:
            return "missing";
        case RouteState::Unexpected:
            return "unexpected";
        }
        return "unknown";
    }
};

struct RouteReport
{
    std::vector<ReconciledRoute> routes;
    bool fullTunnel = false;

    size_t CountOf(RouteState state) const
    {
        return static_cast<size_t>(std::count_if(routes.begin(), routes.end(),
                                                 [state](const ReconciledRoute &route)
                                                 {
                                                     return route.state == state;
                                                 }));
    }

    bool IsClean() const
    {
        return CountOf(RouteState::Missing) == 0;
    }
};

class RouteReconciler
{
public:
    static RouteReport Reconcile(const TunnelInfo &info, const std::string &interfaceName)
    {
        const std::vector<KernelRoute> kernelRoutes = RouteTable::ReadForInterface(interfaceName);

        RouteReport report;
        std::vector<std::string> matched;

        for (const std::string &pushed : info.splitIncludes)
        {
            const std::string normalised = Normalise(pushed);

            const auto found = std::find_if(kernelRoutes.begin(), kernelRoutes.end(),
                                            [&normalised](const KernelRoute &route)
                                            {
                                                return route.ToCidr() == normalised;
                                            });

            ReconciledRoute entry;
            entry.cidr = normalised;

            if (found != kernelRoutes.end())
            {
                entry.state = RouteState::Applied;
                entry.metric = found->metric;
                entry.gateway = found->gateway;
                matched.push_back(normalised);
            }
            else
            {
                entry.state = RouteState::Missing;
            }

            report.routes.push_back(entry);
        }

        for (const KernelRoute &route : kernelRoutes)
        {
            const std::string cidr = route.ToCidr();

            if (std::find(matched.begin(), matched.end(), cidr) != matched.end())
            {
                continue;
            }

            if (route.IsDefault())
            {
                report.fullTunnel = true;
            }

            ReconciledRoute entry;
            entry.cidr = cidr;
            entry.state = RouteState::Unexpected;
            entry.metric = route.metric;
            entry.gateway = route.gateway;
            report.routes.push_back(entry);
        }

        return report;
    }

private:
    // Split includes arrive as "10.0.10.0/24" or "10.0.10.0/255.255.255.0"
    // depending on the gateway; KernelRoute::ToCidr() is prefix-length form.
    static std::string Normalise(const std::string &route)
    {
        const size_t slash = route.find('/');
        if (slash == std::string::npos)
        {
            return route + "/32";
        }

        const std::string network = route.substr(0, slash);
        const std::string suffix = route.substr(slash + 1);

        if (suffix.find('.') == std::string::npos)
        {
            return route; // already prefix-length form
        }

        return network + '/' + std::to_string(Ip4::PrefixLength(suffix.c_str()));
    }
};
