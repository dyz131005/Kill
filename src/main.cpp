#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QEvent>
#include <QObject>
#include <QWheelEvent>

#include "main_window.h"

// Disables mouse-wheel value switching on combo boxes and spin boxes (so the
// user can no longer accidentally change a setting while scrolling the page),
// while still forwarding the wheel to the nearest scroll area so the page keeps
// scrolling smoothly under the cursor.
class NoWheelValueFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget)
            return QObject::eventFilter(watched, event);

        const bool isCombo = qobject_cast<QComboBox *>(widget) != nullptr;
        const bool isSpin = qobject_cast<QAbstractSpinBox *>(widget) != nullptr;
        if (!isCombo && !isSpin)
            return QObject::eventFilter(watched, event);

        // Re-target the wheel to the closest scroll area (if any) so the page
        // still scrolls, but the widget's value does NOT change.
        QWidget *target = widget->parentWidget();
        while (target && !qobject_cast<QAbstractScrollArea *>(target))
            target = target->parentWidget();
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(target)) {
            QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
            QWheelEvent forwarded(wheel->position(), wheel->globalPosition(),
                                  wheel->pixelDelta(), wheel->angleDelta(),
                                  wheel->buttons(), wheel->modifiers(),
                                  wheel->phase(), wheel->inverted(),
                                  wheel->source());
            QApplication::sendEvent(scrollArea->viewport(), &forwarded);
        }
        // Swallow the event so the combo/spin never sees it (no value change).
        return true;
    }
};

static void installBundledCaCertificates()
{
    QFile caFile(QStringLiteral(":/assets/cacert.pem"));
    if (!caFile.open(QIODevice::ReadOnly)) {
        return;
    }
    const QList<QSslCertificate> bundled = QSslCertificate::fromData(caFile.readAll(), QSsl::Pem);
    caFile.close();
    if (bundled.isEmpty()) {
        return;
    }

    // Merge the bundled Mozilla CA store with whatever the system provides.
    // On Windows 7 RTM the system root store is often too old (or too small)
    // to validate modern certificate chains, which makes HTTPS requests fail
    // with "root certificate is self-signed / untrusted". Bundling a current
    // CA list keeps validation working without relying on OS updates.
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    QList<QSslCertificate> certs = config.caCertificates();
    certs.append(bundled);
    config.setCaCertificates(certs);
    QSslConfiguration::setDefaultConfiguration(config);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Kill");
    QApplication::setOrganizationName("KillLauncher");
    QApplication::setWindowIcon(QIcon(":/icon.ico"));

    // Globally stop mouse-wheel from changing combo/spin values (prevents
    // accidental setting changes while scrolling), while letting the page scroll.
    NoWheelValueFilter noWheelFilter;
    app.installEventFilter(&noWheelFilter);

    installBundledCaCertificates();

    QFile styleFile(":/assets/style.qss");
    if (styleFile.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    MainWindow window;
    window.show();
    return app.exec();
}
