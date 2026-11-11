#include "toplevels.h"

#ifdef HAVE_WAYLAND
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <QGuiApplication>
#include <wayland-client.h>

namespace {

void handleTitle(void *, zwlr_foreign_toplevel_handle_v1 *, const char *) {}
void handleAppId(void *data, zwlr_foreign_toplevel_handle_v1 *handle, const char *appId)
{
    static_cast<Toplevels *>(data)->setAppId(handle, QString::fromUtf8(appId));
}
void handleOutput(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void handleState(void *, zwlr_foreign_toplevel_handle_v1 *, wl_array *) {}
void handleDone(void *, zwlr_foreign_toplevel_handle_v1 *) {}
void handleClosed(void *data, zwlr_foreign_toplevel_handle_v1 *handle)
{
    static_cast<Toplevels *>(data)->remove(handle);
}
void handleParent(void *, zwlr_foreign_toplevel_handle_v1 *, zwlr_foreign_toplevel_handle_v1 *) {}

const zwlr_foreign_toplevel_handle_v1_listener handleListener = {
    handleTitle, handleAppId, handleOutput, handleOutput, handleState, handleDone, handleClosed, handleParent,
};

void managerToplevel(void *data, zwlr_foreign_toplevel_manager_v1 *, zwlr_foreign_toplevel_handle_v1 *handle)
{
    static_cast<Toplevels *>(data)->add(handle);
}
void managerFinishedCb(void *data, zwlr_foreign_toplevel_manager_v1 *)
{
    static_cast<Toplevels *>(data)->managerFinished();
}
const zwlr_foreign_toplevel_manager_v1_listener managerListener = {managerToplevel, managerFinishedCb};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    if (!qstrcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name))
        static_cast<Toplevels *>(data)->bind(registry, name, version);
}
void registryRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener = {registryGlobal, registryRemove};

} // namespace

Toplevels::Toplevels(QObject *parent)
    : QObject(parent)
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display())
        return; // not a Wayland session: Quit goes straight to SIGTERM
    // Qt dispatches the default queue on this thread, so the listeners
    // below run here too.
    m_registry = wl_display_get_registry(wayland->display());
    wl_registry_add_listener(m_registry, &registryListener, this);
}

Toplevels::~Toplevels()
{
    for (const Window &w : std::as_const(m_windows))
        zwlr_foreign_toplevel_handle_v1_destroy(w.handle);
    if (m_manager)
        zwlr_foreign_toplevel_manager_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
}

void Toplevels::bind(wl_registry *registry, quint32 name, quint32 version)
{
    if (m_manager)
        return;
    m_manager = static_cast<zwlr_foreign_toplevel_manager_v1 *>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, qMin<quint32>(version, 3)));
    zwlr_foreign_toplevel_manager_v1_add_listener(m_manager, &managerListener, this);
}

void Toplevels::add(zwlr_foreign_toplevel_handle_v1 *handle)
{
    m_windows.append({handle, QString()});
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handleListener, this);
}

void Toplevels::setAppId(zwlr_foreign_toplevel_handle_v1 *handle, const QString &appId)
{
    for (Window &w : m_windows) {
        if (w.handle == handle && w.appId != appId) {
            w.appId = appId;
            emit changed(appIds());
        }
    }
}

void Toplevels::remove(zwlr_foreign_toplevel_handle_v1 *handle)
{
    for (int i = 0; i < m_windows.size(); ++i) {
        if (m_windows[i].handle == handle) {
            m_windows.remove(i);
            zwlr_foreign_toplevel_handle_v1_destroy(handle);
            emit changed(appIds());
            return;
        }
    }
}

void Toplevels::managerFinished()
{
    if (m_manager)
        zwlr_foreign_toplevel_manager_v1_destroy(m_manager);
    m_manager = nullptr;
}

int Toplevels::closeWindows(const QString &appId)
{
    int asked = 0;
    for (const Window &w : std::as_const(m_windows)) {
        if (!appId.isEmpty() && w.appId.compare(appId, Qt::CaseInsensitive) == 0) {
            zwlr_foreign_toplevel_handle_v1_close(w.handle);
            ++asked;
        }
    }
    return asked;
}

#else // !HAVE_WAYLAND

Toplevels::Toplevels(QObject *parent) : QObject(parent) {}
Toplevels::~Toplevels() = default;
void Toplevels::bind(wl_registry *, quint32, quint32) {}
void Toplevels::add(zwlr_foreign_toplevel_handle_v1 *) {}
void Toplevels::setAppId(zwlr_foreign_toplevel_handle_v1 *, const QString &) {}
void Toplevels::remove(zwlr_foreign_toplevel_handle_v1 *) {}
void Toplevels::managerFinished() {}
int Toplevels::closeWindows(const QString &) { return 0; }

#endif

QStringList Toplevels::appIds() const
{
    QStringList ids;
    for (const Window &w : m_windows) {
        if (!w.appId.isEmpty() && !ids.contains(w.appId))
            ids << w.appId;
    }
    return ids;
}

bool Toplevels::hasWindow(const QString &appId) const
{
    for (const Window &w : m_windows) {
        if (!appId.isEmpty() && w.appId.compare(appId, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}
