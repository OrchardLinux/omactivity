#include "inspector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <unistd.h>

Inspector::Inspector(ProcessModel *model, const QString &procRoot, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_proc(procRoot)
{
    connect(model, &ProcessModel::updated, this, &Inspector::refresh);
}

QString Inspector::stateName(char state)
{
    switch (state) {
    case 'R': return QStringLiteral("Running");
    case 'S': return QStringLiteral("Sleeping");
    case 'D': return QStringLiteral("Waiting");
    case 'I': return QStringLiteral("Idle");
    case 'T': return QStringLiteral("Stopped");
    case 't': return QStringLiteral("Traced");
    case 'Z': return QStringLiteral("Zombie");
    case 'X': return QStringLiteral("Dead");
    }
    return QStringLiteral("Unknown");
}

QString Inspector::formatElapsed(qint64 s)
{
    if (s < 0)
        s = 0;
    if (s >= 86400)
        return QStringLiteral("%1d %2:%3").arg(s / 86400).arg(s / 3600 % 24, 2, 10, QLatin1Char('0')).arg(s / 60 % 60, 2, 10, QLatin1Char('0'));
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString Inspector::notAvailable(uint uid) const
{
    return QStringLiteral("Not available: owned by %1").arg(m_model->userName(uid));
}

QVariantList Inspector::cpuHistory() const
{
    QVariantList out;
    const QVector<float> h = m_model->historyFor(m_id);
    out.reserve(h.size());
    for (float f : h)
        out << double(f);
    return out;
}

void Inspector::open(const QString &rowKey)
{
    const QVariantMap row = m_model->rowInfo(rowKey);
    if (!row.value(QStringLiteral("valid")).toBool())
        return;
    m_key = rowKey;
    m_processKey = row.value(QStringLiteral("processKey")).toString();
    m_files.clear();
    m_filesNote.clear();
    m_filesLoaded = false;
    m_env.clear();
    m_envNote.clear();
    m_envLoaded = false;
    emit keyChanged();
    emit filesChanged();
    emit environmentChanged();
    refresh();
}

void Inspector::close()
{
    if (m_key.isEmpty())
        return;
    m_key.clear();
    m_processKey.clear();
    m_id = 0;
    m_info.clear();
    m_children.clear();
    emit keyChanged();
    emit infoChanged();
}

void Inspector::refresh()
{
    if (m_key.isEmpty())
        return;
    const SnapshotPtr snap = m_model->snapshot();
    const ProcSample *p = m_model->procByKey(m_processKey);
    if (!p || !snap) {
        // The process ended while the panel was open. Keep what was shown
        // and say so, instead of closing under the user's finger.
        if (!m_info.value(QStringLiteral("ended")).toBool()) {
            m_info[QStringLiteral("ended")] = true;
            m_info[QStringLiteral("state")] = QStringLiteral("Ended");
            m_info[QStringLiteral("valid")] = false;
            emit infoChanged();
        }
        return;
    }
    m_id = p->id();

    QVariantMap m = m_model->rowInfo(m_key);
    const long tps = snap->ticksPerSecond;
    const QString base = m_proc + QLatin1Char('/') + QString::number(p->pid) + QLatin1Char('/');
    const bool app = m.value(QStringLiteral("isApp")).toBool();

    m[QStringLiteral("ended")] = false;
    m[QStringLiteral("title")] = app ? m.value(QStringLiteral("name")).toString() : p->name;
    m[QStringLiteral("processName")] = p->name;
    m[QStringLiteral("userLabel")] = QStringLiteral("%1 (%2)").arg(m_model->userName(p->uid)).arg(p->uid);
    m[QStringLiteral("state")] = stateName(p->state);
    m[QStringLiteral("pgrp")] = p->pgrp;
    m[QStringLiteral("cmdline")] = p->cmdline.isEmpty() ? QStringLiteral("[%1]").arg(p->name) : p->cmdline;
    // "App" names the app's own scope. A process outside one belongs to no
    // app; the unit it does run in (a service, the login session) is shown
    // as that, not as an app.
    const QString unit = ProcessModel::unitOf(p->cgroup);
    m[QStringLiteral("cgroup")] = !p->appUnit.isEmpty() ? p->appUnit : QStringLiteral("—");
    m[QStringLiteral("unit")] = p->kernelThread ? QStringLiteral("Kernel") : unit.isEmpty() ? QStringLiteral("—") : unit;
    m[QStringLiteral("sleepBlock")] = m_model->sleepBlocker(p->pid);
    m[QStringLiteral("cpuWait")] = QString::number(p->cpuWaitPercent, 'f', 1) + QLatin1Char('%');
    m[QStringLiteral("ioWait")] = p->ioWaitPercent < 0 ? QStringLiteral("not measured") : QString::number(p->ioWaitPercent, 'f', 1) + QLatin1Char('%');

    if (const ProcSample *parent = p->ppid > 0 ? m_model->procByPid(p->ppid) : nullptr) {
        m[QStringLiteral("parent")] = QStringLiteral("%1 (%2)").arg(parent->name).arg(parent->pid);
        m[QStringLiteral("parentKey")] = ProcessModel::processKey(*parent);
    } else {
        m[QStringLiteral("parent")] = QStringLiteral("—");
        m[QStringLiteral("parentKey")] = QString();
    }

    const qint64 startSecs = snap->sys.bootTime + qint64(p->startTicks / quint64(tps));
    const QDateTime started = QDateTime::fromSecsSinceEpoch(startSecs);
    const QDateTime now = QDateTime::currentDateTime();
    m[QStringLiteral("started")] = started.date() == now.date() ? started.toString(QStringLiteral("HH:mm:ss"))
                                                               : started.toString(QStringLiteral("d MMM HH:mm"));
    m[QStringLiteral("elapsed")] = formatElapsed(qint64(snap->sys.uptimeSeconds) - qint64(p->startTicks / quint64(tps)));

    const QString cwd = QFile::symLinkTarget(base + QLatin1String("cwd"));
    m[QStringLiteral("cwd")] = cwd.isEmpty() ? (p->uid != m_model->myUid() && !p->kernelThread ? notAvailable(p->uid) : QStringLiteral("—")) : cwd;

    m[QStringLiteral("cpu")] = QString::number(p->cpuPercent, 'f', 1);
    m[QStringLiteral("cpuTime")] = ProcessModel::formatCpuTime(p->cpuTicks, tps);
    m[QStringLiteral("threads")] = p->threads;
    m[QStringLiteral("nice")] = p->nice;

    // The list reads smaps_rollup only on the Memory tab; the panel always
    // shows it, so read this one process here.
    QByteArray buf;
    procfs::Smaps smaps;
    const bool haveSmaps = !p->kernelThread && procfs::readFile(base + QLatin1String("smaps_rollup"), buf)
                           && procfs::parseSmapsRollup(buf, smaps);
    const QString denied = p->kernelThread ? QStringLiteral("—") : notAvailable(p->uid);
    m[QStringLiteral("memReal")] = ProcessModel::formatBytes(p->rssKb);
    m[QStringLiteral("memPrivate")] = haveSmaps ? ProcessModel::formatBytes(smaps.privateKb) : denied;
    m[QStringLiteral("memShared")] = haveSmaps ? ProcessModel::formatBytes(smaps.sharedKb) : denied;
    m[QStringLiteral("memSwap")] = haveSmaps ? ProcessModel::formatBytes(smaps.swapKb) : denied;
    m[QStringLiteral("memVirtual")] = ProcessModel::formatBytes(qint64(p->vsizeKb));

    m[QStringLiteral("ctxt")] = ProcessModel::formatCount(qint64(p->ctxtVoluntary)) + QLatin1String(" / ")
                                + ProcessModel::formatCount(qint64(p->ctxtNonvoluntary));
    m[QStringLiteral("faults")] = ProcessModel::formatCount(qint64(p->minflt)) + QLatin1String(" / ")
                                  + ProcessModel::formatCount(qint64(p->majflt));
    procfs::Io io;
    if (procfs::readFile(base + QLatin1String("io"), buf) && procfs::parseIo(buf, io))
        m[QStringLiteral("io")] = ProcessModel::formatBytes(qint64(io.readBytes / 1024)) + QStringLiteral(" · ")
                                  + ProcessModel::formatBytes(qint64(io.writeBytes / 1024));
    else
        m[QStringLiteral("io")] = denied;

    QVariantList children;
    for (const ProcSample &c : snap->procs) {
        if (c.ppid != p->pid || c.pid == p->pid)
            continue;
        children << QVariantMap{{"key", ProcessModel::processKey(c)},
                                {"name", c.name},
                                {"pid", c.pid},
                                {"cpu", QString::number(c.cpuPercent, 'f', 1)},
                                {"memory", ProcessModel::formatBytes(c.memKb)}};
    }
    m_children = children;
    m_info = m;
    emit infoChanged();
}

void Inspector::loadFiles()
{
    m_files.clear();
    m_filesNote.clear();
    m_filesLoaded = true;
    const ProcSample *p = m_model->procByKey(m_processKey);
    if (!p) {
        emit filesChanged();
        return;
    }
    const QString base = m_proc + QLatin1Char('/') + QString::number(p->pid);
    QDir fdDir(base + QLatin1String("/fd"));
    if (!fdDir.isReadable()) {
        m_filesNote = notAvailable(p->uid);
        emit filesChanged();
        return;
    }

    QHash<quint64, procfs::SocketInfo> sockets;
    QByteArray buf;
    const QString net = m_proc + QLatin1String("/net/");
    static const char *const tables[][2] = {{"tcp", "TCP"}, {"tcp6", "TCP"}, {"udp", "UDP"}, {"udp6", "UDP"}};
    for (const auto &t : tables) {
        if (procfs::readFile(net + QLatin1String(t[0]), buf))
            procfs::parseNetSockets(buf, QLatin1String(t[1]), sockets);
    }
    if (procfs::readFile(net + QLatin1String("unix"), buf))
        procfs::parseNetUnix(buf, sockets);

    QStringList fds = fdDir.entryList(QDir::Files | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot);
    std::sort(fds.begin(), fds.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    for (const QString &fd : fds) {
        char linkBuf[4096];
        const ssize_t n = ::readlink(QFile::encodeName(fdDir.filePath(fd)).constData(), linkBuf, sizeof linkBuf - 1);
        if (n < 0)
            continue;
        const QString target = QString::fromUtf8(linkBuf, int(n));
        QString kind = QStringLiteral("file");
        QString text = target;
        QString detail;
        if (target.startsWith(QLatin1String("socket:["))) {
            kind = QStringLiteral("socket");
            const quint64 inode = target.mid(8, target.size() - 9).toULongLong();
            const auto it = sockets.constFind(inode);
            if (it != sockets.constEnd()) {
                if (it->proto == QLatin1String("unix")) {
                    text = it->local.isEmpty() ? QStringLiteral("Unix socket") : it->local;
                    detail = QStringLiteral("unix");
                } else {
                    const bool listening = it->remote.endsWith(QLatin1String(":*"));
                    text = listening ? it->local : it->local + QStringLiteral(" → ") + it->remote;
                    detail = listening ? it->proto + QStringLiteral(" · listening") : it->proto;
                }
            } else {
                text = QStringLiteral("Socket");
                detail = QStringLiteral("other");
            }
        } else if (target.startsWith(QLatin1String("pipe:["))) {
            kind = QStringLiteral("pipe");
            text = QStringLiteral("Pipe");
            detail = target.mid(6, target.size() - 7);
        } else if (target.startsWith(QLatin1String("anon_inode:"))) {
            kind = QStringLiteral("other");
            text = target.mid(11);
            if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']')))
                text = text.mid(1, text.size() - 2);
        }
        m_files << QVariantMap{{"fd", fd.toInt()}, {"kind", kind}, {"text", text}, {"detail", detail}};
    }
    emit filesChanged();
}

void Inspector::loadEnvironment()
{
    m_env.clear();
    m_envNote.clear();
    m_envLoaded = true;
    const ProcSample *p = m_model->procByKey(m_processKey);
    if (p) {
        QByteArray buf;
        int err = 0;
        if (procfs::readFile(m_proc + QLatin1Char('/') + QString::number(p->pid) + QLatin1String("/environ"), buf, &err)) {
            const QList<QByteArray> vars = buf.split('\0');
            for (const QByteArray &v : vars) {
                if (!v.isEmpty())
                    m_env << QString::fromUtf8(v);
            }
            m_env.sort();
            if (m_env.isEmpty())
                m_envNote = QStringLiteral("No environment variables");
        } else {
            m_envNote = (err == EACCES || err == EPERM) ? notAvailable(p->uid) : QStringLiteral("Not available");
        }
    }
    emit environmentChanged();
}
