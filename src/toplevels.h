// The compositor's list of open windows (wlr-foreign-toplevel-management,
// which labwc and Hyprland both speak). Used to ask an app to close its
// windows, so it can still show "save changes?", and for the Windowed view.
// The protocol reports an app id per window and no PID; a window is matched
// to its app through the app's systemd scope name.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

struct wl_registry;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

class Toplevels : public QObject
{
    Q_OBJECT
public:
    explicit Toplevels(QObject *parent = nullptr);
    ~Toplevels() override;

    bool available() const { return m_manager != nullptr; }
    QStringList appIds() const;
    bool hasWindow(const QString &appId) const;
    // Asks every window of the app to close. Returns how many were asked.
    int closeWindows(const QString &appId);

    // Called from the protocol listeners.
    struct Window {
        zwlr_foreign_toplevel_handle_v1 *handle = nullptr;
        QString appId;
    };
    void bind(wl_registry *registry, quint32 name, quint32 version);
    void add(zwlr_foreign_toplevel_handle_v1 *handle);
    void setAppId(zwlr_foreign_toplevel_handle_v1 *handle, const QString &appId);
    void remove(zwlr_foreign_toplevel_handle_v1 *handle);
    void managerFinished();

signals:
    void changed(const QStringList &appIds);

private:
    wl_registry *m_registry = nullptr;
    zwlr_foreign_toplevel_manager_v1 *m_manager = nullptr;
    QVector<Window> m_windows;
};
