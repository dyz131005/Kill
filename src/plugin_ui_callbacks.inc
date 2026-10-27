
// Included at the end of plugin_manager.cpp (same TU; private access OK).
void *PluginManager::cb_inject_widget(void *host, int slot, const char *title, void *(*factory)(void *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!factory) return nullptr;
    PluginWidgetInjection inj;
    inj.slot = slot;
    inj.title = QString::fromUtf8(title ? title : "");
    inj.factory = [factory, ud]() -> QWidget * { return static_cast<QWidget *>(factory(ud)); };
    self->m_widgetInjections.append(inj);
    return reinterpret_cast<void *>(1);
}

void *PluginManager::cb_register_button(void *host, int slot, const char *text, void (*on_click)(void *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    PluginButtonInjection inj;
    inj.slot = slot;
    inj.text = QString::fromUtf8(text ? text : "");
    inj.onClick = [on_click, ud]() { if (on_click) on_click(ud); };
    self->m_buttonInjections.append(inj);
    return reinterpret_cast<void *>(1);
}

void *PluginManager::cb_register_setting(void *host, const char *label, void *(*factory)(void *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!factory) return nullptr;
    PluginSettingInjection inj;
    inj.label = QString::fromUtf8(label ? label : "");
    inj.factory = [factory, ud]() -> QWidget * { return static_cast<QWidget *>(factory(ud)); };
    self->m_settingInjections.append(inj);
    return reinterpret_cast<void *>(1);
}

int PluginManager::cb_inject_style(void *host, const char *qss)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!qss) return -1;
    self->m_injectedStyles.append(QString::fromUtf8(qss));
    return 0;
}

int PluginManager::cb_override_page(void *host, int slot, void *(*factory)(void *), void *ud)
{
    auto *self = static_cast<PluginManager *>(host);
    if (!factory) return -1;
    PluginPageOverride ov;
    ov.slot = slot;
    ov.factory = [factory, ud]() -> QWidget * { return static_cast<QWidget *>(factory(ud)); };
    self->m_pageOverrides.append(ov);
    return 0;
}
