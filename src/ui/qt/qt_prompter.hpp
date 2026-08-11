#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "core/auth_prompter.hpp"

// libopenconnect prompts from the VPN worker thread and Qt widgets may only be
// touched from the GUI thread, so each prompt blocks the worker while it runs
// on the GUI thread (Qt::BlockingQueuedConnection). It follows that the GUI
// thread must never wait on the worker, or the two deadlock.
class QtPrompter : public AuthPrompter
{
public:
    explicit QtPrompter(QWidget *parentWindow) : parent(parentWindow) {}

    void ShowFormMessage(const std::string &banner, const std::string &message,
                         const std::string &error) override
    {
        pendingBanner = banner;
        pendingMessage = message;
        pendingError = error;
    }

    std::optional<std::string> AskText(const std::string &label, bool secret,
                                       const std::string &initial) override
    {
        std::optional<std::string> result;

        RunOnGuiThread(
            [&]
            {
                result = ShowTextDialog(label, secret, initial);
            });

        return result;
    }

    std::optional<std::string> AskChoice(const std::string &label,
                                         const std::vector<AuthChoice> &choices) override
    {
        if (choices.empty())
        {
            return std::nullopt;
        }

        if (choices.size() == 1)
        {
            return choices.front().name;
        }

        std::optional<std::string> result;

        RunOnGuiThread(
            [&]
            {
                result = ShowChoiceDialog(label, choices);
            });

        return result;
    }

private:
    // Safe to call from the GUI thread too, where it runs inline.
    template <typename Functor>
    void RunOnGuiThread(Functor &&functor)
    {
        QObject *context = qApp;

        if (QThread::currentThread() == context->thread())
        {
            functor();
            return;
        }

        QMetaObject::invokeMethod(context, std::forward<Functor>(functor),
                                  Qt::BlockingQueuedConnection);
    }

    std::optional<std::string> ShowTextDialog(const std::string &label, bool secret,
                                              const std::string &initial)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(secret ? QObject::tr("Authentication") : QObject::tr("Sign in"));
        dialog.setModal(true);

        auto *layout = new QVBoxLayout(&dialog);

        AddContextLabels(layout);

        layout->addWidget(new QLabel(QString::fromStdString(label), &dialog));

        auto *input = new QLineEdit(&dialog);
        if (secret)
        {
            input->setEchoMode(QLineEdit::Password);
        }
        else if (!initial.empty())
        {
            input->setText(QString::fromStdString(initial));
            input->selectAll();
        }
        layout->addWidget(input);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        input->setFocus();

        if (dialog.exec() != QDialog::Accepted)
        {
            return std::nullopt;
        }

        // A later form in the same exchange must not repeat this banner.
        ClearContext();

        return input->text().toStdString();
    }

    std::optional<std::string> ShowChoiceDialog(const std::string &label,
                                                const std::vector<AuthChoice> &choices)
    {
        QDialog dialog(parent);
        dialog.setWindowTitle(QObject::tr("Select"));
        dialog.setModal(true);

        auto *layout = new QVBoxLayout(&dialog);

        AddContextLabels(layout);

        layout->addWidget(new QLabel(QString::fromStdString(label), &dialog));

        auto *combo = new QComboBox(&dialog);
        for (const AuthChoice &choice : choices)
        {
            const std::string &text = choice.label.empty() ? choice.name : choice.label;
            combo->addItem(QString::fromStdString(text), QString::fromStdString(choice.name));
        }
        layout->addWidget(combo);

        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);

        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted)
        {
            return std::nullopt;
        }

        ClearContext();

        return combo->currentData().toString().toStdString();
    }

    // The banner and error arrive through ShowFormMessage() before the fields
    // do, so they are rendered as part of the next dialog.
    void AddContextLabels(QVBoxLayout *layout)
    {
        if (!pendingBanner.empty())
        {
            auto *banner = new QLabel(QString::fromStdString(pendingBanner));
            banner->setWordWrap(true);
            layout->addWidget(banner);
        }

        if (!pendingMessage.empty())
        {
            auto *message = new QLabel(QString::fromStdString(pendingMessage));
            message->setWordWrap(true);
            layout->addWidget(message);
        }

        if (!pendingError.empty())
        {
            auto *error = new QLabel(QString::fromStdString(pendingError));
            error->setWordWrap(true);
            error->setStyleSheet("color: palette(link-visited);");
            layout->addWidget(error);
        }
    }

    void ClearContext()
    {
        pendingBanner.clear();
        pendingMessage.clear();
        pendingError.clear();
    }

    QWidget *parent;
    std::string pendingBanner;
    std::string pendingMessage;
    std::string pendingError;
};
