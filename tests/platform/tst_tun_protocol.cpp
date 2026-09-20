#include "platform/tun_protocol.hpp"

#include <QObject>
#include <QTest>

#include <cerrno>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    // A connected pair standing in for the GUI and the helper; the protocol
    // itself is the same whichever end is root.
    class SocketPair
    {
    public:
        SocketPair()
        {
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
            {
                throw std::system_error(errno, std::generic_category(), "socketpair");
            }
        }

        SocketPair(const SocketPair &) = delete;
        SocketPair &operator=(const SocketPair &) = delete;

        ~SocketPair()
        {
            CloseClient();
            CloseHelper();
        }

        int Client() const
        {
            return fds[0];
        }

        int Helper() const
        {
            return fds[1];
        }

        void CloseClient()
        {
            Close(fds[0]);
        }

        void CloseHelper()
        {
            Close(fds[1]);
        }

    private:
        static void Close(int &fd)
        {
            if (fd >= 0)
            {
                close(fd);
                fd = -1;
            }
        }

        int fds[2] = {-1, -1};
    };

    ino_t InodeOf(int fd)
    {
        struct stat info{};
        return fstat(fd, &info) == 0 ? info.st_ino : 0;
    }

    class TestTunProtocol : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void roundTripsALine();
        void readsOneLineAtATimeAndKeepsTheRest();
        void failsWhenTheLineExceedsTheLimit();
        void failsWhenThePeerHangsUp();
        void passesADescriptorWithItsMessage();
        void reportsNoDescriptorWhenNoneWasAttached();
    };

    void TestTunProtocol::roundTripsALine()
    {
        SocketPair pair;
        QVERIFY(TunProtocol::SendLine(pair.Client(), "SETUP tun0"));

        std::string line;
        QVERIFY(TunProtocol::ReadLine(pair.Helper(), line));
        QCOMPARE(line, std::string("SETUP tun0"));
    }

    // SCRIPT is followed by an environment block, so a reader that swallowed
    // past the newline would eat the next command.
    void TestTunProtocol::readsOneLineAtATimeAndKeepsTheRest()
    {
        SocketPair pair;
        QVERIFY(TunProtocol::SendLine(pair.Client(), "SCRIPT connect"));
        QVERIFY(TunProtocol::SendLine(pair.Client(), "TUNDEV=tun0"));
        QVERIFY(TunProtocol::SendLine(pair.Client(), ""));

        std::string line;
        QVERIFY(TunProtocol::ReadLine(pair.Helper(), line));
        QCOMPARE(line, std::string("SCRIPT connect"));
        QVERIFY(TunProtocol::ReadLine(pair.Helper(), line));
        QCOMPARE(line, std::string("TUNDEV=tun0"));
        QVERIFY(TunProtocol::ReadLine(pair.Helper(), line));
        QVERIFY(line.empty());
    }

    // The helper runs as root; an unbounded line is an allocation the peer
    // controls.
    void TestTunProtocol::failsWhenTheLineExceedsTheLimit()
    {
        SocketPair pair;
        const std::string oversized(64, 'x');
        QVERIFY(TunProtocol::SendLine(pair.Client(), oversized));

        std::string line;
        QVERIFY(!TunProtocol::ReadLine(pair.Helper(), line, 16));
    }

    void TestTunProtocol::failsWhenThePeerHangsUp()
    {
        SocketPair pair;
        QVERIFY(write(pair.Client(), "BYE", 3) == 3);
        pair.CloseClient();

        std::string line;
        QVERIFY(!TunProtocol::ReadLine(pair.Helper(), line));
    }

    void TestTunProtocol::passesADescriptorWithItsMessage()
    {
        SocketPair pair;

        const int original = open("/dev/null", O_RDONLY | O_CLOEXEC);
        QVERIFY(original >= 0);

        QVERIFY(TunProtocol::SendFd(pair.Helper(), original, "OK tun3\n"));

        std::string message;
        int received = -1;
        QVERIFY(TunProtocol::ReceiveFd(pair.Client(), message, received));

        QCOMPARE(message, std::string("OK tun3\n"));
        QVERIFY(received >= 0);
        QVERIFY(received != original);
        QCOMPARE(InodeOf(received), InodeOf(original));

        close(received);
        close(original);
    }

    void TestTunProtocol::reportsNoDescriptorWhenNoneWasAttached()
    {
        SocketPair pair;
        QVERIFY(TunProtocol::SendLine(pair.Helper(), "ERR could not create tun device"));

        std::string message;
        int received = 42;
        QVERIFY(TunProtocol::ReceiveFd(pair.Client(), message, received));

        QCOMPARE(message, std::string("ERR could not create tun device\n"));
        QCOMPARE(received, -1);
    }
} // namespace

QTEST_APPLESS_MAIN(TestTunProtocol)

#include "tst_tun_protocol.moc"
