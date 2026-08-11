#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/connection_profile.hpp"
#include "platform/vpnc_script.hpp"

class ProfileDialog : public QDialog
{
public:
    explicit ProfileDialog(QWidget *parent, ConnectionProfile existing = {})
        : QDialog(parent), profile(std::move(existing))
    {
        setWindowTitle(profile.name.empty() ? tr("New connection") : tr("Edit connection"));
        setModal(true);
        setMinimumWidth(460);

        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout();

        nameEdit = new QLineEdit(QString::fromStdString(profile.name), this);
        form->addRow(tr("Name"), nameEdit);

        gatewayEdit = new QLineEdit(QString::fromStdString(profile.gateway), this);
        gatewayEdit->setPlaceholderText(tr("vpn.example.com"));
        form->addRow(tr("Gateway"), gatewayEdit);

        auto *gatewayHint =
            new QLabel(tr("Use the hostname, never an IP address. Gateways can be set to reject "
                          "requests whose Host header does not match their configured name."),
                       this);
        gatewayHint->setWordWrap(true);
        gatewayHint->setEnabled(false);
        form->addRow(QString(), gatewayHint);

        usernameEdit = new QLineEdit(QString::fromStdString(profile.username), this);
        usernameEdit->setPlaceholderText(tr("optional, prefilled at login"));
        form->addRow(tr("Username"), usernameEdit);

        protocolCombo = new QComboBox(this);
        for (const char *protocol : {"anyconnect", "nc", "gp", "pulse", "f5", "fortinet", "array"})
        {
            protocolCombo->addItem(QString::fromLatin1(protocol));
        }
        protocolCombo->setCurrentText(QString::fromStdString(profile.protocol));
        form->addRow(tr("Protocol"), protocolCombo);

        form->addRow(BuildCaFileRow());

        interfaceEdit = new QLineEdit(QString::fromStdString(profile.interfaceName), this);
        interfaceEdit->setPlaceholderText(tr("optional, e.g. vpn0"));
        form->addRow(tr("Interface"), interfaceEdit);

        scriptEdit = new QLineEdit(QString::fromStdString(profile.vpncScript), this);
        {
            const std::string found = VpncScript::Find();
            scriptEdit->setPlaceholderText(found.empty()
                                               ? tr("not found - install vpnc-scripts")
                                               : tr("auto: %1").arg(QString::fromStdString(found)));
        }
        form->addRow(tr("vpnc-script"), scriptEdit);

        userAgentEdit = new QLineEdit(QString::fromStdString(profile.userAgent), this);
        form->addRow(tr("User agent"), userAgentEdit);

        auto *agentHint =
            new QLabel(tr("Must identify as an AnyConnect-compatible client, or the gateway may "
                          "return its browser portal page instead of the login form."),
                       this);
        agentHint->setWordWrap(true);
        agentHint->setEnabled(false);
        form->addRow(QString(), agentHint);

        osCombo = new QComboBox(this);
        for (const char *os : {"linux-64", "linux", "win", "mac-intel", "android", "apple-ios"})
        {
            osCombo->addItem(QString::fromLatin1(os));
        }
        osCombo->setCurrentText(QString::fromStdString(profile.reportedOs));
        form->addRow(tr("Reported OS"), osCombo);

        dtlsCheck = new QCheckBox(tr("Use DTLS (UDP) when available"), this);
        dtlsCheck->setChecked(profile.useDtls);
        form->addRow(QString(), dtlsCheck);

        rememberCheck = new QCheckBox(tr("Remember password in the keychain"), this);
        rememberCheck->setChecked(profile.rememberPassword);
        rememberCheck->setToolTip(
            tr("Stores only the password, in the desktop keychain (KWallet or "
               "gnome-keyring). One-time codes are never saved -- a stored code is "
               "both spent and a weakening of two-factor authentication."));
        form->addRow(QString(), rememberCheck);

        ignoreDnsCheck = new QCheckBox(tr("Do not let this connection change my DNS"), this);
        ignoreDnsCheck->setChecked(profile.ignorePushedDns);
        ignoreDnsCheck->setToolTip(
            tr("Leaves the system resolver exactly as it is. Useful when a gateway "
               "pushes a public resolver, which cannot resolve anything internal and "
               "only displaces your own DNS. Internal hostnames will not resolve "
               "either way in that case -- but your normal DNS keeps working."));
        form->addRow(QString(), ignoreDnsCheck);

        autoConnectCheck = new QCheckBox(tr("Connect automatically on start"), this);
        autoConnectCheck->setChecked(profile.autoConnect);
        form->addRow(QString(), autoConnectCheck);

        layout->addLayout(form);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
        layout->addWidget(buttons);

        connect(buttons, &QDialogButtonBox::accepted, this, &ProfileDialog::Validate);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    const ConnectionProfile &Profile() const
    {
        return profile;
    }

private:
    QWidget *BuildCaFileRow()
    {
        auto *row = new QWidget(this);
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        caFileEdit = new QLineEdit(QString::fromStdString(profile.caFile), row);
        caFileEdit->setPlaceholderText(tr("optional, if not in the system trust store"));

        auto *browse = new QPushButton(tr("CA certificate..."), row);
        connect(browse, &QPushButton::clicked, this,
                [this]
                {
                    const QString picked = QFileDialog::getOpenFileName(
                        this, tr("Select CA certificate"), QString(),
                        tr("Certificates (*.crt *.pem *.cer);;All files (*)"));
                    if (!picked.isEmpty())
                    {
                        caFileEdit->setText(picked);
                    }
                });

        rowLayout->addWidget(browse);
        rowLayout->addWidget(caFileEdit);

        return row;
    }

    void Validate()
    {
        const QString name = nameEdit->text().trimmed();
        const QString gateway = gatewayEdit->text().trimmed();

        if (name.isEmpty() || gateway.isEmpty())
        {
            QMessageBox::warning(this, tr("Incomplete"),
                                 tr("A name and a gateway are both required."));
            return;
        }

        // Confirm rather than block -- some deployments have no DNS name, but
        // connecting by IP can fail with a bare HTTP 500 after a successful
        // login, which reads as anything but this.
        if (LooksLikeIpAddress(gateway))
        {
            const auto answer = QMessageBox::question(
                this, tr("Gateway is an IP address"),
                tr("Gateways are often configured to answer only to their own hostname. "
                   "Connecting by IP can fail after login with an unhelpful error.\n\n"
                   "Use it anyway?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (answer != QMessageBox::Yes)
            {
                return;
            }
        }

        profile.name = name.toStdString();
        profile.gateway = gateway.toStdString();
        profile.username = usernameEdit->text().trimmed().toStdString();
        profile.protocol = protocolCombo->currentText().toStdString();
        profile.caFile = caFileEdit->text().trimmed().toStdString();
        profile.interfaceName = interfaceEdit->text().trimmed().toStdString();
        profile.vpncScript = scriptEdit->text().trimmed().toStdString();
        profile.userAgent = userAgentEdit->text().toStdString();
        profile.reportedOs = osCombo->currentText().toStdString();
        profile.useDtls = dtlsCheck->isChecked();
        profile.rememberPassword = rememberCheck->isChecked();
        profile.ignorePushedDns = ignoreDnsCheck->isChecked();
        profile.autoConnect = autoConnectCheck->isChecked();

        accept();
    }

    static bool LooksLikeIpAddress(const QString &value)
    {
        const QStringList parts = value.split('.');
        if (parts.size() != 4)
        {
            return false;
        }

        for (const QString &part : parts)
        {
            bool numeric = false;
            const int octet = part.toInt(&numeric);
            if (!numeric || octet < 0 || octet > 255)
            {
                return false;
            }
        }

        return true;
    }

    ConnectionProfile profile;

    QLineEdit *nameEdit = nullptr;
    QLineEdit *gatewayEdit = nullptr;
    QLineEdit *usernameEdit = nullptr;
    QComboBox *protocolCombo = nullptr;
    QLineEdit *caFileEdit = nullptr;
    QLineEdit *interfaceEdit = nullptr;
    QLineEdit *scriptEdit = nullptr;
    QLineEdit *userAgentEdit = nullptr;
    QComboBox *osCombo = nullptr;
    QCheckBox *dtlsCheck = nullptr;
    QCheckBox *rememberCheck = nullptr;
    QCheckBox *ignoreDnsCheck = nullptr;
    QCheckBox *autoConnectCheck = nullptr;
};
