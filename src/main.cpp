#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QSslCertificate>
#include <QSslConfiguration>

#include "main_window.h"

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

    installBundledCaCertificates();

    QFile styleFile(":/assets/style.qss");
    if (styleFile.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    MainWindow window;
    window.show();
    return app.exec();
}
