// One pass over /proc: reads every process, diffs against the previous pass
// and returns a Snapshot. Holds only the previous counters, nothing else.
#pragma once

#include "procfs.h"

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QVector>

struct ProcSample {
    int pid = 0;
    int ppid = 0;
    int pgrp = 0;
    uint uid = 0;
    char state = '?';
    bool kernelThread = false;
    QString name;    // comm, as the kernel reports it (15 characters at most)
    QString cmdline;
    QString cgroup;
    QString appUnit; // "app-firefox@3f1a.scope", or empty

    double cpuPercent = 0; // 100 = one core
    quint64 cpuTicks = 0;
    quint64 startTicks = 0;
    int threads = 0;
    int nice = 0;
    double wakeups = 0; // context switches since the last sample
    double cpuWaitPercent = 0; // runnable but waiting for a core; 100 = all the time
    double ioWaitPercent = -1; // blocked on disk I/O; -1 when the kernel does not account it
    qint64 readBytes = -1;     // disk I/O so far; -1: not sampled, or not ours to read
    qint64 writeBytes = -1;
    double readRate = 0;       // bytes a second since the last sample
    double writeRate = 0;
    int sockets = -1;          // open sockets; -1: not sampled, or not ours to read
    int connections = -1;      // of those, TCP connections
    QString ports;             // listening ports, "22, 631/udp"; empty when none or not sampled
    bool portsKnown = false;   // false: not sampled, or not ours to read
    quint64 ctxtVoluntary = 0;
    quint64 ctxtNonvoluntary = 0;
    quint64 minflt = 0;
    quint64 majflt = 0;

    qint64 memKb = 0; // Pss, or RSS when smaps_rollup is unreadable or not sampled
    bool memIsRss = true;
    qint64 rssKb = 0;
    qint64 privateKb = -1; // -1: not known (not sampled, or not ours to read)
    qint64 sharedKb = -1;
    qint64 swapKb = -1;
    quint64 vsizeKb = 0;

    quint64 id() const { return (quint64(pid) << 32) ^ startTicks; }
};

struct SystemSample {
    double userPercent = 0;
    double systemPercent = 0;
    double idlePercent = 100;
    QVector<float> corePercent;
    double load1 = 0, load5 = 0, load15 = 0;
    double uptimeSeconds = 0;
    qint64 bootTime = 0;
    double frequencyKhz = 0; // 0: no cpufreq
    int processes = 0;
    int threads = 0;

    qint64 memTotalKb = 0;
    qint64 memUsedKb = 0;
    qint64 cachedKb = 0;
    qint64 swapUsedKb = 0;
    qint64 swapTotalKb = 0;
    qint64 compressedKb = -1; // -1: no zram
    procfs::Pressure pressure;
    // Listening ports whose process could not be told (someone else's
    // descriptors are not readable), by owner: uid -> "22, 5353/udp".
    QHash<uint, QString> unattributedPorts;

    bool hasBattery = false;
    int batteryPercent = -1;
    QString batteryStatus;      // Charging, Discharging, Full, Not charging
    double powerWatts = -1;     // -1: the driver does not say
    double batterySeconds = -1; // to empty, or to full while charging; -1: unknown

    bool diskKnown = false;
    double diskReadsPerSec = 0, diskWritesPerSec = 0;
    double diskReadBytesPerSec = 0, diskWriteBytesPerSec = 0;
    struct Mount {
        QString path;
        qint64 totalKb = 0;
        qint64 freeKb = 0;
    };
    QVector<Mount> mounts;

    bool netKnown = false;
    double netRxBytesPerSec = 0, netTxBytesPerSec = 0;
    quint64 netRxBytes = 0, netTxBytes = 0; // since boot, all interfaces but loopback
    QString netInterface;                   // the one that has carried the most
};

struct Snapshot {
    double interval = 0; // seconds since the previous sample; 0 on the first
    QVector<ProcSample> procs;
    SystemSample sys;
    long ticksPerSecond = 100;
};
using SnapshotPtr = QSharedPointer<const Snapshot>;
Q_DECLARE_METATYPE(SnapshotPtr)

class Sampler
{
public:
    explicit Sampler(const QString &procRoot = QStringLiteral("/proc"),
                     const QString &sysRoot = QStringLiteral("/sys"));

    // smaps_rollup makes the kernel walk every mapping of a process, so it is
    // only read while something on screen shows it. status carries the
    // context-switch counters and VmRSS.
    void setMemoryDetail(bool on) { m_memoryDetail = on; }
    // Listening ports per process need every descriptor of every process
    // looked at, so only while a ports column or view is showing.
    void setPortDetail(bool on)
    {
        if (on && !m_portDetail)
            m_portsFresh = false; // look at once, not at the next third pass
        m_portDetail = on;
    }
    // /proc/PID/io, for the Disk tab.
    void setIoDetail(bool on) { m_ioDetail = on; }

    // elapsedSeconds is the time since the previous call (the caller owns
    // the clock, which lets tests step it).
    SnapshotPtr sample(double elapsedSeconds);

    QString procRoot() const { return m_proc; }

private:
    struct Prev {
        quint64 startTicks = 0;
        quint64 cpuTicks = 0;
        quint64 ctxt = 0;
        quint64 waitNs = 0;
        quint64 blkioTicks = 0;
        bool haveWait = false;
        QString ports;
        bool portsKnown = false;
        int sockets = -1;
        int connections = -1;
        procfs::Io io;
        bool haveIo = false;
        bool ioDenied = false;
        uint uid = 0;
        bool haveStatus = false;
        bool smapsDenied = false;
        QString cmdline;
        QString cgroup;
        QString appUnit;
        QByteArray comm;
        quint32 seen = 0; // round it was last seen in
        quint32 age = 0;  // samples it has been part of
        procfs::Smaps smaps;
        bool haveSmaps = false;
    };

    void sampleSystem(SystemSample &sys, double elapsed);

    QString m_proc;
    QString m_sys;
    bool m_memoryDetail = false;
    bool m_portDetail = false;
    bool m_portsFresh = false;
    bool m_delayAcct = false;
    QHash<quint64, procfs::SocketInfo> m_listening; // by socket inode
    QSet<quint64> m_connected;                       // TCP connections, by inode
    void socketsOf(const QString &base, Prev &prev);
    bool m_ioDetail = false;
    procfs::DiskStats m_prevDisk;
    bool m_havePrevDisk = false;
    quint64 m_prevRx = 0, m_prevTx = 0;
    bool m_havePrevNet = false;
    QVector<SystemSample::Mount> m_mounts;
    QSet<quint64> m_attributed;
    QHash<uint, QString> m_unattributed;
    quint32 m_round = 0;
    long m_ticks = 100;
    long m_pageKb = 4;
    QHash<int, Prev> m_prev;
    procfs::SystemStat m_prevStat;
    bool m_havePrevStat = false;
    QByteArray m_buf;
};
