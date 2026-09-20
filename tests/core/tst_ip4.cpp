#include "core/ip4.hpp"

#include <QObject>
#include <QTest>

#include <string>

namespace
{
    class TestIp4 : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void countsPrefixBitsInAMask();
        void parsesDottedMasks();
        void returnsZeroForAMaskThatDoesNotParse();
        void buildsMasksFromPrefixLengths();
        void masksAnAddressToItsNetwork();
        void returnsEmptyWhenTheNetworkInputDoesNotParse();
    };

    void TestIp4::countsPrefixBitsInAMask()
    {
        QCOMPARE(Ip4::PrefixLengthFromMask(0xFFFFFF00U), 24);
        QCOMPARE(Ip4::PrefixLengthFromMask(0xFFFFFFFFU), 32);
        QCOMPARE(Ip4::PrefixLengthFromMask(0U), 0);
        QCOMPARE(Ip4::PrefixLengthFromMask(0xFFFE0000U), 15);
    }

    void TestIp4::parsesDottedMasks()
    {
        QCOMPARE(Ip4::PrefixLength("255.255.255.0"), 24);
        QCOMPARE(Ip4::PrefixLength("255.255.255.255"), 32);
        QCOMPARE(Ip4::PrefixLength("255.0.0.0"), 8);
        QCOMPARE(Ip4::PrefixLength("0.0.0.0"), 0);
    }

    void TestIp4::returnsZeroForAMaskThatDoesNotParse()
    {
        QCOMPARE(Ip4::PrefixLength(nullptr), 0);
        QCOMPARE(Ip4::PrefixLength(""), 0);
        QCOMPARE(Ip4::PrefixLength("24"), 0);
        QCOMPARE(Ip4::PrefixLength("255.255.255"), 0);
    }

    void TestIp4::buildsMasksFromPrefixLengths()
    {
        QCOMPARE(Ip4::MaskFromPrefix(24), std::string("255.255.255.0"));
        QCOMPARE(Ip4::MaskFromPrefix(32), std::string("255.255.255.255"));
        QCOMPARE(Ip4::MaskFromPrefix(0), std::string("0.0.0.0"));
        QCOMPARE(Ip4::MaskFromPrefix(-1), std::string("0.0.0.0"));
        QCOMPARE(Ip4::MaskFromPrefix(9), std::string("255.128.0.0"));
    }

    void TestIp4::masksAnAddressToItsNetwork()
    {
        QCOMPARE(Ip4::NetworkAddress("10.0.250.11", "255.255.255.0"), std::string("10.0.250.0"));
        QCOMPARE(Ip4::NetworkAddress("10.0.250.11", "255.255.255.255"),
                 std::string("10.0.250.11"));
        QCOMPARE(Ip4::NetworkAddress("192.168.1.91", "255.255.0.0"), std::string("192.168.0.0"));
    }

    void TestIp4::returnsEmptyWhenTheNetworkInputDoesNotParse()
    {
        QVERIFY(Ip4::NetworkAddress(nullptr, "255.255.255.0").empty());
        QVERIFY(Ip4::NetworkAddress("10.0.250.11", nullptr).empty());
        QVERIFY(Ip4::NetworkAddress("not-an-address", "255.255.255.0").empty());
        QVERIFY(Ip4::NetworkAddress("10.0.250.11", "/24").empty());
    }
} // namespace

QTEST_APPLESS_MAIN(TestIp4)

#include "tst_ip4.moc"
