#pragma once

#include <cstdint>
#include <string>

#include <arpa/inet.h>

namespace Ip4
{
    inline int PrefixLengthFromMask(uint32_t mask)
    {
        int bits = 0;
        while (mask != 0)
        {
            bits += static_cast<int>(mask & 1U);
            mask >>= 1U;
        }
        return bits;
    }

    // "255.255.255.0" -> 24. Returns 0 when the mask does not parse.
    inline int PrefixLength(const char *dottedMask)
    {
        struct in_addr addr{};
        if (dottedMask == nullptr || inet_pton(AF_INET, dottedMask, &addr) != 1)
        {
            return 0;
        }
        return PrefixLengthFromMask(ntohl(addr.s_addr));
    }

    inline std::string MaskFromPrefix(int length)
    {
        const uint32_t value = length <= 0 ? 0 : (0xFFFFFFFFU << (32 - length));

        struct in_addr addr{};
        addr.s_addr = htonl(value);

        char buffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &addr, buffer, sizeof(buffer));
        return buffer;
    }

    // address & mask, or empty when either fails to parse.
    inline std::string NetworkAddress(const char *address, const char *mask)
    {
        struct in_addr a{};
        struct in_addr m{};

        if (address == nullptr || mask == nullptr || inet_pton(AF_INET, address, &a) != 1 ||
            inet_pton(AF_INET, mask, &m) != 1)
        {
            return {};
        }

        struct in_addr network{};
        network.s_addr = a.s_addr & m.s_addr;

        char buffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &network, buffer, sizeof(buffer));
        return buffer;
    }
} // namespace Ip4
