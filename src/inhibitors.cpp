#include "inhibitors.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

Inhibitors::Inhibitors(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(10000);
    connect(&m_timer, &QTimer::timeout, this, &Inhibitors::ask);
}

void Inhibitors::setActive(bool active)
{
    if (active == m_timer.isActive())
        return;
    if (active) {
        m_timer.start();
        ask();
    } else {
        m_timer.stop();
    }
}

void Inhibitors::ask()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (m_pending || !bus.isConnected())
        return;
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
                                                             QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("ListInhibitors"));
    m_pending = true;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &Inhibitors::answered);
}

void Inhibitors::answered(QDBusPendingCallWatcher *watcher)
{
    m_pending = false;
    watcher->deleteLater();
    const QDBusMessage reply = watcher->reply();
    QHash<int, QString> blockers;
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        // a(ssssuu): what, who, why, mode, uid, pid
        const QDBusArgument list = reply.arguments().constFirst().value<QDBusArgument>();
        list.beginArray();
        while (!list.atEnd()) {
            QString what, who, why, mode;
            uint uid = 0, pid = 0;
            list.beginStructure();
            list >> what >> who >> why >> mode >> uid >> pid;
            list.endStructure();
            // "delay" inhibitors only hold sleep back for a moment; "block" ones prevent it.
            if (mode != QLatin1String("block") || !(what.contains(QLatin1String("sleep")) || what.contains(QLatin1String("idle"))))
                continue;
            const QString text = (why.isEmpty() ? who : why);
            blockers.insert(int(pid), text);
        }
        list.endArray();
    }
    emit changed(blockers);
}
