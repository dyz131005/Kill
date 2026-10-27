// Plugin UI injection application (separate TU)
#include "main_window.h"

#include <QApplication>
#include <QTabWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>

void MainWindow::applyPluginUi()
{
    if (!m_pluginManager || !m_mainTabWidget) return;

    const QStringList styles = m_pluginManager->injectedStyles();
    if (!styles.isEmpty()) {
        const QString sep = QString(QChar(10));
        const QString base = qApp->styleSheet();
        const QString extra = styles.join(sep);
        qApp->setStyleSheet(base + sep + extra);
    }

    const auto widgets = m_pluginManager->widgetInjections();
    const auto buttons = m_pluginManager->buttonInjections();
    const auto settings = m_pluginManager->settingInjections();
    const auto overrides = m_pluginManager->pageOverrides();
    if (widgets.isEmpty() && buttons.isEmpty() && settings.isEmpty() && overrides.isEmpty())
        return;

    auto *extPage = new QWidget(m_mainTabWidget);
    auto *extLayout = new QVBoxLayout(extPage);
    auto *scroll = new QScrollArea(extPage);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget(scroll);
    auto *innerLayout = new QVBoxLayout(inner);

    for (const PluginWidgetInjection &inj : widgets) {
        QWidget *w = inj.factory ? inj.factory() : nullptr;
        if (w) innerLayout->addWidget(w);
    }
    for (const PluginPageOverride &ov : overrides) {
        QWidget *w = ov.factory ? ov.factory() : nullptr;
        if (w) innerLayout->addWidget(w);
    }
    for (const PluginSettingInjection &st : settings) {
        QWidget *w = st.factory ? st.factory() : nullptr;
        if (w) innerLayout->addWidget(w);
    }
    for (const PluginButtonInjection &bi : buttons) {
        auto *b = new QPushButton(bi.text, inner);
        if (bi.onClick) QObject::connect(b, &QPushButton::clicked, inner, bi.onClick);
        innerLayout->addWidget(b);
    }
    innerLayout->addStretch();
    scroll->setWidget(inner);
    extLayout->addWidget(scroll);
    m_mainTabWidget->addTab(extPage, QString::fromUtf8("插件扩展"));
}
