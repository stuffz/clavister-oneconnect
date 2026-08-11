#include <exception>
#include <iostream>
#include <string>

#include <QApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTimer>

#include "core/logger.hpp"
#include "core/profile_store.hpp"
#include "platform/display.hpp"
#include "platform/paths.hpp"
#include "platform/privilege.hpp"
#include "platform/single_instance.hpp"
#include "ui/qt/main_window.hpp"
#include "ui/qt/tray_icon.hpp"

#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

using std::string;

namespace
{
    // A message box is only attempted when there is a display: QApplication's
    // constructor aborts the process when it cannot load a platform plugin.
    void ReportStartupFailure(int argc, char **argv, const string &title, const string &detail)
    {
        LOG_ERROR(title + ": " + detail);
        std::cerr << title << "\n" << detail << "\n";

        if (!SessionDisplay::Available())
        {
            std::cerr << "\n" << SessionDisplay::Diagnosis() << "\n";
            return;
        }

        QApplication app(argc, argv);
        QMessageBox::warning(nullptr, QString::fromStdString(title),
                             QString::fromStdString(detail));
    }
} // namespace

int main(int argc, char **argv)
{
    try
    {
        const string logPath = Paths::LogFile();
        Logger::Instance().SetLogFile(logPath);
        Paths::ReturnOwnership(logPath);

        LOG_INFO("---- oneconnect-gui " GIT_HASH " starting ----");

        // Everything that can refuse to start runs before any Qt object is
        // constructed, so a refusal is always reportable.
        const string helperPath = Privilege::HelperPath();

        if (!Privilege::HasNetworkAdmin() && helperPath.empty())
        {
            ReportStartupFailure(argc, argv, "Helper not found",
                                 "oneconnect-helper is required to create the VPN "
                                 "interface without running this application as root.\n\n"
                                 "Looked in:\n" +
                                     Privilege::SearchedPaths());
            return 1;
        }

        SingleInstanceLock instanceLock;

        if (instanceLock.Acquire() == SingleInstanceLock::Result::AlreadyRunning)
        {
            ReportStartupFailure(argc, argv, "Already running",
                                 "Clavister OneConnect is already running as " +
                                     instanceLock.HolderDescription() +
                                     ".\n\nOnly one instance can hold a tunnel at a time. "
                                     "Use the existing window or its tray icon.");
            return 1;
        }

        if (!SessionDisplay::Available())
        {
            LOG_ERROR(SessionDisplay::Diagnosis());
            std::cerr << "oneconnect-gui needs a display.\n"
                      << SessionDisplay::Diagnosis() << "\n\n"
                      << "Run it from a graphical session, or use the console client:\n"
                      << "  oneconnect --list\n";
            return 1;
        }

        QApplication app(argc, argv);
        QApplication::setApplicationName("clavister-oneconnect");
        QApplication::setApplicationDisplayName("Clavister OneConnect");
        QApplication::setQuitOnLastWindowClosed(false);

        const string storePath = Paths::ConnectionsFile();

        ProfileStore store;
        if (!store.Load(storePath))
        {
            QMessageBox::warning(nullptr, QObject::tr("Could not read connections"),
                                 QObject::tr("%1\n\n%2\n\nThe file has been left untouched.")
                                     .arg(QString::fromStdString(storePath),
                                          QString::fromStdString(store.LastError())));
        }

        MainWindow window(store, storePath);
        window.Controller().SetHelperPath(helperPath);
        TrayIcon tray(store, window.Controller());

        tray.OnShowWindow(
            [&window]
            {
                window.show();
                window.raise();
                window.activateWindow();
            });

        auto *instanceTimer = new QTimer(&app);
        QObject::connect(instanceTimer, &QTimer::timeout,
                         [&instanceLock]
                         {
                             instanceLock.ServePending();
                         });
        instanceTimer->start(500);

        tray.OnConnectRequested(
            [&window](const string &name)
            {
                window.ConnectProfileByName(name);
            });

        window.OnStateChanged(
            [&tray](VpnState state, const string &message)
            {
                tray.SetState(state, message);

                if (state == VpnState::Connected || state == VpnState::Disconnected)
                {
                    tray.Notify(QObject::tr("Clavister OneConnect"),
                                QString::fromStdString(message));
                }
            });

        if (QSystemTrayIcon::isSystemTrayAvailable())
        {
            tray.Show();
        }
        else
        {
            // With no tray, hiding the window would strand the user.
            LOG_INFO("No system tray available; running as a plain window");
            QApplication::setQuitOnLastWindowClosed(true);
        }

        window.show();

        for (const ConnectionProfile &profile : store.Profiles())
        {
            if (profile.autoConnect)
            {
                LOG_INFO("Auto-connecting " + profile.name);
                window.ConnectProfileByName(profile.name);
                break;
            }
        }

        return QApplication::exec();
    }
    catch (const std::exception &ex)
    {
        LOG_ERROR(string("Unhandled exception: ") + ex.what());
        std::cerr << "Unhandled exception: " << ex.what() << "\n";
        return 1;
    }
    catch (...)
    {
        LOG_ERROR("Unhandled unknown exception");
        std::cerr << "Unhandled unknown exception\n";
        return 1;
    }
}
