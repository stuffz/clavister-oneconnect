#include "core/vpnc_environment.hpp"

#include <QObject>
#include <QTest>

#include <map>
#include <string>

#include <openconnect.h>

namespace
{
    using Environment = std::map<std::string, std::string>;

    // What the Clavister gateway pushes, shapes included: it sends split
    // routes with dotted-quad masks, while other gateways send prefix lengths.
    struct PushedConfig
    {
        oc_split_include includePrefix{"10.0.20.0/24", nullptr};
        oc_split_include includeDotted{"10.0.10.0/255.255.255.0", &includePrefix};
        oc_split_include splitDns{"corp.example", nullptr};
        oc_ip_info ip{};

        PushedConfig()
        {
            ip.addr = "10.0.250.11";
            ip.netmask = "255.255.255.0";
            ip.dns[0] = "10.0.10.5";
            ip.dns[1] = "10.0.10.6";
            ip.nbns[0] = "10.0.10.7";
            ip.domain = "corp.example";
            ip.mtu = 1400;
            ip.split_includes = &includeDotted;
            ip.split_dns = &splitDns;
        }
    };

    class TestVpncEnvironment : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void namesTheDeviceAndGatewayEvenWithoutIpInfo();
        void translatesTheAddressAndMask();
        void indexesSplitRoutesInBothMaskForms();
        void passesResolverSettingsThrough();
        void omitsEveryResolverVariableWhenDnsIsIgnored();
        void skipsEmptyAndAbsentValues();
    };

    void TestVpncEnvironment::namesTheDeviceAndGatewayEvenWithoutIpInfo()
    {
        const Environment env = VpncEnvironment::Build(nullptr, "tun2", "198.51.100.116");

        QCOMPARE(env.size(), static_cast<size_t>(3));
        QCOMPARE(env.at("TUNDEV"), std::string("tun2"));
        QCOMPARE(env.at("VPNGATEWAY"), std::string("198.51.100.116"));
        QVERIFY(!env.at("VPNPID").empty());
    }

    void TestVpncEnvironment::translatesTheAddressAndMask()
    {
        const PushedConfig pushed;
        const Environment env = VpncEnvironment::Build(&pushed.ip, "tun2", "198.51.100.116");

        QCOMPARE(env.at("INTERNAL_IP4_ADDRESS"), std::string("10.0.250.11"));
        QCOMPARE(env.at("INTERNAL_IP4_NETMASK"), std::string("255.255.255.0"));
        QCOMPARE(env.at("INTERNAL_IP4_NETMASKLEN"), std::string("24"));
        QCOMPARE(env.at("INTERNAL_IP4_NETADDR"), std::string("10.0.250.0"));
        QCOMPARE(env.at("INTERNAL_IP4_MTU"), std::string("1400"));
        QCOMPARE(env.count("INTERNAL_IP6_ADDRESS"), static_cast<size_t>(0));
    }

    // vpnc-script wants CISCO_SPLIT_INC_<n>_{ADDR,MASK,MASKLEN} plus the count,
    // and both mask spellings have to end up in both fields.
    void TestVpncEnvironment::indexesSplitRoutesInBothMaskForms()
    {
        const PushedConfig pushed;
        const Environment env = VpncEnvironment::Build(&pushed.ip, "tun2", "198.51.100.116");

        QCOMPARE(env.at("CISCO_SPLIT_INC"), std::string("2"));

        QCOMPARE(env.at("CISCO_SPLIT_INC_0_ADDR"), std::string("10.0.10.0"));
        QCOMPARE(env.at("CISCO_SPLIT_INC_0_MASK"), std::string("255.255.255.0"));
        QCOMPARE(env.at("CISCO_SPLIT_INC_0_MASKLEN"), std::string("24"));

        QCOMPARE(env.at("CISCO_SPLIT_INC_1_ADDR"), std::string("10.0.20.0"));
        QCOMPARE(env.at("CISCO_SPLIT_INC_1_MASK"), std::string("255.255.255.0"));
        QCOMPARE(env.at("CISCO_SPLIT_INC_1_MASKLEN"), std::string("24"));

        QCOMPARE(env.count("CISCO_SPLIT_EXC"), static_cast<size_t>(0));
    }

    void TestVpncEnvironment::passesResolverSettingsThrough()
    {
        const PushedConfig pushed;
        const Environment env = VpncEnvironment::Build(&pushed.ip, "tun2", "198.51.100.116");

        QCOMPARE(env.at("INTERNAL_IP4_DNS"), std::string("10.0.10.5 10.0.10.6"));
        QCOMPARE(env.at("INTERNAL_IP4_NBNS"), std::string("10.0.10.7"));
        QCOMPARE(env.at("CISCO_DEF_DOMAIN"), std::string("corp.example"));
        QCOMPARE(env.at("CISCO_SPLIT_DNS"), std::string("corp.example"));
    }

    // vpnc-script keys its whole resolver path on INTERNAL_IP4_DNS being set,
    // so "leave my DNS alone" has to mean none of these reach it.
    void TestVpncEnvironment::omitsEveryResolverVariableWhenDnsIsIgnored()
    {
        const PushedConfig pushed;
        const Environment env = VpncEnvironment::Build(&pushed.ip, "tun2", "198.51.100.116", true);

        for (const char *key :
             {"INTERNAL_IP4_DNS", "INTERNAL_IP4_NBNS", "CISCO_DEF_DOMAIN", "CISCO_SPLIT_DNS"})
        {
            QVERIFY2(env.count(key) == 0, key);
        }

        // Routes are not DNS and must still be installed.
        QCOMPARE(env.at("CISCO_SPLIT_INC"), std::string("2"));
        QCOMPARE(env.at("INTERNAL_IP4_ADDRESS"), std::string("10.0.250.11"));
    }

    void TestVpncEnvironment::skipsEmptyAndAbsentValues()
    {
        oc_ip_info ip{};
        ip.addr = "10.0.250.11";
        ip.domain = "";

        const Environment env = VpncEnvironment::Build(&ip, "tun0", "");

        QCOMPARE(env.at("INTERNAL_IP4_ADDRESS"), std::string("10.0.250.11"));
        QCOMPARE(env.count("INTERNAL_IP4_NETMASK"), static_cast<size_t>(0));
        QCOMPARE(env.count("INTERNAL_IP4_NETMASKLEN"), static_cast<size_t>(0));
        QCOMPARE(env.count("INTERNAL_IP4_MTU"), static_cast<size_t>(0));
        QCOMPARE(env.count("CISCO_DEF_DOMAIN"), static_cast<size_t>(0));
        QCOMPARE(env.count("CISCO_SPLIT_INC"), static_cast<size_t>(0));
        QCOMPARE(env.at("INTERNAL_IP4_DNS"), std::string());
    }
} // namespace

QTEST_APPLESS_MAIN(TestVpncEnvironment)

#include "tst_vpnc_environment.moc"
