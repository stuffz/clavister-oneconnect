#include "core/stored_password.hpp"

#include <QObject>
#include <QTest>

namespace
{
    class TestStoredPassword : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void keepsAPasswordThatWasNeverUsed();
        void forgetsAPasswordRejectedAtItsOwnForm();
        void keepsAPasswordWhenALaterFormFails();
        void forgetsAPasswordTheGatewayAsksForAgain();
        void aFormDoesNotJudgeItsOwnPassword();
    };

    void TestStoredPassword::keepsAPasswordThatWasNeverUsed()
    {
        StoredPasswordTracker tracker;
        tracker.OnForm(FormFields::NoPassword);
        tracker.OnForm(FormFields::AsksForPassword);

        QVERIFY(!tracker.Used());
        QVERIFY(!tracker.ShouldForget());
    }

    // Clavister answers a wrong password with a 401 before any OTP form.
    void TestStoredPassword::forgetsAPasswordRejectedAtItsOwnForm()
    {
        StoredPasswordTracker tracker;
        tracker.OnForm(FormFields::NoPassword);
        tracker.OnForm(FormFields::AsksForPassword);
        tracker.OnStoredPasswordUsed();

        QVERIFY(tracker.Used());
        QVERIFY(tracker.ShouldForget());
    }

    // The OTP form only arrives once the password passed, so a failure after
    // it is the one-time code.
    void TestStoredPassword::keepsAPasswordWhenALaterFormFails()
    {
        StoredPasswordTracker tracker;
        tracker.OnForm(FormFields::NoPassword);
        tracker.OnForm(FormFields::AsksForPassword);
        tracker.OnStoredPasswordUsed();
        tracker.OnForm(FormFields::NoPassword);

        QVERIFY(tracker.Used());
        QVERIFY(!tracker.ShouldForget());
    }

    void TestStoredPassword::forgetsAPasswordTheGatewayAsksForAgain()
    {
        StoredPasswordTracker tracker;
        tracker.OnForm(FormFields::AsksForPassword);
        tracker.OnStoredPasswordUsed();
        tracker.OnForm(FormFields::AsksForPassword);
        tracker.OnForm(FormFields::NoPassword);

        QVERIFY(tracker.ShouldForget());
    }

    void TestStoredPassword::aFormDoesNotJudgeItsOwnPassword()
    {
        StoredPasswordTracker tracker;
        tracker.OnForm(FormFields::AsksForPassword);
        tracker.OnStoredPasswordUsed();

        QVERIFY(tracker.ShouldForget());
    }
} // namespace

QTEST_APPLESS_MAIN(TestStoredPassword)

#include "tst_stored_password.moc"
