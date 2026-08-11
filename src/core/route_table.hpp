#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "core/ip4.hpp"

// Reads the kernel's IPv4 routing table from /proc/net/route. Deliberately not
// netlink: this is a read-only view polled a few times a minute at most.
struct KernelRoute
{
    std::string interfaceName;
    std::string destination;
    std::string gateway; // "0.0.0.0" when link-scoped
    int prefixLength = 0;
    int metric = 0;

    bool IsDefault() const
    {
        return prefixLength == 0 && destination == "0.0.0.0";
    }

    std::string ToCidr() const
    {
        return destination + '/' + std::to_string(prefixLength);
    }
};

class RouteTable
{
public:
    static std::vector<KernelRoute> Read()
    {
        std::vector<KernelRoute> routes;

        std::ifstream file("/proc/net/route");
        if (!file.is_open())
        {
            return routes;
        }

        std::string line;
        std::getline(file, line); // discard the header row

        while (std::getline(file, line))
        {
            if (auto route = ParseLine(line))
            {
                routes.push_back(*route);
            }
        }

        return routes;
    }

    static std::vector<KernelRoute> ReadForInterface(const std::string &interfaceName)
    {
        std::vector<KernelRoute> routes = Read();

        routes.erase(std::remove_if(routes.begin(), routes.end(),
                                    [&interfaceName](const KernelRoute &route)
                                    {
                                        return route.interfaceName != interfaceName;
                                    }),
                     routes.end());

        return routes;
    }

private:
    static std::optional<KernelRoute> ParseLine(const std::string &line)
    {
        std::istringstream stream(line);

        std::string iface;
        std::string destinationHex;
        std::string gatewayHex;
        std::string flags;
        std::string refCount;
        std::string use;
        std::string metric;
        std::string maskHex;

        if (!(stream >> iface >> destinationHex >> gatewayHex >> flags >> refCount >> use >>
              metric >> maskHex))
        {
            return std::nullopt;
        }

        KernelRoute route;
        route.interfaceName = iface;
        route.destination = HexToDottedQuad(destinationHex);
        route.gateway = HexToDottedQuad(gatewayHex);
        route.prefixLength = Ip4::PrefixLengthFromMask(ParseHex(maskHex));

        try
        {
            route.metric = std::stoi(metric);
        }
        catch (const std::exception &)
        {
            route.metric = 0;
        }

        return route;
    }

    static uint32_t ParseHex(const std::string &value)
    {
        return static_cast<uint32_t>(std::stoul(value, nullptr, 16));
    }

    // /proc/net/route stores addresses as little-endian hex, so byte 0 of the
    // stored value is the *first* octet of the dotted quad.
    static std::string HexToDottedQuad(const std::string &value)
    {
        uint32_t raw = 0;
        try
        {
            raw = ParseHex(value);
        }
        catch (const std::exception &)
        {
            return "0.0.0.0";
        }

        std::ostringstream out;
        out << (raw & 0xFF) << '.' << ((raw >> 8) & 0xFF) << '.' << ((raw >> 16) & 0xFF) << '.'
            << ((raw >> 24) & 0xFF);
        return out.str();
    }
};
