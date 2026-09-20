#include "core/redactor.hpp"

#include <QObject>
#include <QTest>

#include <string>

namespace
{
    std::string Redacted();

    class TestRedactor : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void redactsHeaderValuesRegardlessOfCase();
        void leavesOrdinaryHeadersAlone();
        void redactsTheDtlsSessionIdHeader();
        void redactsEveryHeaderOnItsOwnLine();
        void redactsFormValuesUpToTheNextSeparator();
        void matchesWholeKeysOnly();
        void redactsXmlElementsRegardlessOfCase();
        void passesCleanTextThroughUnchanged();
    };

    void TestRedactor::redactsHeaderValuesRegardlessOfCase()
    {
        QCOMPARE(Redactor::Scrub("Cookie: webvpn=0123456789abcdef; path=/"),
                 std::string("Cookie: ") + Redacted());
        QCOMPARE(Redactor::Scrub("SET-COOKIE: webvpn=abc"),
                 std::string("SET-COOKIE: ") + Redacted());
        QCOMPARE(Redactor::Scrub("authorization: Basic dGVzdHVzZXI6aHVudGVyMg=="),
                 std::string("authorization: ") + Redacted());
    }

    void TestRedactor::leavesOrdinaryHeadersAlone()
    {
        const std::string line = "Content-Type: text/xml; charset=utf-8";
        QCOMPARE(Redactor::Scrub(line), line);
    }

    // The session ID is what a DTLS resume is keyed on; a trace log must not
    // hand it out.
    void TestRedactor::redactsTheDtlsSessionIdHeader()
    {
        QCOMPARE(Redactor::Scrub("X-DTLS-Session-ID: 5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A5A"),
                 std::string("X-DTLS-Session-ID: ") + Redacted());
    }

    void TestRedactor::redactsEveryHeaderOnItsOwnLine()
    {
        const std::string block = "HTTP/1.1 200 OK\n"
                                  "Set-Cookie: webvpn=first\n"
                                  "Content-Length: 12\n"
                                  "Set-Cookie: webvpnc=second\n";

        QCOMPARE(Redactor::Scrub(block), std::string("HTTP/1.1 200 OK\n"
                                                     "Set-Cookie: ") +
                                             Redacted() +
                                             "\n"
                                             "Content-Length: 12\n"
                                             "Set-Cookie: " +
                                             Redacted() + "\n");
    }

    // The rest of the form has to stay readable, or the log stops being
    // useful for the very auth problems it exists to debug.
    void TestRedactor::redactsFormValuesUpToTheNextSeparator()
    {
        QCOMPARE(Redactor::Scrub("username=testuser&password=hunter2&otp=123456&group=Work"),
                 std::string("username=testuser&password=") + Redacted() + "&otp=" + Redacted() +
                     "&group=Work");
        QCOMPARE(Redactor::Scrub("POST token=abc123 sent"),
                 std::string("POST token=") + Redacted() + " sent");
    }

    void TestRedactor::matchesWholeKeysOnly()
    {
        for (const char *line : {"password_hint=blue", "mypassword=x", "otp_length=6"})
        {
            QCOMPARE(Redactor::Scrub(line), std::string(line));
        }
    }

    void TestRedactor::redactsXmlElementsRegardlessOfCase()
    {
        QCOMPARE(Redactor::Scrub("<auth><password>hunter2</password></auth>"),
                 std::string("<auth><password>") + Redacted() + "</password></auth>");
        QCOMPARE(Redactor::Scrub("<Password>hunter2</Password>"),
                 std::string("<Password>") + Redacted() + "</Password>");
        QCOMPARE(Redactor::Scrub("<otp>111</otp><otp>222</otp>"),
                 std::string("<otp>") + Redacted() + "</otp><otp>" + Redacted() + "</otp>");
    }

    void TestRedactor::passesCleanTextThroughUnchanged()
    {
        for (const char *line :
             {"", "CSTP connected. DPD 5, Keepalive 32400",
              "DTLS handshake failed: Resource temporarily unavailable, try again.",
              "Corrected tun2 MTU to 1362"})
        {
            QCOMPARE(Redactor::Scrub(line), std::string(line));
        }
    }

    std::string Redacted()
    {
        return std::string(Redactor::Placeholder);
    }
} // namespace

QTEST_APPLESS_MAIN(TestRedactor)

#include "tst_redactor.moc"
