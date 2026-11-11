// Unit tests. The collector runs against the recorded /proc tree in
// tests/fixtures (see tests/make-fixture for what is recorded and what is
// chosen); the action tests signal processes they start themselves.
#include "actions.h"
#include "apps.h"
#include "collector.h"
#include "inspector.h"
#include "painteditems.h"
#include "prefs.h"
#include "processmodel.h"
#include "procfs.h"
#include "protectedlist.h"
#include "sampler.h"
#include "systeminfo.h"
#include "theme.h"

#include <QAbstractItemModelTester>
#include <QDirIterator>
#include <QPointingDevice>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <csignal>
#include <sys/resource.h>
#include <unistd.h>

namespace {

const QString kFixtures = QStringLiteral(FIXTURES);

void copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    QDirIterator it(from, QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        const QString dst = to + src.mid(from.size());
        const QFileInfo info(src);
        if (info.isSymLink()) {
            char buf[4096];
            const ssize_t n = ::readlink(QFile::encodeName(src).constData(), buf, sizeof buf - 1);
            QVERIFY(n > 0);
            buf[n] = 0;
            QFile::remove(dst);
            QCOMPARE(::symlink(buf, QFile::encodeName(dst).constData()), 0);
        } else if (info.isDir()) {
            QDir().mkpath(dst);
        } else {
            QFile::remove(dst);
            QVERIFY2(QFile::copy(src, dst), qPrintable(src));
        }
    }
}

// A private copy of the recorded tree that can be stepped to the second frame.
struct Tree {
    QTemporaryDir dir;
    QString proc() const { return dir.path() + QStringLiteral("/proc"); }
    QString sys() const { return dir.path() + QStringLiteral("/sys"); }
    Tree() { copyTree(kFixtures + QStringLiteral("/tree"), dir.path()); }
    void step()
    {
        copyTree(kFixtures + QStringLiteral("/next/proc"), proc());
        QFile removed(kFixtures + QStringLiteral("/next/removed"));
        QVERIFY(removed.open(QIODevice::ReadOnly));
        const QList<QByteArray> pids = removed.readAll().split('\n');
        for (const QByteArray &pid : pids) {
            if (!pid.trimmed().isEmpty())
                QDir(proc() + QLatin1Char('/') + QString::fromLatin1(pid.trimmed())).removeRecursively();
        }
    }
};

const ProcSample *find(const SnapshotPtr &snap, int pid)
{
    for (const ProcSample &p : snap->procs) {
        if (p.pid == pid)
            return &p;
    }
    return nullptr;
}

QStringList names(const ProcessModel &model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out << model.data(model.index(i), ProcessModel::NameRole).toString();
    return out;
}

int rowOf(const ProcessModel &model, const QString &name)
{
    return names(model).indexOf(name);
}

QString cellOf(const ProcessModel &model, int row, const QString &column)
{
    const QVariantList cols = model.columns();
    for (int i = 0; i < cols.size(); ++i) {
        if (cols[i].toMap().value(QStringLiteral("key")) == column)
            return model.data(model.index(row), ProcessModel::CellsRole).toStringList().value(i);
    }
    return QStringLiteral("<no such column>");
}

} // namespace

class TestOmactivity : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Settings written by the model must not touch the real ones.
        QCoreApplication::setOrganizationName(QStringLiteral("omactivity-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("omactivity-tests"));
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
        qRegisterMetaType<SnapshotPtr>("SnapshotPtr");
    }
    void init() { QSettings().clear(); }

    // ------------------------------------------------------------ parsing
    void statWithAwkwardNames()
    {
        procfs::Stat st;
        // A name with spaces and a closing parenthesis.
        QVERIFY(procfs::parseStat("4242 (Web (Content) x) R 17 4242 4242 0 -1 4194304 10 0 3 0 250 50 0 0 20 5 12 0 9000 1048576 300 1", st));
        QCOMPARE(st.pid, 4242);
        QCOMPARE(st.comm, QByteArray("Web (Content) x"));
        QCOMPARE(st.state, 'R');
        QCOMPARE(st.ppid, 17);
        QCOMPARE(st.utime + st.stime, quint64(300));
        QCOMPARE(st.nice, 5);
        QCOMPARE(st.threads, 12);
        QCOMPARE(st.starttime, quint64(9000));
        QCOMPARE(st.vsizeBytes, quint64(1048576));
        QCOMPARE(st.rssPages, qint64(300));
        QCOMPARE(st.minflt, quint64(10));
        QCOMPARE(st.majflt, quint64(3));

        // Kernel threads carry names longer than the 15 characters user
        // processes are cut to; the recording has several.
        QByteArray data;
        QDir proc(kFixtures + QStringLiteral("/tree/proc"));
        bool sawLong = false;
        const QStringList pids = proc.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);
        QVERIFY(pids.size() > 150);
        for (const QString &pid : pids) {
            QVERIFY(procfs::readFile(proc.filePath(pid + QStringLiteral("/stat")), data));
            QVERIFY2(procfs::parseStat(data, st), qPrintable(pid));
            QCOMPARE(st.pid, pid.toInt());
            sawLong = sawLong || (st.comm.size() > 15 && st.kernelThread());
        }
        QVERIFY(sawLong);
        QVERIFY(!procfs::parseStat("garbage", st));
    }

    void statusSmapsIo()
    {
        QByteArray data;
        const QString base = kFixtures + QStringLiteral("/tree/proc/1/");
        procfs::Status status;
        QVERIFY(procfs::readFile(base + QStringLiteral("status"), data));
        QVERIFY(procfs::parseStatus(data, status));
        QCOMPARE(status.uid, 0u);
        QVERIFY(status.vmRssKb > 1000);
        QVERIFY(status.voluntaryCtxt > 0);

        procfs::Smaps smaps;
        QVERIFY(procfs::readFile(base + QStringLiteral("smaps_rollup"), data));
        QVERIFY(procfs::parseSmapsRollup(data, smaps));
        QVERIFY(smaps.pssKb > 0);
        // "Pss:" must not be confused with Pss_Dirty, Pss_Anon and friends.
        QVERIFY(data.contains("Pss_Dirty:"));
        QCOMPARE(smaps.pssKb, QByteArray(data.mid(data.indexOf("\nPss:") + 5, 20)).trimmed().split(' ').first().toLongLong());
        QVERIFY(smaps.privateKb > 0);

        procfs::Io io;
        QVERIFY(procfs::readFile(base + QStringLiteral("io"), data));
        QVERIFY(procfs::parseIo(data, io));
        QVERIFY(io.readBytes > 0);

        int err = 0;
        QVERIFY(!procfs::readFile(base + QStringLiteral("no-such-file"), data, &err));
        QCOMPARE(err, ENOENT);
    }

    void cgroupToApp()
    {
        const QString cg = procfs::parseCgroup("0::/user.slice/user-1000.slice/user@1000.service/app.slice/app-firefox@3f1a.scope\n");
        QCOMPARE(procfs::appUnitFromCgroup(cg), QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(procfs::appIdCandidates(QStringLiteral("app-firefox@3f1a.scope")).first(), QStringLiteral("firefox"));
        // The systemd naming with a random suffix, and with a launcher prefix.
        QVERIFY(procfs::appIdCandidates(QStringLiteral("app-org.kde.dolphin-8f3e2a.scope")).contains(QStringLiteral("org.kde.dolphin")));
        QVERIFY(procfs::appIdCandidates(QStringLiteral("app-Hyprland-firefox-8f3e2a.scope")).contains(QStringLiteral("firefox")));
        QVERIFY(procfs::appIdCandidates(QStringLiteral("app-org.gnome.Foo\\x2dbar@1.service")).contains(QStringLiteral("org.gnome.Foo-bar")));
        // Not apps: system services, the session scope, user services.
        QCOMPARE(procfs::appUnitFromCgroup(QStringLiteral("/system.slice/app-thing@1.service")), QString());
        QCOMPARE(procfs::appUnitFromCgroup(QStringLiteral("/user.slice/user-1000.slice/session-1.scope")), QString());
        QCOMPARE(procfs::appUnitFromCgroup(QStringLiteral("/user.slice/user-1000.slice/user@1000.service/app.slice/dbus-broker.service")), QString());
        // cgroup v1 keeps the systemd hierarchy under name=systemd.
        QCOMPARE(procfs::parseCgroup("12:pids:/x\n1:name=systemd:/user.slice/a.scope\n"), QStringLiteral("/user.slice/a.scope"));
    }

    void systemFiles()
    {
        QByteArray data;
        const QString base = kFixtures + QStringLiteral("/tree/proc/");
        procfs::SystemStat stat;
        QVERIFY(procfs::readFile(base + QStringLiteral("stat"), data));
        QVERIFY(procfs::parseSystemStat(data, stat));
        QVERIFY(stat.cores.size() >= 2);
        QVERIFY(stat.bootTime > 1600000000);
        quint64 sum = 0;
        for (const procfs::CpuTimes &c : stat.cores)
            sum += c.total();
        QVERIFY(qAbs(qint64(sum) - qint64(stat.all.total())) < qint64(stat.cores.size()) * 20);

        procfs::MemInfo mem;
        QVERIFY(procfs::readFile(base + QStringLiteral("meminfo"), data));
        QVERIFY(procfs::parseMemInfo(data, mem));
        QVERIFY(mem.totalKb > mem.availableKb && mem.availableKb > 0);

        procfs::Pressure pressure;
        QVERIFY(procfs::readFile(base + QStringLiteral("pressure/memory"), data));
        QVERIFY(procfs::parsePressure(data, pressure));
        QCOMPARE(pressure.some10, 4.2);
        QCOMPARE(pressure.some60, 3.1);
        QCOMPARE(pressure.some300, 2.05);
        QCOMPARE(pressure.full10, 0.6);

        procfs::LoadAvg load;
        QVERIFY(procfs::readFile(base + QStringLiteral("loadavg"), data));
        QVERIFY(procfs::parseLoadAvg(data, load));
        QVERIFY(load.l1 >= 0);
    }

    void socketTables()
    {
        QHash<quint64, procfs::SocketInfo> sockets;
        procfs::parseNetSockets("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                                "   0: 00000000:0016 00000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 14199 1 0 100 0 0 10 0\n"
                                "   1: 027FA8C0:0016 017FA8C0:B5EA 01 00000000:00000000 02:000720F5 00000000     0        0 14247 2 0 20 4 24 10 -1\n",
                                QStringLiteral("TCP"), sockets);
        QCOMPARE(sockets.value(14199).local, QStringLiteral("0.0.0.0:22"));
        QCOMPARE(sockets.value(14199).remote, QStringLiteral("0.0.0.0:*"));
        QCOMPARE(sockets.value(14247).local, QStringLiteral("192.168.127.2:22"));
        QCOMPARE(sockets.value(14247).remote, QStringLiteral("192.168.127.1:46570"));
        procfs::parseNetSockets("  sl  local_address                         remote_address                        st\n"
                                "   0: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 555 1 0 100 0 0 10 0\n"
                                "   1: 0000000000000000FFFF00000100007F:1F90 0000000000000000FFFF00000100007F:C000 01 00000000:00000000 00:00000000 00000000     0        0 556 1 0 100 0 0 10 0\n"
                                "   2: B80D0120000000000000000001000000:01BB 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 557 1 0 100 0 0 10 0\n",
                                QStringLiteral("TCP"), sockets);
        QCOMPARE(sockets.value(555).local, QStringLiteral("[::]:22"));
        QCOMPARE(sockets.value(556).local, QStringLiteral("127.0.0.1:8080"));
        QCOMPARE(sockets.value(557).local, QStringLiteral("[2001:db8:0:0:0:0:0:1]:443"));
        procfs::parseNetUnix("Num       RefCount Protocol Flags    Type St Inode Path\n"
                             "0000000000000000: 00000002 00000000 00010000 0001 01 11841 /run/systemd/private\n"
                             "0000000000000000: 00000002 00000000 00000000 0002 03 214518\n",
                             sockets);
        QCOMPARE(sockets.value(11841).local, QStringLiteral("/run/systemd/private"));
        QCOMPARE(sockets.value(214518).proto, QStringLiteral("unix"));
    }

    // ------------------------------------------------------------ sampling
    void firstSampleIsComplete()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        const SnapshotPtr snap = sampler.sample(0);
        QCOMPARE(snap->interval, 0.0);
        QVERIFY(snap->procs.size() > 200);
        QCOMPARE(snap->sys.processes, snap->procs.size());

        const ProcSample *firefox = find(snap, 1204);
        QVERIFY(firefox);
        QCOMPARE(firefox->name, QStringLiteral("firefox"));
        QCOMPARE(firefox->uid, 1000u);
        QCOMPARE(firefox->ppid, 900);
        QCOMPARE(firefox->threads, 72);
        QCOMPARE(firefox->appUnit, QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(firefox->cmdline, QStringLiteral("/usr/lib/firefox/firefox"));
        QCOMPARE(firefox->cpuPercent, 0.0); // nothing to diff against yet
        QCOMPARE(firefox->cpuTicks, quint64(40820));
        QVERIFY(firefox->memIsRss); // smaps_rollup is not read unless asked
        QCOMPARE(firefox->memKb, firefox->rssKb);

        const ProcSample *init = find(snap, 1);
        QVERIFY(init && init->appUnit.isEmpty() && !init->kernelThread);
        const ProcSample *kthreadd = find(snap, 2);
        QVERIFY(kthreadd && kthreadd->kernelThread);
        QCOMPARE(find(snap, 2954)->state, 'T');

        // The kernel's own averages are there from the first sample on.
        QVERIFY(snap->sys.uptimeSeconds > 1000);
        QVERIFY(snap->sys.pressure.available);
        QCOMPARE(snap->sys.pressure.some10, 4.2);
        QVERIFY(snap->sys.memTotalKb > 0);
        QCOMPARE(snap->sys.frequencyKhz, 2340000.0);
        QCOMPARE(snap->sys.compressedKb, qint64(98881536 / 1024));
        QVERIFY(snap->sys.userPercent + snap->sys.systemPercent + snap->sys.idlePercent > 99.0);
    }

    void secondSampleDiffs()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        sampler.sample(0);
        tree.step();
        const SnapshotPtr snap = sampler.sample(2.0);
        QCOMPARE(snap->interval, 2.0);
        // 39 ticks of 1/100 s in 2 s.
        QCOMPARE(find(snap, 1688)->cpuPercent, 19.5);
        QCOMPARE(find(snap, 1204)->cpuPercent, 12.5);
        QCOMPARE(find(snap, 1688)->wakeups, 214.0);
        QCOMPARE(find(snap, 2954)->cpuPercent, 0.0);
        QVERIFY(!find(snap, 3100)); // ended between the frames
        const ProcSample *fresh = find(snap, 3200);
        QVERIFY(fresh);
        QCOMPARE(fresh->cpuPercent, 0.0); // new: no previous counters
        QCOMPARE(fresh->appUnit, QStringLiteral("app-foot@77c2.scope"));
        QCOMPARE(snap->sys.pressure.some10, 6.8);
        QVERIFY(qAbs(snap->sys.userPercent + snap->sys.systemPercent + snap->sys.idlePercent - 100.0) < 0.01);
        QCOMPARE(snap->sys.corePercent.size(), 10);
    }

    void processVanishesMidRead()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        const int before = sampler.sample(0)->procs.size();
        // The directory is still listed but its files are gone, as when a
        // process ends between readdir and open.
        QVERIFY(QFile::remove(tree.proc() + QStringLiteral("/1752/stat")));
        // status disappears after stat was read.
        QVERIFY(QFile::remove(tree.proc() + QStringLiteral("/1390/status")));
        const SnapshotPtr snap = sampler.sample(2.0);
        QCOMPARE(snap->procs.size(), before - 1);
        QVERIFY(!find(snap, 1752));
        const ProcSample *rdd = find(snap, 1390);
        QVERIFY(rdd);
        QCOMPARE(rdd->uid, 1000u); // kept from the earlier sample
        QVERIFY(rdd->rssKb > 0);   // from stat, when status is gone
    }

    void pidReuseStartsFresh()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        sampler.sample(0);
        // Same PID, later start time, fewer ticks: a different process.
        QFile f(tree.proc() + QStringLiteral("/2301/stat"));
        QVERIFY(f.open(QIODevice::ReadWrite));
        QList<QByteArray> fields = f.readAll().split(' ');
        fields[13] = "1";
        fields[14] = "1";
        fields[21] = QByteArray::number(fields[21].toLongLong() + 5000);
        f.resize(0);
        f.seek(0);
        f.write(fields.join(' '));
        f.close();
        const SnapshotPtr snap = sampler.sample(2.0);
        QCOMPARE(find(snap, 2301)->cpuPercent, 0.0);
        QCOMPARE(find(snap, 2301)->cpuTicks, quint64(2));
    }

    void memoryMatchesSmapsByHand()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        sampler.setMemoryDetail(true);
        // One process whose smaps_rollup cannot be read falls back to RSS.
        QVERIFY(QFile::remove(tree.proc() + QStringLiteral("/1840/smaps_rollup")));
        const SnapshotPtr snap = sampler.sample(0);

        // By hand: Pss from every smaps_rollup in the tree.
        qint64 topPss = 0;
        int topPid = 0;
        QDir proc(tree.proc());
        const QStringList pidDirs = proc.entryList({QStringLiteral("[0-9]*")}, QDir::Dirs);
        for (const QString &pid : pidDirs) {
            QFile f(proc.filePath(pid + QStringLiteral("/smaps_rollup")));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QList<QByteArray> lines = f.readAll().split('\n');
            for (const QByteArray &line : lines) {
                if (!line.startsWith("Pss:"))
                    continue;
                const qint64 pss = line.mid(4).trimmed().split(' ').first().toLongLong();
                if (pss > topPss) {
                    topPss = pss;
                    topPid = pid.toInt();
                }
            }
        }
        QCOMPARE(topPid, 1688);
        QCOMPARE(find(snap, 1688)->memKb, topPss);
        QVERIFY(!find(snap, 1688)->memIsRss);
        QCOMPARE(find(snap, 1688)->swapKb, qint64(21914));
        QVERIFY(find(snap, 1840)->memIsRss);
        QCOMPARE(find(snap, 1840)->memKb, find(snap, 1840)->rssKb);
        QCOMPARE(find(snap, 1840)->swapKb, qint64(-1));

        // The list sorted by Memory has the same process on top.
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        model.setTab(QStringLiteral("memory"));
        model.setView(QStringLiteral("all"));
        model.setSnapshot(snap);
        QCOMPARE(model.sortKey(), QStringLiteral("memory"));
        QCOMPARE(model.data(model.index(0), ProcessModel::KeyRole).toString(), ProcessModel::processKey(*find(snap, 1688)));
        QCOMPARE(cellOf(model, 0, QStringLiteral("memory")), QStringLiteral("312.6 MB"));
    }

    // What htop, lsof and systemctl answer: held up by what, listening
    // where, running as which unit. The recording has none of the three
    // inputs for the session's processes, so the test writes them.
    void waitingPortsAndUnits()
    {
        Tree tree;
        auto put = [&](const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(tree.proc() + path).path());
            QFile f(tree.proc() + path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(data);
        };
        auto setBlkio = [&](int pid, int ticks) {
            QFile f(tree.proc() + QStringLiteral("/%1/stat").arg(pid));
            QVERIFY(f.open(QIODevice::ReadWrite));
            QByteArray data = f.readAll().trimmed();
            const int close = data.lastIndexOf(')');
            QList<QByteArray> fields = data.mid(close + 2).split(' ');
            fields[39] = QByteArray::number(ticks); // field 42, delayacct_blkio_ticks
            f.resize(0);
            f.seek(0);
            f.write(data.left(close + 2) + fields.join(' ') + "\n");
        };
        put(QStringLiteral("/1688/schedstat"), "5000000000 1000000000 4000\n");
        setBlkio(1390, 100);

        Sampler sampler(tree.proc(), tree.sys());
        sampler.setPortDetail(true);
        SnapshotPtr snap = sampler.sample(0);
        // The kernel switch for disk-wait accounting is off (absent here):
        // the figure is "not measured", never a made-up zero.
        QCOMPARE(find(snap, 1390)->ioWaitPercent, -1.0);
        // firefox holds a listening TCP socket (port 22 in the recorded
        // table) and a connected one, which is not a listener.
        QCOMPARE(find(snap, 1204)->ports, QStringLiteral("22"));
        QVERIFY(find(snap, 1204)->portsKnown);
        QVERIFY(!find(snap, 1688)->portsKnown); // no descriptor table to read: unknown, not "none"

        tree.step();
        put(QStringLiteral("/1688/schedstat"), "5400000000 1500000000 4100\n"); // 0.5 s runnable without a core
        put(QStringLiteral("/sys/kernel/task_delayacct"), "1\n");
        setBlkio(1390, 160); // 0.6 s blocked on disk
        sampler.sample(2.0); // the switch is seen from here on
        setBlkio(1390, 220);
        put(QStringLiteral("/1688/schedstat"), "5800000000 2000000000 4200\n");
        snap = sampler.sample(2.0);
        QCOMPARE(find(snap, 1688)->cpuWaitPercent, 25.0);
        QCOMPARE(find(snap, 1390)->ioWaitPercent, 30.0);
        QCOMPARE(find(snap, 1204)->ioWaitPercent, 0.0);

        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy wants(&model, &ProcessModel::wantsPortsChanged);
        model.setView(QStringLiteral("all"));
        model.setSnapshot(snap);
        QVERIFY(!model.wantsPorts());

        // A process held up a lot says so in the list, without any column.
        QCOMPARE(model.data(model.index(model.indexOfKey(model.keyForPid(1688))), ProcessModel::BlockedRole).toString(), QStringLiteral("cpu"));
        QCOMPARE(model.data(model.index(model.indexOfKey(model.keyForPid(1390))), ProcessModel::BlockedRole).toString(), QStringLiteral("io"));
        QCOMPARE(model.data(model.index(model.indexOfKey(model.keyForPid(1204))), ProcessModel::BlockedRole).toString(), QString());
        model.setColumnVisible(QStringLiteral("cpuWait"), true);
        model.setColumnVisible(QStringLiteral("ioWait"), true);
        model.setColumnVisible(QStringLiteral("state"), true);
        model.setColumnVisible(QStringLiteral("command"), true);
        const int web = model.indexOfKey(model.keyForPid(1688));
        QCOMPARE(cellOf(model, web, QStringLiteral("cpuWait")), QStringLiteral("25.0"));
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1390)), QStringLiteral("ioWait")), QStringLiteral("30.0"));
        QCOMPARE(cellOf(model, web, QStringLiteral("state")), QStringLiteral("Sleeping"));
        // The full command line, not the 15 characters of the name.
        QVERIFY(cellOf(model, web, QStringLiteral("command")).endsWith(QStringLiteral("-jsInitLen 231800 {5c1e} 1204 tab")));
        model.sortBy(QStringLiteral("cpuWait"));
        QCOMPARE(model.data(model.index(0), ProcessModel::KeyRole).toString(), model.keyForPid(1688));

        // Who listens on a port: a view, and a search by port number.
        model.setView(QStringLiteral("ports"));
        QVERIFY(model.wantsPorts());
        QCOMPARE(wants.count(), 1);
        QCOMPARE(names(model), QStringList({"firefox"}));
        QCOMPARE(model.columns().first().toMap().value("key").toString(), QStringLiteral("ports"));
        // The recorded machine's other listeners belong to processes whose
        // descriptors were not recorded: named by owner, not guessed at.
        QVERIFY2(model.otherPorts().startsWith(QStringLiteral("root: ")), qPrintable(model.otherPorts()));

        QCOMPARE(cellOf(model, 0, QStringLiteral("ports")), QStringLiteral("22"));
        model.setView(QStringLiteral("all"));
        QVERIFY(!model.wantsPorts());
        model.setColumnVisible(QStringLiteral("ports"), true);
        QVERIFY(model.wantsPorts());
        model.setSearch(QStringLiteral(":22"));
        QCOMPARE(names(model), QStringList({"firefox"}));
        model.setSearch(QStringLiteral("2")); // a port is matched whole, not by digit
        QVERIFY(!names(model).contains(QStringLiteral("firefox")) || model.rowCount() > 1);
        model.setSearch(QString());
        // Someone else's process: unknown, shown as a dash.
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1)), QStringLiteral("ports")), QString()); // PID 1 was readable in the recording

        // By service: one row per systemd unit.
        QCOMPARE(ProcessModel::unitOf(QStringLiteral("/system.slice/NetworkManager.service")), QStringLiteral("NetworkManager.service"));
        QCOMPARE(ProcessModel::unitOf(QStringLiteral("/user.slice/user-1000.slice/user@1000.service/app.slice/app-firefox@3f1a.scope")),
                 QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(ProcessModel::unitOf(QStringLiteral("/")), QString());
        model.setView(QStringLiteral("services"));
        const QStringList units = names(model);
        for (const char *unit : {"NetworkManager.service", "app-firefox@3f1a.scope", "session-1.scope", "Kernel", "quickshell.service"})
            QVERIFY2(units.contains(QLatin1String(unit)), unit);
        const int fx = rowOf(model, QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(model.data(model.index(fx), ProcessModel::CountRole).toInt(), 6);
        model.toggleExpanded(model.data(model.index(fx), ProcessModel::KeyRole).toString());
        QCOMPARE(model.data(model.index(fx + 1), ProcessModel::DepthRole).toInt(), 1);
        // The inspect panel says "no app" for what is not one, and names the unit.
        Inspector inspector(&model, tree.proc());
        inspector.open(model.keyForPid(905));
        QCOMPARE(inspector.info().value("cgroup").toString(), QStringLiteral("—"));
        QCOMPARE(inspector.info().value("unit").toString(), QStringLiteral("session-1.scope"));
        inspector.open(model.keyForPid(1688));
        QCOMPARE(inspector.info().value("cgroup").toString(), QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(inspector.info().value("cpuWait").toString(), QStringLiteral("25.0%"));
    }

    // The Energy, Disk and Network tabs. The recording has no battery, disk
    // or network counters, so the test writes them in the kernel's formats.
    void energyDiskNetwork()
    {
        Tree tree;
        auto put = [&](const QString &root, const QString &path, const QByteArray &data) {
            QDir().mkpath(QFileInfo(root + path).path());
            QFile f(root + path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(data);
        };
        auto battery = [&](const char *file, const QByteArray &v) { put(tree.sys(), QStringLiteral("/class/power_supply/battery/") + QLatin1String(file), v + "\n"); };
        // A charger, and a battery whose driver gives current and voltage
        // (no power_now, no time estimate), with the current signed.
        put(tree.sys(), QStringLiteral("/class/power_supply/usb/type"), "USB\n");
        battery("type", "Battery");
        battery("capacity", "61");
        battery("status", "Discharging");
        battery("current_now", "-800000");  // 0.8 A
        battery("voltage_now", "3800000");  // 3.8 V
        battery("charge_now", "4000000");   // 4 Ah
        battery("charge_full", "8000000");
        auto disk = [&](quint64 reads, quint64 sectorsRead, quint64 writes, quint64 sectorsWritten) {
            put(tree.proc(), QStringLiteral("/diskstats"),
                QStringLiteral(" 259       0 nvme0n1 %1 0 %2 100 %3 0 %4 200 0 300 300 0 0 0 0\n"
                               " 259       1 nvme0n1p1 %1 0 %2 100 %3 0 %4 200 0 300 300 0 0 0 0\n"
                               " 252       0 zram0 500 0 4000 10 600 0 4800 10 0 20 20 0 0 0 0\n"
                               "   7       0 loop0 50 0 400 1 0 0 0 0 0 1 1 0 0 0 0\n")
                    .arg(reads).arg(sectorsRead).arg(writes).arg(sectorsWritten).toLatin1());
        };
        auto net = [&](const char *iface, quint64 rx, quint64 tx) {
            put(tree.sys(), QStringLiteral("/class/net/%1/statistics/rx_bytes").arg(QLatin1String(iface)), QByteArray::number(rx) + "\n");
            put(tree.sys(), QStringLiteral("/class/net/%1/statistics/tx_bytes").arg(QLatin1String(iface)), QByteArray::number(tx) + "\n");
        };
        auto io = [&](int pid, quint64 read, quint64 written) {
            put(tree.proc(), QStringLiteral("/%1/io").arg(pid),
                "rchar: 1\nwchar: 1\nsyscr: 1\nsyscw: 1\nread_bytes: " + QByteArray::number(read) + "\nwrite_bytes: "
                    + QByteArray::number(written) + "\ncancelled_write_bytes: 0\n");
        };
        disk(1000, 80000, 2000, 160000);
        net("lo", 999999999, 999999999);
        net("wlan0", 5000000, 1000000);
        io(1204, 10 * 1024 * 1024, 40 * 1024 * 1024);
        QFile::remove(tree.proc() + QStringLiteral("/1840/io")); // one whose counters cannot be read

        Sampler sampler(tree.proc(), tree.sys());
        sampler.setIoDetail(true);
        sampler.setPortDetail(true);
        SnapshotPtr snap = sampler.sample(0);
        QVERIFY(snap->sys.hasBattery);
        QCOMPARE(snap->sys.batteryPercent, 61);
        QCOMPARE(snap->sys.batteryStatus, QStringLiteral("Discharging"));
        QCOMPARE(snap->sys.powerWatts, 0.8 * 3.8);
        QCOMPARE(qRound(snap->sys.batterySeconds), 5 * 3600); // 4 Ah at 0.8 A
        QCOMPARE(find(snap, 1204)->writeBytes, qint64(40) * 1024 * 1024);
        QCOMPARE(find(snap, 1840)->writeBytes, qint64(-1));
        // firefox's table in the recording: 4 sockets, one of them a TCP connection.
        QCOMPARE(find(snap, 1204)->sockets, 4);
        QCOMPARE(find(snap, 1204)->connections, 1);
        QCOMPARE(find(snap, 1688)->sockets, -1);

        tree.step();
        disk(1100, 80000 + 4096, 2300, 160000 + 8192); // 2 MiB read, 4 MiB written, in 2 s
        net("wlan0", 5000000 + 600000, 1000000 + 50000);
        io(1204, 10 * 1024 * 1024 + 4096, 40 * 1024 * 1024 + 3 * 1024 * 1024);
        battery("status", "Charging");
        snap = sampler.sample(2.0);
        QCOMPARE(snap->sys.diskReadsPerSec, 50.0);
        QCOMPARE(snap->sys.diskWritesPerSec, 150.0);
        QCOMPARE(snap->sys.diskReadBytesPerSec, 1024.0 * 1024);  // the partition, zram and loop lines are not counted
        QCOMPARE(snap->sys.diskWriteBytesPerSec, 2048.0 * 1024);
        QCOMPARE(snap->sys.netRxBytesPerSec, 300000.0);          // loopback is not counted
        QCOMPARE(snap->sys.netTxBytesPerSec, 25000.0);
        QCOMPARE(snap->sys.netInterface, QStringLiteral("wlan0"));
        QCOMPARE(find(snap, 1204)->writeRate, 1.5 * 1024 * 1024);
        QCOMPARE(find(snap, 1204)->readRate, 2048.0);
        QCOMPARE(qRound(snap->sys.batterySeconds), 5 * 3600);    // charging: 4 Ah to go at 0.8 A

        SystemInfo info;
        info.setSnapshot(snap);
        QCOMPARE(info.property("batteryLevel").toString(), QStringLiteral("61%"));
        QCOMPARE(info.property("batteryTimeLabel").toString(), QStringLiteral("Time until full"));
        QCOMPARE(info.property("batteryTime").toString(), QStringLiteral("5:00"));
        QCOMPARE(info.property("powerDraw").toString(), QStringLiteral("3.04 W"));
        QCOMPARE(info.property("diskWriteRate").toString(), QStringLiteral("2.0 MB/s"));
        QCOMPARE(info.property("netRxRate").toString(), QStringLiteral("292 KB/s"));
        QCOMPARE(info.property("netRxTotal").toString(), QStringLiteral("5.3 MB"));

        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        auto keys = [&] {
            QStringList k;
            for (const QVariant &c : model.columns())
                k << c.toMap().value(QStringLiteral("key")).toString();
            return k;
        };
        model.setView(QStringLiteral("mine")); // the recorded system services have written more than the session
        model.setSnapshot(snap);

        model.setTab(QStringLiteral("disk"));
        QCOMPARE(keys(), QStringList({"written", "read", "writeRate", "readRate", "pid", "user"}));
        QCOMPARE(model.sortKey(), QStringLiteral("written"));
        QCOMPARE(model.data(model.index(0), ProcessModel::KeyRole).toString(), model.keyForPid(1204));
        QCOMPARE(cellOf(model, 0, QStringLiteral("written")), QStringLiteral("43.0 MB"));
        QCOMPARE(cellOf(model, 0, QStringLiteral("writeRate")), QStringLiteral("1.5 MB/s"));
        QCOMPARE(cellOf(model, 0, QStringLiteral("readRate")), QStringLiteral("2 KB/s"));
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1840)), QStringLiteral("written")), QStringLiteral("—"));

        model.setTab(QStringLiteral("network"));
        QVERIFY(model.wantsPorts()); // the tab needs every process's sockets looked at
        QCOMPARE(keys(), QStringList({"connections", "sockets", "pid", "user", "ports"}));
        QCOMPARE(model.data(model.index(0), ProcessModel::KeyRole).toString(), model.keyForPid(1204));
        QCOMPARE(cellOf(model, 0, QStringLiteral("connections")), QStringLiteral("1"));
        QCOMPARE(cellOf(model, 0, QStringLiteral("sockets")), QStringLiteral("4"));
        QCOMPARE(cellOf(model, 0, QStringLiteral("ports")), QStringLiteral("22"));
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1688)), QStringLiteral("sockets")), QStringLiteral("—"));

        model.setTab(QStringLiteral("energy"));
        QVERIFY(!model.wantsPorts());
        QCOMPARE(keys(), QStringList({"cpu", "avgCpu", "wakeups", "sleep", "pid", "user"}));
        // Average since it started: 408.45 s of CPU in 1741 s of life.
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1204)), QStringLiteral("avgCpu")), QStringLiteral("23.5"));
        const int dolphin = model.indexOfKey(model.keyForPid(1840));
        QCOMPARE(cellOf(model, dolphin, QStringLiteral("sleep")), QString());
        model.setSleepBlockers({{1840, QStringLiteral("Copying files")}});
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1840)), QStringLiteral("sleep")), QStringLiteral("Yes"));
        model.sortBy(QStringLiteral("sleep"));
        QCOMPARE(model.data(model.index(0), ProcessModel::KeyRole).toString(), model.keyForPid(1840));
        Inspector inspector(&model, tree.proc());
        inspector.open(model.keyForPid(1840));
        QCOMPARE(inspector.info().value("sleepBlock").toString(), QStringLiteral("Copying files"));
        // In the apps view the app's row says so too.
        model.setView(QStringLiteral("apps"));
        QCOMPARE(cellOf(model, rowOf(model, QStringLiteral("Dolphin")), QStringLiteral("sleep")), QStringLiteral("Yes"));

        // While searching, the subtitle counts matches.
        model.setSearch(QStringLiteral("firefox"));
        QCOMPARE(model.matchCount(), 6);

        // No battery at all: said plainly.
        QDir(tree.sys() + QStringLiteral("/class/power_supply/battery")).removeRecursively();
        info.setSnapshot(sampler.sample(2.0));
        QCOMPARE(info.property("batteryState").toString(), QStringLiteral("No battery"));
        QCOMPARE(info.property("powerDraw").toString(), QStringLiteral("—"));
    }

    // ------------------------------------------------------------ the list
    void appsView()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        prot.load(QStringLiteral(DATA_DIR "/protected.conf"));
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        sampler.sample(0);
        tree.step();
        model.setSnapshot(sampler.sample(2.0));

        QCOMPARE(model.view(), QStringLiteral("apps"));
        const QStringList rows = names(model);
        // One row per app, named from its .desktop file, plus the two groups.
        for (const char *name : {"Firefox", "foot", "Dolphin", "Omacalc", "Omactivity", "System", "Background"})
            QVERIFY2(rows.contains(QLatin1String(name)), name);
        QCOMPARE(rows.size(), 7);
        QCOMPARE(rows.first(), QStringLiteral("Firefox")); // sorted by % CPU
        // The two groups keep their place under the apps whatever they use,
        // so they do not trade places at every refresh.
        QCOMPARE(rows.mid(5), QStringList({"Background", "System"}));
        model.sortBy(QStringLiteral("cpu")); // reversed: still last
        QCOMPARE(names(model).mid(5), QStringList({"Background", "System"}));
        model.sortBy(QStringLiteral("cpu"));

        const int firefox = rowOf(model, QStringLiteral("Firefox"));
        QCOMPARE(model.data(model.index(firefox), ProcessModel::KindRole).toInt(), int(ProcessModel::App));
        QCOMPARE(model.data(model.index(firefox), ProcessModel::CountRole).toInt(), 6);
        QCOMPARE(model.data(model.index(firefox), ProcessModel::GlyphRole).toString(), QStringLiteral("globe"));
        // Summed over its processes: 12.5 + 0.5 + 2 + 19.5 + 4 + 0.
        QCOMPARE(cellOf(model, firefox, QStringLiteral("cpu")), QStringLiteral("38.5"));
        QCOMPARE(cellOf(model, firefox, QStringLiteral("threads")), QStringLiteral("154"));
        // PID and user of the main process, the oldest in the scope.
        QCOMPARE(cellOf(model, firefox, QStringLiteral("pid")), QStringLiteral("1204"));

        const int system = rowOf(model, QStringLiteral("System"));
        QCOMPARE(model.data(model.index(system), ProcessModel::KindRole).toInt(), int(ProcessModel::Group));
        QCOMPARE(cellOf(model, system, QStringLiteral("pid")), QStringLiteral("—"));
        QCOMPARE(cellOf(model, system, QStringLiteral("user")), QStringLiteral("—"));
        QVERIFY(model.data(model.index(system), ProcessModel::CountRole).toInt() > 150);

        // A single-process app has nothing to expand; a paused one says so.
        const int omacalc = rowOf(model, QStringLiteral("Omacalc"));
        QVERIFY(!model.data(model.index(omacalc), ProcessModel::ExpandableRole).toBool());
        QVERIFY(model.data(model.index(omacalc), ProcessModel::PausedRole).toBool());

        // Expanding shows the processes, largest first.
        model.toggleExpanded(model.data(model.index(firefox), ProcessModel::KeyRole).toString());
        QCOMPARE(model.rowCount(), 13);
        QCOMPARE(names(model).mid(firefox + 1, 3), QStringList({"Isolated Web Co", "firefox", "Isolated Web Co"}));
        QCOMPARE(model.data(model.index(firefox + 1), ProcessModel::DepthRole).toInt(), 1);
        QCOMPARE(cellOf(model, firefox + 1, QStringLiteral("cpu")), QStringLiteral("19.5"));
        QCOMPARE(cellOf(model, firefox + 1, QStringLiteral("cpuTime")), QStringLiteral("4:51.76"));

        // labwc sits in the session scope, not an app scope: Background, and protected.
        model.reveal(model.keyForPid(905));
        const int labwc = rowOf(model, QStringLiteral("labwc"));
        QVERIFY(labwc > rowOf(model, QStringLiteral("Background")));
        QVERIFY(model.data(model.index(labwc), ProcessModel::ProtectedRole).toBool());
        QVERIFY(model.data(model.index(labwc), ProcessModel::SelectedRole).toBool());
    }

    void searchKeepsParentsInTree()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setSnapshot(sampler.sample(0));
        model.setView(QStringLiteral("tree"));
        const int all = model.rowCount();
        QVERIFY(all > 200);

        model.setSearch(QStringLiteral("FIRE")); // case-insensitive
        // PID 1 and systemd --user lead to the match and stay, though they
        // do not match themselves; firefox's children match by command line.
        // Nothing has used CPU yet, so siblings are in PID order.
        QCOMPARE(names(model), QStringList({"systemd", "systemd", "firefox", "Socket Process", "WebExtensions", "RDD Process",
                                            "Isolated Web Co", "Isolated Web Co"}));
        QList<int> depths;
        for (int i = 0; i < model.rowCount(); ++i)
            depths << model.data(model.index(i), ProcessModel::DepthRole).toInt();
        QCOMPARE(depths, QList<int>({0, 1, 2, 3, 3, 3, 3, 3}));

        // In the apps view the same search finds the app by name.
        model.setView(QStringLiteral("apps"));
        QCOMPARE(names(model), QStringList({"Firefox"}));
        model.setSearch(QStringLiteral("btop"));
        // A match inside a collapsed app opens it and shows only the match.
        QCOMPARE(names(model), QStringList({"foot", "btop"}));
        model.setSearch(QString());
        QCOMPARE(model.rowCount(), 7);
    }

    void treeAndFilters()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        sampler.sample(0);
        tree.step();
        const SnapshotPtr snap = sampler.sample(2.0);
        model.setSnapshot(snap);

        model.setView(QStringLiteral("all"));
        QCOMPARE(model.rowCount(), snap->procs.size());
        QCOMPARE(names(model).first(), QStringLiteral("Isolated Web Co"));

        model.setView(QStringLiteral("mine"));
        QCOMPARE(model.rowCount(), 21); // the session, minus the other user's sleep, plus wl-copy
        model.setView(QStringLiteral("others"));
        QCOMPARE(model.rowCount(), 0);  // uid 1001's sleep has ended
        model.setView(QStringLiteral("system"));
        QCOMPARE(model.rowCount(), snap->procs.size() - 21);
        model.setView(QStringLiteral("active"));
        for (int i = 0; i < model.rowCount(); ++i)
            QVERIFY(cellOf(model, i, QStringLiteral("cpu")).toDouble() > 0 || cellOf(model, i, QStringLiteral("cpu")) == QLatin1String("0.0"));
        QVERIFY(model.rowCount() >= 12 && model.rowCount() < 60);

        model.setWindowAppIds({QStringLiteral("firefox"), QStringLiteral("org.kde.dolphin")});
        model.setView(QStringLiteral("windowed"));
        QCOMPARE(model.rowCount(), 7); // six of Firefox, one Dolphin

        // Tree: children under their parent, siblings in sort order.
        model.setView(QStringLiteral("tree"));
        const int firefox = model.indexOfKey(model.keyForPid(1204));
        QVERIFY(firefox > 0);
        const int depth = model.data(model.index(firefox), ProcessModel::DepthRole).toInt();
        QCOMPARE(model.data(model.index(firefox + 1), ProcessModel::DepthRole).toInt(), depth + 1);
        QCOMPARE(model.data(model.index(firefox + 1), ProcessModel::KeyRole).toString(), model.keyForPid(1688));
        model.toggleExpanded(model.keyForPid(1204));
        QVERIFY(model.data(model.index(firefox + 1), ProcessModel::DepthRole).toInt() <= depth);
        model.toggleExpanded(model.keyForPid(1204));

        // Sorting: tapping a header sorts, tapping again reverses.
        model.setView(QStringLiteral("mine"));
        model.sortBy(QStringLiteral("threads"));
        QVERIFY(model.sortDescending());
        QCOMPARE(names(model).first(), QStringLiteral("firefox"));
        model.sortBy(QStringLiteral("threads"));
        QVERIFY(!model.sortDescending());
        QCOMPARE(cellOf(model, 0, QStringLiteral("threads")), QStringLiteral("1"));
        model.sortBy(QStringLiteral("name"));
        QVERIFY(!model.sortDescending());
        QCOMPARE(names(model).first(), QStringLiteral("bash"));
    }

    void stateSurvivesRefreshAndTabs()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        const SnapshotPtr first = sampler.sample(0);
        model.setSnapshot(first);
        model.setView(QStringLiteral("all"));
        model.setSearch(QStringLiteral("s"));
        const QString firefoxKey = model.keyForPid(1204);
        const QString sleepKey = model.keyForPid(3100);
        model.select(firefoxKey);
        model.toggleSelected(sleepKey);
        QCOMPARE(model.selectionCount(), 2);

        model.setTab(QStringLiteral("memory"));
        QCOMPARE(model.search(), QStringLiteral("s"));
        QCOMPARE(model.view(), QStringLiteral("all"));
        QCOMPARE(model.selectionCount(), 2);
        QCOMPARE(model.sortKey(), QStringLiteral("memory")); // each tab has its own sort
        QVERIFY(model.columns().first().toMap().value(QStringLiteral("key")) == QLatin1String("memory"));

        // The sleep ends: its row stays for one refresh, marked, then goes;
        // it also leaves the selection.
        const int sleepRow = model.indexOfKey(sleepKey);
        QVERIFY(sleepRow >= 0);
        tree.step();
        model.setSnapshot(sampler.sample(2.0));
        QCOMPARE(model.selectionCount(), 1);
        QCOMPARE(model.currentKey(), firefoxKey);
        const int ghost = model.indexOfKey(sleepKey);
        QVERIFY(ghost >= 0);
        QVERIFY(model.data(model.index(ghost), ProcessModel::EndedRole).toBool());
        model.setSnapshot(sampler.sample(2.0));
        QCOMPARE(model.indexOfKey(sleepKey), -1);

        model.setTab(QStringLiteral("cpu"));
        QCOMPARE(model.sortKey(), QStringLiteral("cpu"));
        QVERIFY(model.data(model.index(model.indexOfKey(firefoxKey)), ProcessModel::SelectedRole).toBool());
    }

    void orderDoesNotFlickerWithLoad()
    {
        // Two processes whose load alternates 6 % / 4 % and 4 % / 6 %: by
        // the newest sample alone they would swap at every refresh.
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        model.setView(QStringLiteral("mine"));
        auto addTicks = [&](int pid, int ticks) {
            QFile f(tree.proc() + QStringLiteral("/%1/stat").arg(pid));
            QVERIFY(f.open(QIODevice::ReadWrite));
            QByteArray data = f.readAll().trimmed();
            const int close = data.lastIndexOf(')');
            QList<QByteArray> fields = data.mid(close + 2).split(' ');
            fields[11] = QByteArray::number(fields[11].toLongLong() + ticks); // utime
            f.resize(0);
            f.seek(0);
            f.write(data.left(close + 2) + fields.join(' ') + "\n");
        };
        model.setSnapshot(sampler.sample(0));
        QStringList firstTwo;
        int swaps = 0;
        for (int round = 0; round < 10; ++round) {
            addTicks(1840, round % 2 ? 8 : 12); // dolphin
            addTicks(2210, round % 2 ? 12 : 8); // foot
            if (round == 0)
                addTicks(1840, 2); // dolphin starts out slightly ahead
            model.setSnapshot(sampler.sample(2.0));
            const QStringList now = names(model).mid(0, 2);
            if (round >= 4 && now != firstTwo)
                ++swaps;
            firstTwo = now;
        }
        QVERIFY(firstTwo.contains(QStringLiteral("dolphin")) && firstTwo.contains(QStringLiteral("foot")));
        QVERIFY2(swaps <= 1, qPrintable(QString::number(swaps)));
        // The number shown is still the newest sample, not the average.
        QCOMPARE(cellOf(model, rowOf(model, QStringLiteral("dolphin")), QStringLiteral("cpu")), QStringLiteral("4.0"));
    }

    void rowsHoldStillUnderAFinger()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setSnapshot(sampler.sample(0));
        model.setView(QStringLiteral("mine"));
        const QStringList before = names(model);

        model.setHold(true);
        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        tree.step();
        model.setSnapshot(sampler.sample(2.0));
        QCOMPARE(names(model), before); // same rows, same order
        QCOMPARE(moved.count() + inserted.count() + removed.count(), 0);
        // ... with fresh numbers.
        QCOMPARE(cellOf(model, model.indexOfKey(model.keyForPid(1688)), QStringLiteral("cpu")), QStringLiteral("19.5"));

        model.setHold(false); // the finger lifts: now the list re-sorts
        QCOMPARE(names(model).first(), QStringLiteral("Isolated Web Co"));
        QVERIFY(names(model).contains(QStringLiteral("wl-copy")));
    }

    void columns()
    {
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        auto keys = [&] {
            QStringList k;
            for (const QVariant &c : model.columns())
                k << c.toMap().value(QStringLiteral("key")).toString();
            return k;
        };
        QCOMPARE(keys(), QStringList({"cpu", "cpuTime", "threads", "wakeups", "pid", "user"}));
        model.setColumnVisible(QStringLiteral("nice"), true);
        model.setColumnVisible(QStringLiteral("user"), false);
        model.setColumnVisible(QStringLiteral("pid"), false); // always present
        QCOMPARE(keys(), QStringList({"cpu", "cpuTime", "threads", "wakeups", "nice", "pid"}));
        model.setTab(QStringLiteral("memory"));
        QCOMPARE(keys(), QStringList({"memory", "swap", "threads", "pid", "user"})); // per tab
        // A phone shows two columns; the sorted one must be among them.
        model.sortBy(QStringLiteral("threads"));
        model.setNarrow(true);
        QCOMPARE(keys().first(), QStringLiteral("threads"));
        // The choice is kept for the next start. A filter view is not: the
        // app must not open on a list that may be empty.
        model.setView(QStringLiteral("tree"));
        QCOMPARE(ProcessModel(&apps, &prot, nullptr, 1000).view(), QStringLiteral("tree"));
        model.setView(QStringLiteral("ports"));
        QCOMPARE(ProcessModel(&apps, &prot, nullptr, 1000).view(), QStringLiteral("apps"));
        ProcessModel again(&apps, &prot, nullptr, 1000);
        QCOMPARE(again.tab(), QStringLiteral("memory"));
        again.setTab(QStringLiteral("cpu"));
        QVERIFY(!again.columns().contains(QVariant()));
        QStringList cpuKeys;
        for (const QVariant &c : again.columns())
            cpuKeys << c.toMap().value(QStringLiteral("key")).toString();
        QCOMPARE(cpuKeys, QStringList({"cpu", "cpuTime", "threads", "wakeups", "nice", "pid"}));
    }

    void formatting()
    {
        QCOMPARE(ProcessModel::formatCpuTime(29137, 100), QStringLiteral("4:51.37"));
        QCOMPARE(ProcessModel::formatCpuTime(1192000, 100), QStringLiteral("3:18:40"));
        QCOMPARE(ProcessModel::formatCpuTime(5, 100), QStringLiteral("0:00.05"));
        QCOMPARE(ProcessModel::formatBytes(320102), QStringLiteral("312.6 MB"));
        QCOMPARE(ProcessModel::formatBytes(2097152), QStringLiteral("2.00 GB"));
        QCOMPARE(ProcessModel::formatBytes(512), QStringLiteral("512 KB"));
        QCOMPARE(ProcessModel::formatBytes(-1), QStringLiteral("—"));
        QCOMPARE(ProcessModel::formatCount(1026), QStringLiteral("1,026"));
        QCOMPARE(ProcessModel::formatCount(1234567), QStringLiteral("1,234,567"));
        QCOMPARE(SystemInfo::formatUptime(2 * 86400 + 4 * 3600 + 17 * 60), QStringLiteral("2d 04:17"));
        QCOMPARE(Inspector::formatElapsed(28 * 60 + 57), QStringLiteral("28:57"));
        QCOMPARE(Inspector::formatElapsed(3 * 3600 + 5), QStringLiteral("3:00:05"));
    }

    void footers()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        SystemInfo info;
        info.setSnapshot(sampler.sample(0));
        // Straight after opening: the kernel's averages, no graph of CPU yet.
        QVERIFY(!info.property("loadAverage").toString().isEmpty());
        QCOMPARE(info.property("pressureNow").toString(), QStringLiteral("some 4.2 · full 0.6"));
        QCOMPARE(info.property("pressureAverages").toString(), QStringLiteral("3.1 · 2.0"));
        QCOMPARE(info.userHistory().size(), 0);
        QCOMPARE(info.pressureHistory().size(), 1);
        QCOMPARE(info.memUsedHistory().size(), 1);
        QVERIFY(info.memUsedHistory().first().toDouble() > 1 && info.memUsedHistory().first().toDouble() < 100);
        QCOMPARE(info.property("compressed").toString(), QStringLiteral("94.3 MB"));
        QCOMPARE(info.property("frequency").toString(), QStringLiteral("2.34 GHz"));
        tree.step();
        info.setSnapshot(sampler.sample(2.0));
        QCOMPARE(info.userHistory().size(), 1);
        QCOMPARE(info.pressureHistory().last().toDouble(), double(6.8f));
        QCOMPARE(info.coreHistories().size(), 10);
        QCOMPARE(info.property("historyLength").toInt(), 60); // two minutes at 2 s
    }

    void inspect()
    {
        Tree tree;
        Sampler sampler(tree.proc(), tree.sys());
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot, nullptr, 1000);
        sampler.sample(0);
        tree.step();
        model.setSnapshot(sampler.sample(2.0));
        Inspector inspector(&model, tree.proc());

        inspector.open(model.keyForPid(1204));
        const QVariantMap info = inspector.info();
        QCOMPARE(info.value("title").toString(), QStringLiteral("firefox"));
        QCOMPARE(info.value("appName").toString(), QStringLiteral("Firefox"));
        QCOMPARE(info.value("parent").toString(), QStringLiteral("systemd (900)"));
        QCOMPARE(info.value("parentKey").toString(), model.keyForPid(900));
        QCOMPARE(info.value("state").toString(), QStringLiteral("Sleeping"));
        QCOMPARE(info.value("elapsed").toString(), QStringLiteral("29:01")); // started 1737 s before the first frame; the second was recorded 4 s later
        QCOMPARE(info.value("cgroup").toString(), QStringLiteral("app-firefox@3f1a.scope"));
        QCOMPARE(info.value("cwd").toString(), QStringLiteral("/home/tablet"));
        QCOMPARE(info.value("cpu").toString(), QStringLiteral("12.5"));
        QCOMPARE(info.value("memSwap").toString(), QStringLiteral("30.2 MB"));
        QVERIFY(info.value("io").toString().contains(QStringLiteral(" · ")));
        QCOMPARE(inspector.children().size(), 5);
        QCOMPARE(inspector.cpuHistory().size(), 1);

        QVERIFY(!inspector.filesLoaded());
        inspector.loadFiles();
        QStringList files;
        for (const QVariant &f : inspector.files())
            files << f.toMap().value("text").toString() + QLatin1Char('|') + f.toMap().value("detail").toString();
        QVERIFY2(files.contains(QStringLiteral("0.0.0.0:22|TCP · listening")), qPrintable(files.join('\n')));
        QVERIFY(files.contains(QStringLiteral("192.168.127.2:22 → 192.168.127.1:46570|TCP")));
        QVERIFY(files.contains(QStringLiteral("/run/systemd/private|unix")));
        QVERIFY(files.contains(QStringLiteral("Pipe|30412")));
        QVERIFY(files.contains(QStringLiteral("eventfd|")));
        QVERIFY(files.contains(QStringLiteral("/usr/lib/firefox/omni.ja|")));
        QVERIFY(files.contains(QStringLiteral("Socket|other")));

        // An app row inspects its main process; the buttons keep acting on the app.
        model.setView(QStringLiteral("apps"));
        const QString appKey = model.data(model.index(rowOf(model, QStringLiteral("Firefox"))), ProcessModel::KeyRole).toString();
        inspector.open(appKey);
        QCOMPARE(inspector.key(), appKey);
        QCOMPARE(inspector.info().value("title").toString(), QStringLiteral("Firefox"));
        QCOMPARE(inspector.info().value("pid").toInt(), 1204);
        QCOMPARE(inspector.info().value("count").toInt(), 6);

        // The process ends while the panel is open.
        inspector.open(model.keyForPid(1752));
        QDir(tree.proc() + QStringLiteral("/1752")).removeRecursively();
        model.setSnapshot(sampler.sample(2.0));
        QVERIFY(inspector.info().value("ended").toBool());
        QCOMPARE(inspector.info().value("title").toString(), QStringLiteral("Isolated Web Co"));
    }

    // ------------------------------------------------------------ config
    void protectedList()
    {
        ProtectedList list;
        list.load(QStringLiteral(DATA_DIR "/protected.conf"));
        QVERIFY(list.consequence(905, QStringLiteral("labwc")).contains(QStringLiteral("logged out")));
        QVERIFY(list.contains(1, QStringLiteral("systemd")));
        QVERIFY(!list.contains(900, QStringLiteral("systemd"))); // the user manager is not PID 1
        QVERIFY(list.contains(4, QStringLiteral("NetworkManager")));
        QVERIFY(list.contains(4, QStringLiteral("systemd-logind")));
        QVERIFY(list.contains(int(QCoreApplication::applicationPid()), QStringLiteral("whatever")));
        QVERIFY(!list.contains(4, QStringLiteral("firefox")));
        // A distro adds its own line; names are matched as comm, 15 characters.
        list.parse("# comment\nmy-very-long-daemon-name: It matters.\nbroken line\n");
        QCOMPARE(list.consequence(7, QStringLiteral("my-very-long-da")), QStringLiteral("It matters."));
        QVERIFY(!list.contains(7, QStringLiteral("labwc")));
    }

    void desktopFiles()
    {
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        const AppEntry &dolphin = apps.forUnit(QStringLiteral("app-org.kde.dolphin@a1b2.scope"));
        QCOMPARE(dolphin.name, QStringLiteral("Dolphin"));
        QCOMPARE(dolphin.id, QStringLiteral("org.kde.dolphin"));
        QCOMPARE(dolphin.glyph, QStringLiteral("folder"));
        QCOMPARE(dolphin.icon, QStringLiteral("system-file-manager"));
        // Name= of a later group ([Desktop Action ...]) is not the app's name.
        QCOMPARE(apps.forUnit(QStringLiteral("app-firefox-99aa.scope")).name, QStringLiteral("Firefox"));
        // No desktop file: the id from the unit stands in.
        const AppEntry &unknown = apps.forUnit(QStringLiteral("app-some.tool@1.scope"));
        QCOMPARE(unknown.name, QStringLiteral("some.tool"));
        QVERIFY(!unknown.found);
    }

    void theme()
    {
        QTemporaryDir dir;
        auto put = [&](const QString &name, const QByteArray &data) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        Theme theme;
        theme.setThemeDir(dir.path());
        auto color = [&](const char *name) { return theme.property(name).value<QColor>(); };
        // No theme at all: the Tokyo Night values of the mockups.
        QCOMPARE(color("background"), QColor("#1a1b26"));
        QCOMPARE(color("surface"), QColor("#13141c"));
        QCOMPARE(color("raised"), QColor("#24283b"));
        QCOMPARE(color("accent"), QColor("#7aa2f7"));
        QCOMPARE(color("secondary").name(), QStringLiteral("#7b819e")); // background 68 % toward foreground
        QCOMPARE(color("accentForeground").name(), QStringLiteral("#000000"));
        QCOMPARE(color("scrim").name(QColor::HexArgb), QStringLiteral("#8c000000")); // black at 55 %, not the background
        QCOMPARE(color("line").name(QColor::HexArgb), QStringLiteral("#1fa9b1d6"));
        QCOMPARE(color("tintStrong").name(QColor::HexArgb), QStringLiteral("#2e7aa2f7"));
        QCOMPARE(color("redTint").name(QColor::HexArgb), QStringLiteral("#24f7768e"));
        QVERIFY(!theme.property("light").toBool());
        QCOMPARE(theme.property("radius").toInt(), 0); // square without Hyprland
        QCOMPARE(theme.property("fontText").toInt(), 13);

        // Only colors.toml counts: a shell.toml beside it changes nothing.
        put(QStringLiteral("colors.toml"),
            "# a light theme\nmode = \"light\"\nbackground = \"#fafafa\"\nforeground = '#202020'\naccent = \"#aa3300\"\nred = \"#cc0000\"\n"
            "lighter_background = \"#fafafa\"\n");
        put(QStringLiteral("shell.toml"), "[controls]\nnormal-color = \"#00ff00\"\n[font]\nbase-size = 18\n");
        QSignalSpy changed(&theme, &Theme::changed);
        theme.reload();
        QCOMPARE(changed.count(), 1);
        QVERIFY(theme.property("light").toBool());
        QCOMPARE(color("background"), QColor("#fafafa"));
        QCOMPARE(color("foreground"), QColor("#202020"));
        QCOMPARE(color("red"), QColor("#cc0000"));
        QCOMPARE(theme.property("fontText").toInt(), 13);
        QCOMPARE(color("quiet").name(QColor::HexArgb), QStringLiteral("#0d202020"));
        // A dark accent takes white text.
        QCOMPARE(color("accentForeground").name(), QStringLiteral("#ffffff"));
        // A light theme without dark_background still gets a footer colour
        // near its own background, and secondary text that reads on it.
        QVERIFY(color("surface").lightness() > 150);
        QVERIFY(Theme::luminance(color("secondary")) < 0.2);
        // lighter_background equal to the background would hide the icon tile.
        QVERIFY(color("raised") != color("background"));

        QCOMPARE(Theme::roundingFrom("{\"option\": \"decoration:rounding\", \"int\": 8, \"set\": true}"), 8);
        QCOMPARE(Theme::roundingFrom("no such option"), 0);

        const QHash<QString, QString> parsed = Theme::parseToml("accent = \"#7aa2f7\"  # c\nmode = light\n# muted = \"#000000\"\n");
        QCOMPARE(parsed.value("accent"), QStringLiteral("#7aa2f7"));
        QCOMPARE(parsed.value("mode"), QStringLiteral("light"));
        QVERIFY(!parsed.contains("muted"));
    }

    // ------------------------------------------------------------ actions
    // These act on real processes started here, through the live /proc.
    void actionsOnOwnProcesses()
    {
        if (!QFile::exists(QStringLiteral("/proc/self/stat")))
            QSKIP("needs a live /proc (Linux)");
        Sampler sampler;
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot);
        model.setView(QStringLiteral("all"));
        Actions actions(&model, nullptr, QStringLiteral("/proc"));
        QSignalSpy messages(&actions, &Actions::message);

        auto spawn = [](QProcess &p) {
            p.start(QStringLiteral("sleep"), {QStringLiteral("600")});
            QVERIFY(p.waitForStarted());
        };
        auto state = [](qint64 pid) {
            QByteArray data;
            procfs::Stat st;
            if (!procfs::readFile(QStringLiteral("/proc/%1/stat").arg(pid), data) || !procfs::parseStat(data, st))
                return '-';
            return st.state;
        };
        QProcess a, b, c;
        spawn(a);
        spawn(b);
        spawn(c);
        model.setSnapshot(sampler.sample(0));
        const QString keyA = model.keyForPid(int(a.processId()));
        const QString keyB = model.keyForPid(int(b.processId()));
        const QString keyC = model.keyForPid(int(c.processId()));
        QVERIFY(!keyA.isEmpty() && !keyB.isEmpty() && !keyC.isEmpty());
        QVERIFY(actions.disabledReason(keyA).isEmpty());
        QVERIFY(!actions.needsSudo(keyA));

        // Pause and resume.
        actions.sendSignal({keyA}, SIGSTOP);
        QTRY_COMPARE(state(a.processId()), 'T');
        model.setSnapshot(sampler.sample(1.0));
        QVERIFY(model.rowInfo(keyA).value("paused").toBool());
        actions.sendSignal({keyA}, SIGCONT);
        QTRY_COMPARE(state(a.processId()), 'S');

        // Lowering a priority needs nobody's permission.
        actions.renice(keyA, 7);
        QCOMPARE(getpriority(PRIO_PROCESS, id_t(a.processId())), 7);
        QVERIFY(actions.prompt().isEmpty());

        // Quit needs no confirmation: SIGTERM, and the process is gone.
        actions.quit({keyA});
        QVERIFY(actions.prompt().isEmpty());
        QVERIFY(a.waitForFinished(3000));
        QCOMPARE(a.exitStatus(), QProcess::CrashExit); // ended by the signal

        // Force Quit asks once, names the process, and does nothing on Cancel.
        actions.forceQuit({keyB});
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        QCOMPARE(actions.prompt().value("title").toString(), QStringLiteral("Force Quit sleep?"));
        QVERIFY(actions.prompt().value("message").toString().contains(QStringLiteral("Unsaved work is lost")));
        actions.cancel();
        QVERIFY(actions.prompt().isEmpty());
        QTest::qWait(150);
        QCOMPARE(b.state(), QProcess::Running);
        // A frozen process cannot handle SIGTERM; Force Quit still removes it.
        actions.sendSignal({keyB}, SIGSTOP);
        QTRY_COMPARE(state(b.processId()), 'T');
        QSignalSpy sent(&actions, &Actions::sent);
        actions.forceQuit({keyB});
        QCOMPARE(sent.count(), 0); // not before the confirmation
        actions.confirm();
        QVERIFY(b.waitForFinished(2000));
        // The interface is told, so an open inspect panel can close.
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.first().at(0).toStringList(), QStringList({keyB}));
        QVERIFY(sent.first().at(1).toBool());

        // A recycled PID is never hit: the row still names the old start
        // time, the PID now belongs to something else (here: we pretend by
        // asking about a start time that is not c's).
        const ProcSample *sample = model.procByKey(keyC);
        QVERIFY(sample);
        QVERIFY(actions.sameProcess(sample->pid, sample->startTicks));
        QVERIFY(!actions.sameProcess(sample->pid, sample->startTicks + 1));
        QVERIFY(!actions.sameProcess(int(a.processId()), 1)); // gone entirely

        // Quit on something that has ended says so and sends nothing.
        actions.quit({keyA});
        QCOMPARE(messages.count(), 0); // still in the old snapshot: silently skipped by the start-time check
        model.setSnapshot(sampler.sample(1.0));
        actions.quit({keyA});
        QCOMPARE(messages.count(), 1);

        // Quit process tree: children first, then the parent.
        QProcess shell;
        shell.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("sleep 600 & sleep 600 & wait")});
        QVERIFY(shell.waitForStarted());
        QTest::qWait(300);
        model.setSnapshot(sampler.sample(1.0));
        const QString shellKey = model.keyForPid(int(shell.processId()));
        const QVector<ProcessModel::TargetProc> kids = model.descendants(int(shell.processId()));
        QCOMPARE(kids.size(), 2);
        actions.quitTree(shellKey);
        QVERIFY(shell.waitForFinished(3000));
        for (const ProcessModel::TargetProc &kid : kids)
            QTRY_VERIFY(!actions.sameProcess(kid.pid, kid.startTicks));

        c.kill();
        c.waitForFinished();
    }

    void protectedAsksFirst()
    {
        if (!QFile::exists(QStringLiteral("/proc/self/stat")))
            QSKIP("needs a live /proc (Linux)");
        Sampler sampler;
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        prot.load(QStringLiteral(DATA_DIR "/protected.conf"));
        prot.parse("sleep: Sleeping beauty wakes up.\n");
        ProcessModel model(&apps, &prot);
        model.setView(QStringLiteral("all"));
        Actions actions(&model, nullptr, QStringLiteral("/proc"));
        QProcess p;
        p.start(QStringLiteral("sleep"), {QStringLiteral("600")});
        QVERIFY(p.waitForStarted());
        model.setSnapshot(sampler.sample(0));
        const QString key = model.keyForPid(int(p.processId()));

        // Even a plain Quit asks when the process is protected, says what
        // will happen and names the file.
        actions.quit({key});
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        QCOMPARE(actions.prompt().value("title").toString(), QStringLiteral("Quit sleep?"));
        QCOMPARE(actions.prompt().value("message").toString(), QStringLiteral("Sleeping beauty wakes up."));
        QVERIFY(actions.prompt().value("note").toString().contains(QStringLiteral("protected.conf")));
        QTest::qWait(100);
        QCOMPARE(p.state(), QProcess::Running);
        // Nothing is refused: after the confirmation it goes.
        actions.confirm();
        QVERIFY(p.waitForFinished(3000));
    }

    // ------------------------------------------------------------ interface
    // The app's own QML, driven with synthetic touches and keys, showing
    // the recorded tree (so nothing is ever signalled).
    void iconPaths()
    {
        // A circle as two half arcs, as the icons write it.
        const QPainterPath circle = IconItem::parse(QStringLiteral("M3 12 a9 9 0 1 0 18 0 a9 9 0 1 0 -18 0"));
        const QRectF box = circle.boundingRect();
        QVERIFY2(qAbs(box.left() - 3) < 0.01 && qAbs(box.right() - 21) < 0.01 && qAbs(box.top() - 3) < 0.01
                     && qAbs(box.bottom() - 21) < 0.01, qPrintable(QStringLiteral("%1 %2 %3 %4").arg(box.left()).arg(box.top()).arg(box.right()).arg(box.bottom())));
        // A rounded corner turns the short way round.
        const QRectF corner = IconItem::parse(QStringLiteral("M5 5 V11 a3 3 0 0 0 3 3 H19")).boundingRect();
        QVERIFY(qAbs(corner.left() - 5) < 0.01 && qAbs(corner.bottom() - 14) < 0.01 && qAbs(corner.right() - 19) < 0.01);
        const QRectF lines = IconItem::parse(QStringLiteral("M6 6 L18 18 M18 6 L6 18 H2 V3 Z")).boundingRect();
        QCOMPARE(lines, QRectF(2, 3, 16, 15));
    }

    void interface()
    {
        Tree tree;
        Theme theme;
        QTemporaryDir noTheme;
        theme.setThemeDir(noTheme.path()); // the built-in look
        Prefs prefs;
        QVERIFY(!prefs.hintSeen()); // a first start shows how the list works
        ProtectedList prot;
        prot.load(QStringLiteral(DATA_DIR "/protected.conf"));
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        Collector collector(tree.proc(), tree.sys()); // not started: the test steps the model itself
        ProcessModel model(&apps, &prot, nullptr, 1000);
        SystemInfo system;
        Inspector inspector(&model, tree.proc());
        Actions actions(&model, nullptr, tree.proc());
        QSignalSpy messages(&actions, &Actions::message);
        Sampler sampler(tree.proc(), tree.sys());
        sampler.sample(0);
        tree.step();
        const SnapshotPtr snap = sampler.sample(2.0);
        model.setSnapshot(snap);
        system.setSnapshot(snap);

        qmlRegisterType<IconItem>("Omactivity", 1, 0, "IconItem");
        qmlRegisterType<LinesItem>("Omactivity", 1, 0, "LinesItem");
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &list) {
            for (const QQmlError &e : list)
                warnings << e.toString();
        });
        QQmlContext *ctx = engine.rootContext();
        ctx->setContextProperty(QStringLiteral("theme"), &theme);
        ctx->setContextProperty(QStringLiteral("prefs"), &prefs);
        ctx->setContextProperty(QStringLiteral("collector"), &collector);
        ctx->setContextProperty(QStringLiteral("processes"), &model);
        ctx->setContextProperty(QStringLiteral("system"), &system);
        ctx->setContextProperty(QStringLiteral("inspector"), &inspector);
        ctx->setContextProperty(QStringLiteral("actions"), &actions);
        ctx->setContextProperty(QStringLiteral("devShow"), QString());
        ctx->setContextProperty(QStringLiteral("devWidth"), 768);
        ctx->setContextProperty(QStringLiteral("devHeight"), 1024);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->property("mode").toString(), QStringLiteral("medium"));
        QObject *table = window->findChild<QObject *>(QStringLiteral("table"));
        QVERIFY(table);

        QPointingDevice *touch = QTest::createTouchDevice();
        // Rows start under the toolbar (68), the tab row (52) and the header (44).
        const int top = 68 + 52 + 44, rowHeight = 48;
        auto rowY = [&](int row) { return top + row * rowHeight + rowHeight / 2; };
        auto tap = [&](const QPoint &p) {
            QTest::touchEvent(window, touch).press(1, p);
            QTest::qWait(30);
            QTest::touchEvent(window, touch).release(1, p);
            QTest::qWait(60);
        };
        auto drag = [&](QPoint from, const QPoint &to, bool release = true) {
            QTest::touchEvent(window, touch).press(1, from);
            const int steps = 12;
            for (int i = 1; i <= steps; ++i) {
                QTest::qWait(12);
                QTest::touchEvent(window, touch).move(1, from + (to - from) * i / steps);
            }
            if (release) {
                QTest::qWait(30);
                QTest::touchEvent(window, touch).release(1, to);
            }
            QTest::qWait(250);
        };

        // Rows are 48 px: the first one is Firefox, the busiest app.
        QCOMPARE(model.data(model.index(0), ProcessModel::NameRole).toString(), QStringLiteral("Firefox"));
        const QString firefoxKey = model.data(model.index(0), ProcessModel::KeyRole).toString();

        // (The first event of a new synthetic touch device is not delivered;
        // spend it on the footer.)
        tap(QPoint(300, 1000));

        // A tap on the chevron opens the app's processes without selecting.
        QCOMPARE(model.rowCount(), 7);
        tap(QPoint(24, rowY(0)));
        QCOMPARE(model.rowCount(), 13);
        QCOMPARE(model.selectionCount(), 0);
        QVERIFY(inspector.key().isEmpty());

        // A tap on a row selects it and opens the inspect sheet for it.
        tap(QPoint(300, rowY(1)));
        QCOMPARE(inspector.key(), model.keyForPid(1688));
        QVERIFY(prefs.hintSeen()); // ... and the hint is gone for good once a process was opened
        QCOMPARE(model.currentKey(), model.keyForPid(1688));
        QVERIFY(window->property("inspectOpen").toBool());
        // Escape closes the sheet; the selection stays.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("inspectOpen").toBool());
        QTest::qWait(350); // the sheet slides away
        QCOMPARE(model.selectionCount(), 1);

        // Arrow keys move the selection.
        QTest::keyClick(window, Qt::Key_Down);
        QCOMPARE(model.currentKey(), model.keyForPid(1204));
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(model.currentKey(), model.keyForPid(1688));

        // A sideways drag on a row does nothing while every column fits...
        drag(QPoint(700, rowY(3)), QPoint(500, rowY(3)));
        QVERIFY(actions.prompt().isEmpty());
        QCOMPARE(table->property("hOffset").toDouble(), 0.0);
        QVERIFY(!window->property("inspectOpen").toBool()); // and is not mistaken for a tap
        // ... and scrolls the columns when they don't: no buttons, no action.
        model.setColumnVisible(QStringLiteral("command"), true);
        QTest::qWait(50);
        QVERIFY(table->property("overflow").toBool());
        drag(QPoint(700, rowY(3)), QPoint(500, rowY(3)));
        QVERIFY(table->property("hOffset").toDouble() > 150);
        QVERIFY(actions.prompt().isEmpty());
        QVERIFY(!window->property("inspectOpen").toBool());
        drag(QPoint(400, rowY(3)), QPoint(760, rowY(3)));
        QCOMPARE(table->property("hOffset").toDouble(), 0.0);
        model.setColumnVisible(QStringLiteral("command"), false);

        // A vertical drag scrolls the list.
        window->setHeight(560); // four and a bit rows fit
        QTest::qWait(100);
        drag(QPoint(300, rowY(3)), QPoint(300, rowY(0)));
        QVERIFY(table->property("scrollY").toDouble() > 50);
        QVERIFY(actions.prompt().isEmpty());
        // A long press opens the menu, and the same finger, still down and
        // moving, no longer scrolls the list behind it.
        QMetaObject::invokeMethod(table, "scrollTo", Q_ARG(QVariant, 0));
        QTest::qWait(100);
        QTest::touchEvent(window, touch).press(1, QPoint(300, rowY(3)));
        QTest::qWait(1100);
        QTRY_COMPARE(window->property("overlay").toString(), QStringLiteral("actions"));
        for (int i = 1; i <= 10; ++i) {
            QTest::qWait(12);
            QTest::touchEvent(window, touch).move(1, QPoint(300, rowY(3) - i * 14));
        }
        QTest::qWait(60);
        QCOMPARE(table->property("scrollY").toDouble(), 0.0);
        QTest::touchEvent(window, touch).release(1, QPoint(300, rowY(3) - 140));
        QTest::qWait(60);
        QCOMPARE(window->property("overlay").toString(), QStringLiteral("actions")); // lifting the finger does not dismiss it
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::qWait(200);
        drag(QPoint(300, rowY(3)), QPoint(300, rowY(0))); // afterwards the list scrolls again
        QVERIFY(table->property("scrollY").toDouble() > 50);
        window->setHeight(1024);
        QMetaObject::invokeMethod(table, "scrollTo", Q_ARG(QVariant, 0));
        QTest::qWait(100);

        // While a finger rests on the list, a refresh moves no row.
        QTest::touchEvent(window, touch).press(1, QPoint(300, rowY(5)));
        QTest::qWait(50);
        QVERIFY(model.hold());
        QTest::touchEvent(window, touch).release(1, QPoint(300, rowY(5)));
        QTest::qWait(60);
        QVERIFY(!model.hold());
        window->setProperty("inspectOpen", false);
        inspector.close();
        QTest::qWait(350);

        // A long press opens the menu with every action.
        QTest::touchEvent(window, touch).press(1, QPoint(300, rowY(2)));
        QTest::qWait(1100);
        QTest::touchEvent(window, touch).release(1, QPoint(300, rowY(2)));
        QTRY_COMPARE(window->property("overlay").toString(), QStringLiteral("actions"));
        QCOMPARE(window->property("menuKey").toString(), model.keyForPid(1204));
        QTest::keyClick(window, Qt::Key_Escape);
        QCOMPARE(window->property("overlay").toString(), QString());

        // Ctrl+F goes to the search field; typing filters as you type, and
        // Delete then edits text instead of quitting the selected process.
        model.select(model.keyForPid(1204));
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(window->property("typing").toBool());
        for (const char c : {'b', 't', 'o', 'p'})
            QTest::keyClick(window, c);
        QCOMPARE(model.search(), QStringLiteral("btop"));
        QCOMPARE(names(model), QStringList({"foot", "btop"}));
        QTest::keyClick(window, Qt::Key_Delete);
        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(model.search(), QStringLiteral("bto"));
        QCOMPARE(messages.count(), 0);
        QVERIFY(actions.prompt().isEmpty());

        // Resizing switches the layout and keeps search, sort and selection.
        model.sortBy(QStringLiteral("threads"));
        window->setWidth(375);
        QTRY_COMPARE(window->property("mode").toString(), QStringLiteral("narrow"));
        QVERIFY(model.narrow());
        QCOMPARE(model.columns().first().toMap().value("key").toString(), QStringLiteral("threads"));
        window->setWidth(1024);
        QTRY_COMPARE(window->property("mode").toString(), QStringLiteral("wide"));
        QCOMPARE(model.search(), QStringLiteral("bto"));
        QCOMPARE(model.sortKey(), QStringLiteral("threads"));
        QCOMPARE(model.currentKey(), model.keyForPid(1204));

        // Outside a text field, Delete is Quit. This is a recorded tree, so
        // the app says so and sends nothing.
        QTest::keyClick(window, Qt::Key_Escape); // leaves and clears the search
        QCOMPARE(model.search(), QString());
        QVERIFY(!window->property("typing").toBool());
        QTest::keyClick(window, Qt::Key_Delete);
        QCOMPARE(messages.count(), 1);
        QVERIFY(messages.first().first().toString().contains(QStringLiteral("recorded")));
        // Shift+Delete is Force Quit, which asks.
        QTest::keyClick(window, Qt::Key_Delete, Qt::ShiftModifier);
        QCOMPARE(actions.prompt().value("title").toString(), QStringLiteral("Force Quit firefox?"));
        actions.cancel();

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    // Root's processes, through the real sudo. Needs a user with sudo rights
    // and a password, and two root-owned processes to end, so it only runs
    // where dev/sudo-check sets it up (a container).
    void rootProcessesThroughSudo()
    {
        const QByteArray password = qgetenv("OMACTIVITY_TEST_SUDO_PASSWORD");
        const QList<QByteArray> rootPids = qgetenv("OMACTIVITY_TEST_ROOT_PIDS").split(' ');
        if (password.isEmpty() || rootPids.size() != 2)
            QSKIP("set up by dev/sudo-check");
        Sampler sampler;
        AppIndex apps({kFixtures + QStringLiteral("/applications")});
        ProtectedList prot;
        ProcessModel model(&apps, &prot);
        model.setView(QStringLiteral("all"));
        Actions actions(&model, nullptr, QStringLiteral("/proc"));
        QStringList prompts;
        connect(&actions, &Actions::promptChanged, this, [&] { prompts << actions.prompt().value("type").toString(); });
        QSignalSpy messages(&actions, &Actions::message);
        model.setSnapshot(sampler.sample(0));

        QVERIFY(actions.canSudo());
        const QString first = model.keyForPid(rootPids[0].toInt());
        const QString second = model.keyForPid(rootPids[1].toInt());
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        const ProcSample a = *model.procByKey(first);
        const ProcSample b = *model.procByKey(second);
        QCOMPARE(a.uid, 0u);
        QVERIFY(actions.needsSudo(first));
        QVERIFY(actions.disabledReason(first).isEmpty());

        // Force Quit on a root process: confirm, then the password is asked
        // for in the app, because sudo needs it.
        actions.forceQuit({first});
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        QVERIFY(actions.prompt().value("message").toString().contains(QStringLiteral("owned by root")));
        actions.confirm();
        QTRY_COMPARE(actions.prompt().value("type").toString(), QStringLiteral("password"));
        QVERIFY(actions.prompt().value("error").toString().isEmpty());
        QVERIFY(actions.sameProcess(a.pid, a.startTicks));

        // A wrong password says so and sends nothing.
        actions.submitPassword(QStringLiteral("not the password"));
        QTRY_VERIFY_WITH_TIMEOUT(actions.prompt().value("error").toString().contains(QStringLiteral("Wrong password")), 15000);
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("password"));
        QVERIFY(actions.sameProcess(a.pid, a.startTicks));

        // The right one ends the process and closes the dialog.
        actions.submitPassword(QString::fromUtf8(password));
        QTRY_VERIFY_WITH_TIMEOUT(actions.prompt().isEmpty(), 15000);
        QTRY_VERIFY(!actions.sameProcess(a.pid, a.startTicks));

        // With the app still open, sudo remembers: no password for the
        // next root action. First a priority change on root's process...
        prompts.clear();
        actions.renice(second, 6);
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        actions.confirm();
        QTRY_COMPARE(getpriority(PRIO_PROCESS, id_t(b.pid)), 6);
        QTRY_VERIFY(actions.prompt().isEmpty());
        // ... then a second Force Quit.
        actions.forceQuit({second});
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        actions.confirm();
        QTRY_VERIFY(!actions.sameProcess(b.pid, b.startTicks));
        QVERIFY2(!prompts.contains(QStringLiteral("password")), qPrintable(prompts.join(',')));
        QCOMPARE(messages.count(), 0);

        // Raising the priority of an own process needs root as well, so it
        // asks first. (Not carried out here: a rootless container may not
        // raise priorities even as root.)
        QProcess own;
        own.start(QStringLiteral("sleep"), {QStringLiteral("600")});
        QVERIFY(own.waitForStarted());
        model.setSnapshot(sampler.sample(1.0));
        actions.renice(model.keyForPid(int(own.processId())), -5);
        QCOMPARE(actions.prompt().value("type").toString(), QStringLiteral("confirm"));
        QVERIFY(actions.prompt().value("message").toString().contains(QStringLiteral("sudo")));
        actions.cancel();
        own.kill();
        own.waitForFinished();
    }
};

QTEST_MAIN(TestOmactivity)
#include "tst_omactivity.moc"
