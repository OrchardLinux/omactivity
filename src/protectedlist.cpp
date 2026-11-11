#include "protectedlist.h"

#include <QCoreApplication>
#include <QFile>

void ProtectedList::load(const QString &path)
{
    m_path = path;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        // Running from a build directory: use the copy compiled in, but keep
        // naming the system path, where an installed copy would be.
        f.setFileName(QStringLiteral(":/data/protected.conf"));
        if (!f.open(QIODevice::ReadOnly))
            return;
    }
    parse(f.readAll());
}

void ProtectedList::parse(const QByteArray &data)
{
    m_byName.clear();
    const QList<QByteArray> lines = data.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QString name = QString::fromUtf8(line.left(colon).trimmed());
        const QString text = QString::fromUtf8(line.mid(colon + 1).trimmed());
        if (!text.isEmpty())
            m_byName.insert(name.left(15), text);
    }
}

QString ProtectedList::consequence(int pid, const QString &comm) const
{
    if (pid == 1)
        return m_byName.value(QStringLiteral("@init"),
                              QStringLiteral("This is the first process of the system. The device will stop working until it is restarted."));
    if (pid == QCoreApplication::applicationPid())
        return m_byName.value(QStringLiteral("@self"), QStringLiteral("This is Omactivity itself. It will close."));
    return m_byName.value(comm.left(15));
}
