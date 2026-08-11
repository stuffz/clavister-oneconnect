#pragma once

#include <functional>
#include <string>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMenu>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>

#include "core/profile_store.hpp"
#include "ui/qt/vpn_controller.hpp"

// StatusNotifierItem via Qt, which is what Plasma consumes.
class TrayIcon
{
public:
    using ConnectRequest = std::function<void(const std::string &)>;

    TrayIcon(ProfileStore &profileStore, VpnController &vpnController)
        : store(profileStore), controller(vpnController)
    {
        menu = new QMenu();
        tray.setContextMenu(menu);
        tray.setIcon(IconFor(VpnState::Disconnected));
        tray.setToolTip(QObject::tr("Not connected"));
    }

    ~TrayIcon()
    {
        delete menu;
    }

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    void OnConnectRequested(ConnectRequest handler)
    {
        connectHandler = std::move(handler);
    }

    void OnShowWindow(std::function<void()> handler)
    {
        showHandler = std::move(handler);

        QObject::connect(&tray, &QSystemTrayIcon::activated,
                         [this](QSystemTrayIcon::ActivationReason reason)
                         {
                             if (reason == QSystemTrayIcon::Trigger && showHandler)
                             {
                                 showHandler();
                             }
                         });
    }

    void Show()
    {
        Rebuild();
        tray.show();
    }

    void SetState(VpnState state, const std::string &message)
    {
        tray.setIcon(IconFor(state));
        tray.setToolTip(QString::fromStdString(message));
        Rebuild();
    }

    void Notify(const QString &title, const QString &message)
    {
        tray.showMessage(title, message, QSystemTrayIcon::Information, 4000);
    }

private:
    void Rebuild()
    {
        menu->clear();

        const bool connected = controller.State() == VpnState::Connected;

        if (connected)
        {
            auto *status =
                menu->addAction(QObject::tr("Connected: %1")
                                    .arg(QString::fromStdString(controller.ActiveProfileName())));
            status->setEnabled(false);

            menu->addAction(QObject::tr("Disconnect"),
                            [this]
                            {
                                controller.Disconnect();
                            });
        }
        else if (controller.IsBusy())
        {
            auto *status = menu->addAction(QObject::tr("Working..."));
            status->setEnabled(false);
        }
        else
        {
            auto *header = menu->addAction(QObject::tr("Connect to"));
            header->setEnabled(false);

            for (const ConnectionProfile &profile : store.Profiles())
            {
                const std::string name = profile.name;
                menu->addAction(QString::fromStdString(name),
                                [this, name]
                                {
                                    if (connectHandler)
                                    {
                                        connectHandler(name);
                                    }
                                });
            }

            if (store.Profiles().empty())
            {
                auto *empty = menu->addAction(QObject::tr("No connections configured"));
                empty->setEnabled(false);
            }
        }

        menu->addSeparator();

        menu->addAction(QObject::tr("Show window"),
                        [this]
                        {
                            if (showHandler)
                            {
                                showHandler();
                            }
                        });

        menu->addAction(QObject::tr("Quit"),
                        []
                        {
                            QApplication::quit();
                        });
    }

    // Two separate SVGs rather than one icon dimmed at runtime: rasterising
    // and repainting yields a bitmap with no device-pixel-ratio, which renders
    // at the wrong size on scaled displays. Icon files: see ATTRIBUTION.md.
    static QIcon IconFor(VpnState state)
    {
        const bool up = state == VpnState::Connected;

        static const QIcon connected = LoadShield("oneconnect.svg");
        static const QIcon disconnected = LoadShield("oneconnect-disconnected.svg");

        const QIcon &shield = up ? connected : disconnected;
        if (!shield.isNull())
        {
            return shield;
        }

        QIcon themed = QIcon::fromTheme(up ? "network-vpn" : "network-vpn-disconnected");
        if (!themed.isNull())
        {
            return themed;
        }

        return QApplication::style()->standardIcon(up ? QStyle::SP_DialogApplyButton
                                                      : QStyle::SP_DialogCancelButton);
    }

    static QIcon LoadShield(const QString &fileName)
    {
        const QStringList directories = {
            QStringLiteral("/usr/local/share/icons/hicolor/scalable/apps"),
            QStringLiteral("/usr/share/icons/hicolor/scalable/apps"),
            QStringLiteral("resources/icons"),
        };

        for (const QString &directory : directories)
        {
            const QString path = directory + '/' + fileName;
            if (QFile::exists(path))
            {
                return QIcon(path);
            }
        }

        return {};
    }

    ProfileStore &store;
    VpnController &controller;
    QSystemTrayIcon tray;
    QMenu *menu = nullptr;

    ConnectRequest connectHandler;
    std::function<void()> showHandler;
};
