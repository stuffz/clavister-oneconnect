#include "core/dtls_watchdog.hpp"

#include <QObject>
#include <QTest>

namespace
{
    // openconnect's dtls_try_handshake() declares a handshake failed 12 s after
    // it started; the grace must outlast that or a healthy rekey gets reconnected.
    constexpr std::int64_t OpenconnectHandshakeDeadline = 12;
    constexpr std::int64_t OneHour = 3600;

    class TestDtlsWatchdog : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void graceOutlastsOpenconnectsHandshakeDeadline();
        void ignoresATunnelThatNeverHadDtls();
        void staysQuietWhileDtlsIsUp();
        void toleratesAGapShorterThanTheGrace();
        void reconnectsOnceWhenDtlsStaysGone();
        void reconnectsAgainAfterTheWindow();
        void disablesWhenDtlsDropsAgainInsideTheWindow();
        void staysDisabledEvenIfDtlsReappears();
    };

    void TestDtlsWatchdog::graceOutlastsOpenconnectsHandshakeDeadline()
    {
        QVERIFY(DtlsWatchdog::LostGraceSeconds > OpenconnectHandshakeDeadline);
    }

    // Blocked UDP or a TLS-only profile: nothing was lost, so nothing to recover.
    void TestDtlsWatchdog::ignoresATunnelThatNeverHadDtls()
    {
        DtlsWatchdog watchdog;

        for (std::int64_t now = 0; now < 4 * OneHour; now += 2)
        {
            QCOMPARE(watchdog.Observe(false, now), DtlsAction::None);
        }
        QVERIFY(!watchdog.Disabled());
    }

    void TestDtlsWatchdog::staysQuietWhileDtlsIsUp()
    {
        DtlsWatchdog watchdog;

        for (std::int64_t now = 0; now < 10 * OneHour; now += 2)
        {
            QCOMPARE(watchdog.Observe(true, now), DtlsAction::None);
        }
    }

    // A rekey in flight reports no cipher for a moment; that is not a loss.
    void TestDtlsWatchdog::toleratesAGapShorterThanTheGrace()
    {
        DtlsWatchdog watchdog;
        QCOMPARE(watchdog.Observe(true, 0), DtlsAction::None);

        QCOMPARE(watchdog.Observe(false, 100), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 100 + DtlsWatchdog::LostGraceSeconds - 1),
                 DtlsAction::None);
        QCOMPARE(watchdog.Observe(true, 100 + DtlsWatchdog::LostGraceSeconds), DtlsAction::None);

        // The clock restarts at the next gap rather than carrying the old one.
        QCOMPARE(watchdog.Observe(false, 500), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 500 + DtlsWatchdog::LostGraceSeconds - 1),
                 DtlsAction::None);
        QCOMPARE(watchdog.Observe(true, 520), DtlsAction::None);
    }

    void TestDtlsWatchdog::reconnectsOnceWhenDtlsStaysGone()
    {
        DtlsWatchdog watchdog;
        QCOMPARE(watchdog.Observe(true, 0), DtlsAction::None);

        QCOMPARE(watchdog.Observe(false, 100), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 102), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 100 + DtlsWatchdog::LostGraceSeconds),
                 DtlsAction::Reconnect);

        // The reconnect is in progress; asking again every 2 s must not stack up.
        QCOMPARE(watchdog.Observe(false, 117), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 200), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 5000), DtlsAction::None);
        QVERIFY(!watchdog.Disabled());
    }

    // One reconnect per rekey is the expected steady state of a long session.
    void TestDtlsWatchdog::reconnectsAgainAfterTheWindow()
    {
        DtlsWatchdog watchdog;
        QCOMPARE(watchdog.Observe(true, 0), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 100), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 115), DtlsAction::Reconnect);
        QCOMPARE(watchdog.Observe(true, 130), DtlsAction::None);

        const std::int64_t later = 115 + DtlsWatchdog::ReconnectWindowSeconds;
        QCOMPARE(watchdog.Observe(false, later), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, later + DtlsWatchdog::LostGraceSeconds),
                 DtlsAction::Reconnect);
    }

    void TestDtlsWatchdog::disablesWhenDtlsDropsAgainInsideTheWindow()
    {
        DtlsWatchdog watchdog;
        QCOMPARE(watchdog.Observe(true, 0), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 100), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 115), DtlsAction::Reconnect);
        QCOMPARE(watchdog.Observe(true, 130), DtlsAction::None);

        QCOMPARE(watchdog.Observe(false, 300), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 300 + DtlsWatchdog::LostGraceSeconds - 1),
                 DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 300 + DtlsWatchdog::LostGraceSeconds),
                 DtlsAction::Disable);
        QVERIFY(watchdog.Disabled());
    }

    // openconnect_disable_dtls() is final for the session; so is this.
    void TestDtlsWatchdog::staysDisabledEvenIfDtlsReappears()
    {
        DtlsWatchdog watchdog;
        QCOMPARE(watchdog.Observe(true, 0), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 100), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 115), DtlsAction::Reconnect);
        QCOMPARE(watchdog.Observe(true, 130), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 300), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 315), DtlsAction::Disable);

        QCOMPARE(watchdog.Observe(true, 400), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 10000), DtlsAction::None);
        QCOMPARE(watchdog.Observe(false, 20000), DtlsAction::None);
        QVERIFY(watchdog.Disabled());
    }
} // namespace

QTEST_APPLESS_MAIN(TestDtlsWatchdog)

#include "tst_dtls_watchdog.moc"
