// Processes whose loss takes the session down. Acting on one asks first and
// says what will happen; nothing is ever refused. The list is a config file
// the distro can extend.
#pragma once

#include <QHash>
#include <QString>

class ProtectedList
{
public:
    static QString defaultPath() { return QStringLiteral("/etc/omactivity/protected.conf"); }

    // Lines are "process-name: what happens". '#' starts a comment.
    void load(const QString &path);
    void parse(const QByteArray &data);
    QString path() const { return m_path; }

    // The consequence text for this process, or an empty string.
    QString consequence(int pid, const QString &comm) const;
    bool contains(int pid, const QString &comm) const { return !consequence(pid, comm).isEmpty(); }

private:
    QString m_path;
    QHash<QString, QString> m_byName; // keyed by the first 15 characters, as comm is
};
