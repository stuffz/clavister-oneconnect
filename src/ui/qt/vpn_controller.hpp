#pragma once

#include <atomic>
#include <cerrno>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <QApplication>
#include <QMetaObject>
#include <QTimer>

#include "core/connection_profile.hpp"
#include "core/logger.hpp"
#include "core/route_reconciler.hpp"
#include "core/vpn_session.hpp"
#include "ui/qt/qt_prompter.hpp"

enum class VpnState
{
    Disconnected,
    Connecting,
    Connected,
    Disconnecting
};

// Runs the blocking VpnSession calls on a worker thread. All handlers are
// invoked on the GUI thread, so they can touch widgets directly. No Q_OBJECT
// anywhere, and the build has no moc step -- keep it that way.
class VpnController
{
public:
    using StateHandler = std::function<void(VpnState, const std::string &)>;
    using ConnectedHandler = std::function<void(const TunnelInfo &, const RouteReport &)>;
    using UpdateHandler = std::function<void(const TunnelInfo &, const TunnelStats &)>;

    explicit VpnController(QWidget *parentWindow)
        : prompter(parentWindow), state(VpnState::Disconnected)
    {
    }

    ~VpnController()
    {
        Disconnect();
        JoinWorker();
    }

    VpnController(const VpnController &) = delete;
    VpnController &operator=(const VpnController &) = delete;

    void OnStateChanged(StateHandler handler)
    {
        stateHandler = std::move(handler);
    }

    void OnConnected(ConnectedHandler handler)
    {
        connectedHandler = std::move(handler);
    }

    // Fires periodically after connecting; DTLS establishes after Connect()
    // returns, so only these updates ever report it.
    void OnUpdated(UpdateHandler handler)
    {
        updateHandler = std::move(handler);
    }

    void SetHelperPath(std::string path)
    {
        helperPath = std::move(path);
    }

    VpnState State() const
    {
        return state;
    }

    const std::string &ActiveProfileName() const
    {
        return activeProfile;
    }

    bool IsBusy() const
    {
        return state == VpnState::Connecting || state == VpnState::Disconnecting;
    }

    void Connect(const ConnectionProfile &profile)
    {
        if (state != VpnState::Disconnected)
        {
            return;
        }

        // The previous run's thread may be finished but not yet joined.
        JoinWorker();

        activeProfile = profile.name;
        SetState(VpnState::Connecting, "Connecting to " + profile.gateway);

        SessionOptions options = profile.ToSessionOptions();
        options.helperPath = helperPath;

        worker = std::thread(
            [this, options]
            {
                Run(options);
            });
    }

    // Returns immediately; the worker reports Disconnected when the mainloop
    // actually stops.
    void Disconnect()
    {
        if (session != nullptr)
        {
            if (state == VpnState::Connected)
            {
                SetState(VpnState::Disconnecting, "Disconnecting");
            }
            session->Cancel();
        }
    }

private:
    void Run(const SessionOptions &options)
    {
        session = std::make_unique<VpnSession>(prompter);

        session->OnInfoRefreshed(
            [this](const TunnelInfo &refreshed, const TunnelStats &counters)
            {
                PostToGui(
                    [this, refreshed, counters]
                    {
                        if (updateHandler)
                        {
                            updateHandler(refreshed, counters);
                        }
                    });
            });

        if (!session->Connect(options))
        {
            session.reset();
            SetState(VpnState::Disconnected, "Connection failed - see the log for details");
            return;
        }

        const TunnelInfo info = session->Info();
        const RouteReport routes = RouteReconciler::Reconcile(info, session->InterfaceName());

        SetState(VpnState::Connected, "Connected to " + options.gateway);

        PostToGui(
            [this, info, routes]
            {
                if (connectedHandler)
                {
                    connectedHandler(info, routes);
                }
                StartRefreshTimer();
            });

        const int result = session->RunMainLoop();

        PostToGui(
            [this]
            {
                StopRefreshTimer();
            });

        session.reset();
        SetState(VpnState::Disconnected, DisconnectMessage(result));
    }

    static std::string DisconnectMessage(int result)
    {
        if (result == -EINTR)
        {
            return "Disconnected";
        }
        if (result == -EPERM)
        {
            return "The gateway ended the session - sign in again";
        }
        return "Connection lost - see the log for details";
    }

    void StartRefreshTimer()
    {
        if (refreshTimer == nullptr)
        {
            refreshTimer = new QTimer(qApp);

            QObject::connect(refreshTimer, &QTimer::timeout,
                             [this]
                             {
                                 if (session != nullptr)
                                 {
                                     session->RequestRefresh();
                                 }
                             });
        }

        refreshTimer->start(2000);
        QTimer::singleShot(1500, qApp,
                           [this]
                           {
                               if (session != nullptr)
                               {
                                   session->RequestRefresh();
                               }
                           });
    }

    void StopRefreshTimer()
    {
        if (refreshTimer != nullptr)
        {
            refreshTimer->stop();
        }
    }

    void SetState(VpnState next, const std::string &message)
    {
        state = next;

        PostToGui(
            [this, next, message]
            {
                if (stateHandler)
                {
                    stateHandler(next, message);
                }
            });
    }

    // Queued, never blocking: the GUI thread may be waiting on an auth dialog
    // driven by this same worker. See the deadlock note in QtPrompter.
    template <typename Functor>
    static void PostToGui(Functor &&functor)
    {
        QMetaObject::invokeMethod(qApp, std::forward<Functor>(functor), Qt::QueuedConnection);
    }

    void JoinWorker()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    QtPrompter prompter;
    std::unique_ptr<VpnSession> session;
    std::thread worker;
    std::atomic<VpnState> state;
    std::string activeProfile;

    StateHandler stateHandler;
    ConnectedHandler connectedHandler;
    UpdateHandler updateHandler;
    std::string helperPath;
    QTimer *refreshTimer = nullptr;
};
