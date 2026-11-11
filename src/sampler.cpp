#include "sampler.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <dirent.h>
#include <sys/statvfs.h>
#include <unistd.h>

Sampler::Sampler(const QString &procRoot, const QString &sysRoot)
    : m_proc(procRoot)
    , m_sys(sysRoot)
{
    const long t = sysconf(_SC_CLK_TCK);
    if (t > 0)
        m_ticks = t;
    const long page = sysconf(_SC_PAGESIZE);
    if (page > 0)
        m_pageKb = page / 1024;
}

SnapshotPtr Sampler::sample(double elapsed)
{
    auto snap = QSharedPointer<Snapshot>::create();
    snap->interval = m_round == 0 ? 0 : elapsed;
    snap->ticksPerSecond = m_ticks;
    ++m_round;

    // Disk-wait accounting is a kernel switch that is off by default.
    m_delayAcct = procfs::readFile(m_proc + QLatin1String("/sys/kernel/task_delayacct"), m_buf) && m_buf.trimmed() == "1";
    // Ports change rarely: look every third pass.
    const bool portPass = m_portDetail && (!m_portsFresh || m_round % 3 == 0);
    m_portsFresh = m_portDetail;
    if (portPass) {
        m_attributed.clear();
        m_listening.clear();
        QHash<quint64, procfs::SocketInfo> all;
        static const char *const tables[][2] = {{"tcp", "TCP"}, {"tcp6", "TCP"}, {"udp", "UDP"}, {"udp6", "UDP"}};
        for (const auto &t : tables) {
            if (procfs::readFile(m_proc + QLatin1String("/net/") + QLatin1String(t[0]), m_buf))
                procfs::parseNetSockets(m_buf, QLatin1String(t[1]), all);
        }
        m_connected.clear();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            if (it->listening)
                m_listening.insert(it.key(), *it);
            else if (it->connected)
                m_connected.insert(it.key());
        }
    }

    DIR *dir = opendir(QFile::encodeName(m_proc).constData());
    if (dir) {
        snap->procs.reserve(m_prev.size() + 16);
        while (dirent *e = readdir(dir)) {
            if (e->d_name[0] < '0' || e->d_name[0] > '9')
                continue;
            const int pid = atoi(e->d_name);
            const QString base = m_proc + QLatin1Char('/') + QLatin1String(e->d_name) + QLatin1Char('/');

            // A process may end between readdir and any of these reads; it
            // is then simply not part of this sample.
            procfs::Stat st;
            if (!procfs::readFile(base + QLatin1String("stat"), m_buf) || !procfs::parseStat(m_buf, st))
                continue;

            Prev &prev = m_prev[pid];
            const bool isNew = prev.age == 0 || prev.startTicks != st.starttime;
            if (isNew) {
                prev = Prev();
                prev.startTicks = st.starttime;
            }

            ProcSample p;
            p.pid = pid;
            p.ppid = st.ppid;
            p.pgrp = st.pgrp;
            p.state = st.state;
            p.kernelThread = st.kernelThread();
            p.name = QString::fromUtf8(st.comm);
            p.cpuTicks = st.utime + st.stime;
            p.startTicks = st.starttime;
            p.threads = st.threads;
            p.nice = st.nice;
            p.minflt = st.minflt;
            p.majflt = st.majflt;
            p.vsizeKb = st.vsizeBytes / 1024;
            p.rssKb = qMax<qint64>(0, st.rssPages) * m_pageKb;

            if (!isNew && snap->interval > 0 && p.cpuTicks >= prev.cpuTicks)
                p.cpuPercent = double(p.cpuTicks - prev.cpuTicks) / double(m_ticks) / snap->interval * 100.0;
            prev.cpuTicks = p.cpuTicks;

            quint64 runNs = 0, waitNs = 0;
            if (procfs::readFile(base + QLatin1String("schedstat"), m_buf) && procfs::parseSchedstat(m_buf, runNs, waitNs)) {
                if (prev.haveWait && snap->interval > 0 && waitNs >= prev.waitNs)
                    p.cpuWaitPercent = double(waitNs - prev.waitNs) / 1e9 / snap->interval * 100.0;
                prev.waitNs = waitNs;
            }
            if (m_delayAcct) {
                p.ioWaitPercent = 0;
                if (prev.haveWait && snap->interval > 0 && st.blkioTicks >= prev.blkioTicks)
                    p.ioWaitPercent = double(st.blkioTicks - prev.blkioTicks) / double(m_ticks) / snap->interval * 100.0;
                prev.blkioTicks = st.blkioTicks;
            }
            prev.haveWait = true;

            if (portPass && !p.kernelThread)
                socketsOf(base, prev);
            if (m_portDetail) {
                p.ports = prev.ports;
                p.portsKnown = prev.portsKnown;
                p.sockets = prev.sockets;
                p.connections = prev.connections;
            }

            if (m_ioDetail && !p.kernelThread && !prev.ioDenied) {
                int err = 0;
                procfs::Io io;
                if (procfs::readFile(base + QLatin1String("io"), m_buf, &err) && procfs::parseIo(m_buf, io)) {
                    if (prev.haveIo && snap->interval > 0) {
                        if (io.readBytes >= prev.io.readBytes)
                            p.readRate = double(io.readBytes - prev.io.readBytes) / snap->interval;
                        if (io.writeBytes >= prev.io.writeBytes)
                            p.writeRate = double(io.writeBytes - prev.io.writeBytes) / snap->interval;
                    }
                    prev.io = io;
                    prev.haveIo = true;
                } else if (err == EACCES || err == EPERM) {
                    prev.ioDenied = true; // someone else's process
                }
            }
            if (prev.haveIo) {
                p.readBytes = qint64(prev.io.readBytes);
                p.writeBytes = qint64(prev.io.writeBytes);
            }

            procfs::Status status;
            if (procfs::readFile(base + QLatin1String("status"), m_buf) && procfs::parseStatus(m_buf, status)) {
                const quint64 ctxt = status.voluntaryCtxt + status.nonvoluntaryCtxt;
                if (prev.haveStatus && ctxt >= prev.ctxt)
                    p.wakeups = double(ctxt - prev.ctxt);
                prev.ctxt = ctxt;
                prev.uid = status.uid;
                prev.haveStatus = true;
                p.ctxtVoluntary = status.voluntaryCtxt;
                p.ctxtNonvoluntary = status.nonvoluntaryCtxt;
                if (status.vmRssKb >= 0)
                    p.rssKb = status.vmRssKb;
            }
            p.uid = prev.uid;

            if (isNew || prev.comm != st.comm) {
                // comm changes on exec, and so does the command line.
                prev.comm = st.comm;
                if (procfs::readFile(base + QLatin1String("cmdline"), m_buf))
                    prev.cmdline = procfs::cmdlineToString(m_buf);
            }
            p.cmdline = prev.cmdline;

            // A launcher moves a process into its app scope shortly after
            // forking it, so look again for the first few samples, then only
            // now and then.
            if (prev.age < 3 || (m_round + quint32(pid)) % 5 == 0) {
                if (procfs::readFile(base + QLatin1String("cgroup"), m_buf)) {
                    const QString cg = procfs::parseCgroup(m_buf).trimmed();
                    if (cg != prev.cgroup || prev.age == 0) {
                        prev.cgroup = cg;
                        prev.appUnit = procfs::appUnitFromCgroup(cg);
                    }
                }
            }
            p.cgroup = prev.cgroup;
            p.appUnit = prev.appUnit;

            if (m_memoryDetail && !p.kernelThread && !prev.smapsDenied) {
                int err = 0;
                if (procfs::readFile(base + QLatin1String("smaps_rollup"), m_buf, &err)) {
                    prev.haveSmaps = procfs::parseSmapsRollup(m_buf, prev.smaps);
                } else if (err == EACCES || err == EPERM) {
                    prev.smapsDenied = true; // someone else's process: stays RSS
                }
            }
            if (prev.haveSmaps && !p.kernelThread) {
                // Kept from the last time it was read, so the Memory sort
                // does not change meaning when the user leaves the tab.
                p.memKb = prev.smaps.pssKb;
                p.memIsRss = false;
                p.privateKb = prev.smaps.privateKb;
                p.sharedKb = prev.smaps.sharedKb;
                p.swapKb = prev.smaps.swapKb;
            } else {
                p.memKb = p.rssKb;
            }

            prev.seen = m_round;
            ++prev.age;
            snap->sys.threads += p.threads;
            snap->procs.append(p);
        }
        closedir(dir);
    }

    // Forget processes that were not in this pass.
    for (auto it = m_prev.begin(); it != m_prev.end();) {
        if (it->seen != m_round)
            it = m_prev.erase(it);
        else
            ++it;
    }

    if (portPass) {
        QHash<uint, QStringList> rest;
        for (auto it = m_listening.constBegin(); it != m_listening.constEnd(); ++it) {
            if (m_attributed.contains(it.key()))
                continue;
            const QString text = it->proto == QLatin1String("UDP") ? QString::number(it->port) + QLatin1String("/udp")
                                                                   : QString::number(it->port);
            if (!rest[it->uid].contains(text))
                rest[it->uid] << text;
        }
        m_unattributed.clear();
        for (auto it = rest.begin(); it != rest.end(); ++it) {
            std::sort(it->begin(), it->end(), [](const QString &a, const QString &b) {
                const int x = a.section(QLatin1Char('/'), 0, 0).toInt(), y = b.section(QLatin1Char('/'), 0, 0).toInt();
                return x != y ? x < y : a < b;
            });
            m_unattributed.insert(it.key(), it->join(QLatin1String(", ")));
        }
    }
    if (m_portDetail)
        snap->sys.unattributedPorts = m_unattributed;

    snap->sys.processes = snap->procs.size();
    sampleSystem(snap->sys, snap->interval);
    return snap;
}

void Sampler::socketsOf(const QString &base, Prev &prev)
{
    prev.portsKnown = false;
    prev.ports.clear();
    prev.sockets = -1;
    prev.connections = -1;
    DIR *dir = opendir(QFile::encodeName(base + QLatin1String("fd")).constData());
    if (!dir)
        return; // someone else's process
    prev.portsKnown = true;
    prev.sockets = 0;
    prev.connections = 0;
    QStringList ports;
    const int dfd = dirfd(dir);
    int entries = 0, readable = 0;
    while (dirent *e = readdir(dir)) {
        if (e->d_name[0] == '.')
            continue;
        ++entries;
        char target[64];
        const ssize_t n = readlinkat(dfd, e->d_name, target, sizeof target - 1);
        if (n > 0)
            ++readable;
        if (n < 10 || qstrncmp(target, "socket:[", 8))
            continue;
        target[n] = 0;
        ++prev.sockets;
        const quint64 inode = strtoull(target + 8, nullptr, 10);
        if (m_connected.contains(inode))
            ++prev.connections;
        const auto it = m_listening.constFind(inode);
        if (it == m_listening.constEnd())
            continue;
        m_attributed.insert(it.key());
        const QString text = it->proto == QLatin1String("UDP") ? QString::number(it->port) + QLatin1String("/udp")
                                                               : QString::number(it->port);
        if (!ports.contains(text))
            ports << text;
    }
    closedir(dir);
    if (entries > 0 && readable == 0) {
        // The table can be listed but not read (another user namespace):
        // that is "unknown", not "no sockets".
        prev.portsKnown = false;
        prev.sockets = -1;
        prev.connections = -1;
        return;
    }
    std::sort(ports.begin(), ports.end(), [](const QString &a, const QString &b) {
        const int x = a.section(QLatin1Char('/'), 0, 0).toInt(), y = b.section(QLatin1Char('/'), 0, 0).toInt();
        return x != y ? x < y : a < b;
    });
    prev.ports = ports.join(QLatin1String(", "));
}

void Sampler::sampleSystem(SystemSample &sys, double elapsed)
{
    procfs::SystemStat stat;
    if (procfs::readFile(m_proc + QLatin1String("/stat"), m_buf) && procfs::parseSystemStat(m_buf, stat)) {
        sys.bootTime = stat.bootTime;
        if (m_havePrevStat) {
            const double total = double(stat.all.total() - m_prevStat.all.total());
            if (total > 0) {
                sys.userPercent = double(stat.all.user - m_prevStat.all.user) / total * 100.0;
                sys.systemPercent = double(stat.all.system - m_prevStat.all.system) / total * 100.0;
                sys.idlePercent = qMax(0.0, 100.0 - sys.userPercent - sys.systemPercent);
            }
            if (stat.cores.size() == m_prevStat.cores.size()) {
                sys.corePercent.reserve(stat.cores.size());
                for (int i = 0; i < stat.cores.size(); ++i) {
                    const double t = double(stat.cores[i].total() - m_prevStat.cores[i].total());
                    const double idle = double(stat.cores[i].idle - m_prevStat.cores[i].idle);
                    sys.corePercent.append(t > 0 ? float((t - idle) / t * 100.0) : 0.f);
                }
            }
        } else {
            // First sample: no interval to diff over yet, so show the
            // average since boot rather than nothing.
            const double total = double(stat.all.total());
            if (total > 0) {
                sys.userPercent = double(stat.all.user) / total * 100.0;
                sys.systemPercent = double(stat.all.system) / total * 100.0;
                sys.idlePercent = qMax(0.0, 100.0 - sys.userPercent - sys.systemPercent);
            }
        }
        m_prevStat = stat;
        m_havePrevStat = true;
    }

    procfs::LoadAvg load;
    if (procfs::readFile(m_proc + QLatin1String("/loadavg"), m_buf) && procfs::parseLoadAvg(m_buf, load)) {
        sys.load1 = load.l1;
        sys.load5 = load.l5;
        sys.load15 = load.l15;
    }
    if (procfs::readFile(m_proc + QLatin1String("/uptime"), m_buf))
        sys.uptimeSeconds = m_buf.left(m_buf.indexOf(' ')).toDouble();

    procfs::MemInfo mem;
    if (procfs::readFile(m_proc + QLatin1String("/meminfo"), m_buf) && procfs::parseMemInfo(m_buf, mem)) {
        sys.memTotalKb = mem.totalKb;
        sys.memUsedKb = qMax<qint64>(0, mem.totalKb - mem.availableKb);
        sys.cachedKb = mem.cachedKb;
        sys.swapTotalKb = mem.swapTotalKb;
        sys.swapUsedKb = qMax<qint64>(0, mem.swapTotalKb - mem.swapFreeKb);
    }
    if (procfs::readFile(m_proc + QLatin1String("/pressure/memory"), m_buf))
        procfs::parsePressure(m_buf, sys.pressure);

    // The fastest core right now; the cores of a cluster share a policy.
    const QDir cpuDir(m_sys + QLatin1String("/devices/system/cpu"));
    const QStringList cpus = cpuDir.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs);
    for (const QString &cpu : cpus) {
        if (procfs::readFile(cpuDir.filePath(cpu + QLatin1String("/cpufreq/scaling_cur_freq")), m_buf))
            sys.frequencyKhz = qMax(sys.frequencyKhz, m_buf.trimmed().toDouble());
    }

    // zram: mm_stat's third field is the memory the compressed data takes.
    const QDir blockDir(m_sys + QLatin1String("/block"));
    const QStringList zrams = blockDir.entryList({QStringLiteral("zram*")}, QDir::Dirs);
    for (const QString &z : zrams) {
        if (!procfs::readFile(blockDir.filePath(z + QLatin1String("/mm_stat")), m_buf))
            continue;
        const QList<QByteArray> f = m_buf.simplified().split(' ');
        if (f.size() >= 3) {
            if (sys.compressedKb < 0)
                sys.compressedKb = 0;
            sys.compressedKb += f[2].toLongLong() / 1024;
        }
    }

    // ---- battery: /sys/class/power_supply ----
    const QDir supplyDir(m_sys + QLatin1String("/class/power_supply"));
    const QStringList supplies = supplyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : supplies) {
        const QString dir = supplyDir.filePath(name) + QLatin1Char('/');
        auto text = [&](const char *file) {
            return procfs::readFile(dir + QLatin1String(file), m_buf) ? QString::fromLatin1(m_buf.trimmed()) : QString();
        };
        auto number = [&](const char *file) {
            bool ok = false;
            const double v = text(file).toDouble(&ok);
            return ok ? v : -1.0;
        };
        if (text("type") != QLatin1String("Battery") || text("scope") == QLatin1String("Device"))
            continue; // chargers, and batteries of mice and pens
        sys.hasBattery = true;
        sys.batteryPercent = int(number("capacity"));
        sys.batteryStatus = text("status");
        // Drivers report power, or current and voltage; some sign the
        // current while discharging.
        double watts = number("power_now") >= 0 ? number("power_now") / 1e6 : -1;
        const QString current = text("current_now"), voltage = text("voltage_now");
        if (watts < 0 && !current.isEmpty() && !voltage.isEmpty())
            watts = qAbs(current.toDouble()) * voltage.toDouble() / 1e12;
        sys.powerWatts = watts;
        const bool charging = sys.batteryStatus == QLatin1String("Charging");
        double seconds = number(charging ? "time_to_full_now" : "time_to_empty_now");
        if (seconds < 0 && watts > 0.01) {
            // Energy in µWh, or charge in µAh at the present voltage.
            double now = number("energy_now"), full = number("energy_full");
            if (now < 0 && !voltage.isEmpty()) {
                const double volts = voltage.toDouble() / 1e6;
                now = number("charge_now") >= 0 ? number("charge_now") * volts : -1;
                full = number("charge_full") >= 0 ? number("charge_full") * volts : -1;
            }
            if (now >= 0 && (!charging || full >= now))
                seconds = (charging ? full - now : now) / 1e6 / watts * 3600;
        }
        if (sys.batteryStatus == QLatin1String("Full") || sys.batteryStatus == QLatin1String("Not charging"))
            seconds = -1;
        sys.batterySeconds = seconds;
        break;
    }

    // ---- disks: /proc/diskstats ----
    procfs::DiskStats disk;
    if (procfs::readFile(m_proc + QLatin1String("/diskstats"), m_buf) && procfs::parseDiskStats(m_buf, disk)) {
        sys.diskKnown = true;
        if (m_havePrevDisk && elapsed > 0 && disk.readBytes >= m_prevDisk.readBytes && disk.writeBytes >= m_prevDisk.writeBytes) {
            sys.diskReadsPerSec = double(disk.reads - m_prevDisk.reads) / elapsed;
            sys.diskWritesPerSec = double(disk.writes - m_prevDisk.writes) / elapsed;
            sys.diskReadBytesPerSec = double(disk.readBytes - m_prevDisk.readBytes) / elapsed;
            sys.diskWriteBytesPerSec = double(disk.writeBytes - m_prevDisk.writeBytes) / elapsed;
        }
        m_prevDisk = disk;
        m_havePrevDisk = true;
    }
    // Free space changes slowly, and asking can block on a slow device.
    if (m_round % 5 == 1 && m_proc == QLatin1String("/proc") && procfs::readFile(m_proc + QLatin1String("/mounts"), m_buf)) {
        m_mounts.clear();
        QSet<QByteArray> seen;
        const QList<QByteArray> lines = m_buf.split('\n');
        for (const QByteArray &line : lines) {
            const QList<QByteArray> f = line.split(' ');
            if (f.size() < 3 || !f[0].startsWith("/dev/") || seen.contains(f[0]) || f[0].startsWith("/dev/loop"))
                continue;
            seen.insert(f[0]);
            QByteArray path = f[1];
            path.replace("\\040", " ");
            struct statvfs vfs;
            if (statvfs(path.constData(), &vfs) != 0 || vfs.f_blocks == 0)
                continue;
            m_mounts.append({QString::fromUtf8(path), qint64(quint64(vfs.f_blocks) * vfs.f_frsize / 1024),
                             qint64(quint64(vfs.f_bavail) * vfs.f_frsize / 1024)});
        }
    }
    sys.mounts = m_mounts;

    // ---- network: /sys/class/net/*/statistics ----
    const QDir netDir(m_sys + QLatin1String("/class/net"));
    const QStringList interfaces = netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    quint64 rx = 0, tx = 0, busiest = 0;
    for (const QString &name : interfaces) {
        if (name == QLatin1String("lo"))
            continue;
        const QString stats = netDir.filePath(name) + QLatin1String("/statistics/");
        if (!procfs::readFile(stats + QLatin1String("rx_bytes"), m_buf))
            continue;
        const quint64 r = m_buf.trimmed().toULongLong();
        const quint64 t = procfs::readFile(stats + QLatin1String("tx_bytes"), m_buf) ? m_buf.trimmed().toULongLong() : 0;
        rx += r;
        tx += t;
        sys.netKnown = true;
        if (r + t >= busiest) {
            busiest = r + t;
            sys.netInterface = name;
        }
    }
    if (sys.netKnown) {
        sys.netRxBytes = rx;
        sys.netTxBytes = tx;
        if (m_havePrevNet && elapsed > 0 && rx >= m_prevRx && tx >= m_prevTx) {
            sys.netRxBytesPerSec = double(rx - m_prevRx) / elapsed;
            sys.netTxBytesPerSec = double(tx - m_prevTx) / elapsed;
        }
        m_prevRx = rx;
        m_prevTx = tx;
        m_havePrevNet = true;
    }
}
