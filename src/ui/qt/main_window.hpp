#pragma once

#include <iterator>
#include <string>

#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "core/profile_store.hpp"
#include "core/route_reconciler.hpp"
#include "core/tunnel_info.hpp"
#include "ui/qt/profile_dialog.hpp"
#include "ui/qt/vpn_controller.hpp"

class MainWindow : public QMainWindow
{
public:
    MainWindow(ProfileStore &profileStore, const std::string &storePath)
        : store(profileStore), path(storePath), controller(this)
    {
        setWindowTitle(tr("Clavister OneConnect"));
        resize(940, 560);

        BuildUi();
        WireController();
        RefreshProfileList();
        UpdateButtons();
    }

    VpnController &Controller()
    {
        return controller;
    }

    void ConnectProfileByName(const std::string &name)
    {
        if (const ConnectionProfile *profile = store.Find(name))
        {
            controller.Connect(*profile);
        }
    }

    // The tray mirrors the window's state rather than subscribing separately,
    // so the two always agree.
    void OnStateChanged(std::function<void(VpnState, const std::string &)> handler)
    {
        stateHandler = std::move(handler);
    }

protected:
    // Closing hides to the tray and keeps the session alive; quitting is an
    // explicit action.
    void closeEvent(QCloseEvent *event) override
    {
        hide();
        event->ignore();
    }

private:
    void BuildUi()
    {
        auto *central = new QWidget(this);
        auto *layout = new QHBoxLayout(central);

        layout->addWidget(BuildConnectionPanel(), 1);
        layout->addWidget(BuildDetailPanel(), 2);

        setCentralWidget(central);
    }

    QWidget *BuildConnectionPanel()
    {
        auto *panel = new QGroupBox(tr("Connections"), this);
        auto *layout = new QVBoxLayout(panel);

        profileList = new QListWidget(panel);
        connect(profileList, &QListWidget::itemSelectionChanged, this, &MainWindow::UpdateButtons);
        connect(profileList, &QListWidget::itemDoubleClicked, this, &MainWindow::ToggleConnection);
        layout->addWidget(profileList);

        connectButton = new QPushButton(tr("Connect"), panel);
        connect(connectButton, &QPushButton::clicked, this, &MainWindow::ToggleConnection);
        layout->addWidget(connectButton);

        auto *row = new QHBoxLayout();

        auto *addButton = new QPushButton(tr("Add"), panel);
        connect(addButton, &QPushButton::clicked, this, &MainWindow::AddProfile);
        row->addWidget(addButton);

        editButton = new QPushButton(tr("Edit"), panel);
        connect(editButton, &QPushButton::clicked, this, &MainWindow::EditProfile);
        row->addWidget(editButton);

        removeButton = new QPushButton(tr("Remove"), panel);
        connect(removeButton, &QPushButton::clicked, this, &MainWindow::RemoveProfile);
        row->addWidget(removeButton);

        layout->addLayout(row);

        return panel;
    }

    QWidget *BuildDetailPanel()
    {
        auto *panel = new QWidget(this);
        auto *layout = new QVBoxLayout(panel);

        statusLabel = new QLabel(tr("Not connected"), panel);
        statusLabel->setWordWrap(true);
        layout->addWidget(statusLabel);

        detailLabel = new QLabel(panel);
        detailLabel->setWordWrap(true);
        detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(detailLabel);

        auto *routeBox = new QGroupBox(tr("Routes"), panel);
        auto *routeLayout = new QVBoxLayout(routeBox);

        routeSummary = new QLabel(tr("No tunnel"), routeBox);
        routeLayout->addWidget(routeSummary);

        routeTable = new QTableWidget(0, 3, routeBox);
        routeTable->setHorizontalHeaderLabels({tr("Network"), tr("State"), tr("Via")});
        routeTable->horizontalHeader()->setStretchLastSection(true);
        routeTable->verticalHeader()->setVisible(false);
        routeTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        routeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        routeLayout->addWidget(routeTable);

        layout->addWidget(routeBox, 1);

        return panel;
    }

    void WireController()
    {
        controller.OnStateChanged(
            [this](VpnState state, const std::string &message)
            {
                HandleStateChange(state, message);
            });

        controller.OnConnected(
            [this](const TunnelInfo &info, const RouteReport &routes)
            {
                lastRoutes = routes;
                ShowTunnel(info, routes);
            });

        controller.OnUpdated(
            [this](const TunnelInfo &info, const TunnelStats &counters)
            {
                lastStats = counters;
                ShowTunnel(info, lastRoutes);
            });
    }

    void HandleStateChange(VpnState state, const std::string &message)
    {
        statusLabel->setText(QString::fromStdString(message));

        if (state == VpnState::Disconnected)
        {
            ClearTunnel();
        }

        UpdateButtons();

        if (stateHandler)
        {
            stateHandler(state, message);
        }
    }

    void ShowTunnel(const TunnelInfo &info, const RouteReport &routes)
    {
        QString detail;
        detail += tr("Address: %1").arg(QString::fromStdString(info.address));
        detail += tr("   MTU: %1").arg(info.mtu);
        detail += info.IsSplitTunnel() ? tr("   Split tunnel") : tr("   Full tunnel");
        detail += "\n";

        detail +=
            tr("CSTP: %1")
                .arg(info.cstpCipher.empty() ? tr("n/a") : QString::fromStdString(info.cstpCipher));
        detail += "\n";

        detail += tr("DTLS: %1").arg(DtlsStatus(info));

        if (lastStats.txBytes != 0 || lastStats.rxBytes != 0)
        {
            detail += tr("\nTraffic: %1 sent, %2 received")
                          .arg(FormatBytes(lastStats.txBytes), FormatBytes(lastStats.rxBytes));
        }

        if (!info.dnsServers.empty())
        {
            QStringList servers;
            for (const std::string &server : info.dnsServers)
            {
                servers << QString::fromStdString(server);
            }
            detail +=
                info.dnsIgnored
                    ? tr("\nDNS: %1  (ignored - system resolver unchanged)").arg(servers.join(", "))
                    : tr("\nDNS: %1").arg(servers.join(", "));
        }

        detailLabel->setText(detail);

        routeSummary->setText(tr("%1 applied, %2 missing, %3 unexpected")
                                  .arg(routes.CountOf(RouteState::Applied))
                                  .arg(routes.CountOf(RouteState::Missing))
                                  .arg(routes.CountOf(RouteState::Unexpected)));

        routeTable->setRowCount(static_cast<int>(routes.routes.size()));

        int row = 0;
        for (const ReconciledRoute &route : routes.routes)
        {
            routeTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(route.cidr)));

            auto *stateItem = new QTableWidgetItem(QString::fromLatin1(route.StateName()));
            if (route.state == RouteState::Missing)
            {
                stateItem->setForeground(QBrush(QColor(200, 60, 60)));
            }
            routeTable->setItem(row, 1, stateItem);

            const QString via = (route.state == RouteState::Missing || route.gateway == "0.0.0.0")
                                    ? QString()
                                    : QString::fromStdString(route.gateway);
            routeTable->setItem(row, 2, new QTableWidgetItem(via));

            ++row;
        }

        routeTable->resizeColumnsToContents();
    }

    static QString DtlsStatus(const TunnelInfo &info)
    {
        if (info.dtlsDisabled)
        {
            return tr("disabled after repeated failure - traffic is using TLS only");
        }
        if (info.dtlsCipher.empty())
        {
            return tr("not established - traffic is using TLS only");
        }
        return QString::fromStdString(info.dtlsCipher);
    }

    void ClearTunnel()
    {
        detailLabel->clear();
        routeTable->setRowCount(0);
        routeSummary->setText(tr("No tunnel"));
        lastStats = {};
        lastRoutes = {};
    }

    static QString FormatBytes(uint64_t bytes)
    {
        static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};

        auto value = static_cast<double>(bytes);
        size_t unit = 0;

        while (value >= 1024.0 && unit + 1 < std::size(units))
        {
            value /= 1024.0;
            ++unit;
        }

        return QString::number(value, 'f', unit == 0 ? 0 : 1) + ' ' +
               QString::fromLatin1(units[unit]);
    }

    void ToggleConnection()
    {
        if (controller.IsBusy())
        {
            return;
        }

        if (controller.State() == VpnState::Connected)
        {
            controller.Disconnect();
            return;
        }

        if (const ConnectionProfile *profile = SelectedProfile())
        {
            controller.Connect(*profile);
        }
    }

    void AddProfile()
    {
        ProfileDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }

        if (store.Find(dialog.Profile().name) != nullptr)
        {
            QMessageBox::warning(this, tr("Duplicate name"),
                                 tr("A connection called '%1' already exists.")
                                     .arg(QString::fromStdString(dialog.Profile().name)));
            return;
        }

        store.Profiles().push_back(dialog.Profile());
        Persist();
        RefreshProfileList();
    }

    void EditProfile()
    {
        ConnectionProfile *existing = SelectedProfile();
        if (existing == nullptr)
        {
            return;
        }

        ProfileDialog dialog(this, *existing);
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }

        *existing = dialog.Profile();
        Persist();
        RefreshProfileList();
    }

    void RemoveProfile()
    {
        ConnectionProfile *existing = SelectedProfile();
        if (existing == nullptr)
        {
            return;
        }

        const QString name = QString::fromStdString(existing->name);

        if (QMessageBox::question(this, tr("Remove connection"), tr("Remove '%1'?").arg(name)) !=
            QMessageBox::Yes)
        {
            return;
        }

        auto &profiles = store.Profiles();
        profiles.erase(std::remove_if(profiles.begin(), profiles.end(),
                                      [&name](const ConnectionProfile &profile)
                                      {
                                          return profile.name == name.toStdString();
                                      }),
                       profiles.end());

        Persist();
        RefreshProfileList();
    }

    void Persist()
    {
        if (!store.Save(path))
        {
            QMessageBox::warning(this, tr("Could not save"),
                                 tr("Failed to write %1").arg(QString::fromStdString(path)));
        }
    }

    ConnectionProfile *SelectedProfile()
    {
        QListWidgetItem *item = profileList->currentItem();
        if (item == nullptr)
        {
            return nullptr;
        }

        return store.Find(item->data(Qt::UserRole).toString().toStdString());
    }

    void RefreshProfileList()
    {
        const QString previous = profileList->currentItem() != nullptr
                                     ? profileList->currentItem()->data(Qt::UserRole).toString()
                                     : QString();

        profileList->clear();

        for (const ConnectionProfile &profile : store.Profiles())
        {
            auto *item =
                new QListWidgetItem(QString("%1\n%2").arg(QString::fromStdString(profile.name),
                                                          QString::fromStdString(profile.gateway)),
                                    profileList);

            item->setData(Qt::UserRole, QString::fromStdString(profile.name));

            if (QString::fromStdString(profile.name) == previous)
            {
                profileList->setCurrentItem(item);
            }
        }

        if (profileList->currentItem() == nullptr && profileList->count() > 0)
        {
            profileList->setCurrentRow(0);
        }

        UpdateButtons();
    }

    void UpdateButtons()
    {
        const bool hasSelection = profileList->currentItem() != nullptr;
        const bool connected = controller.State() == VpnState::Connected;
        const bool busy = controller.IsBusy();

        connectButton->setEnabled((hasSelection || connected) && !busy);
        connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));

        // Edits under a live tunnel would not take effect until reconnect.
        editButton->setEnabled(hasSelection && !connected && !busy);
        removeButton->setEnabled(hasSelection && !connected && !busy);
    }

    ProfileStore &store;
    std::string path;
    VpnController controller;

    QListWidget *profileList = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *editButton = nullptr;
    QPushButton *removeButton = nullptr;
    QLabel *statusLabel = nullptr;
    QLabel *detailLabel = nullptr;
    QLabel *routeSummary = nullptr;
    QTableWidget *routeTable = nullptr;

    TunnelStats lastStats;
    RouteReport lastRoutes;

    std::function<void(VpnState, const std::string &)> stateHandler;
};
