#include "ui/qt/profile_dialog.hpp"

#include <QLabel>
#include <QObject>
#include <QTest>

namespace
{
    class TestProfileDialog : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void wrappedHintsGetTheirFullHeight();
        void noWidgetIsSquashedBelowItsMinimum();
    };

    // The offscreen screen is 800x600, so a window capped at two thirds of it
    // is shorter than this dialog needs.
    void TestProfileDialog::wrappedHintsGetTheirFullHeight()
    {
        ProfileDialog dialog(nullptr);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        for (const QLabel *label : dialog.findChildren<QLabel *>())
        {
            if (!label->wordWrap())
            {
                continue;
            }

            QVERIFY2(label->height() >= label->heightForWidth(label->width()),
                     qPrintable(label->text()));
        }
    }

    void TestProfileDialog::noWidgetIsSquashedBelowItsMinimum()
    {
        ConnectionProfile saved;
        saved.name = "Work";
        saved.gateway = "vpn.example.com";

        ProfileDialog dialog(nullptr, saved);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        for (const QWidget *widget : dialog.findChildren<QWidget *>())
        {
            if (!widget->isVisible())
            {
                continue;
            }

            QVERIFY2(widget->height() >= widget->minimumSizeHint().height(),
                     widget->metaObject()->className());
        }
    }
} // namespace

QTEST_MAIN(TestProfileDialog)

#include "tst_profile_dialog.moc"
