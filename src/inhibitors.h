// Who keeps the device awake: the holders of logind's sleep and idle
// inhibitors (org.freedesktop.login1 ListInhibitors). Asked for only while
// the Energy tab shows it. Idle inhibitors that apps set through the
// compositor are not visible here.
#pragma once

#include <QHash>
#include <QObject>
#include <QTimer>

class QDBusPendingCallWatcher;

class Inhibitors : public QObject
{
    Q_OBJECT
public:
    explicit Inhibitors(QObject *parent = nullptr);
    void setActive(bool active);

signals:
    void changed(const QHash<int, QString> &blockers); // PID -> "sleep: Downloading updates"

private:
    void ask();
    void answered(QDBusPendingCallWatcher *watcher);
    QTimer m_timer;
    bool m_pending = false;
};
