// Plain /proc and /sys text parsing. No state, no Qt GUI: everything here
// takes file contents (or a root directory) and returns numbers, so the
// tests can run it against the recorded tree in tests/fixtures.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

namespace procfs {

// Reads a whole small file with plain read(2). /proc files report size 0, so
// QFile's size-based paths don't help here. Returns false when the file is
// missing or unreadable (the process ended, or belongs to someone else);
// *err, when given, receives errno.
bool readFile(const QString &path, QByteArray &out, int *err = nullptr);

struct Stat {
    int pid = 0;
    QByteArray comm;
    char state = '?';
    int ppid = 0;
    int pgrp = 0;
    quint32 flags = 0;
    quint64 minflt = 0;
    quint64 majflt = 0;
    quint64 utime = 0;
    quint64 stime = 0;
    int nice = 0;
    int threads = 0;
    quint64 starttime = 0;
    quint64 vsizeBytes = 0;
    qint64 rssPages = 0;
    quint64 blkioTicks = 0; // time blocked on disk I/O; stays 0 unless kernel.task_delayacct is on
    bool kernelThread() const { return flags & 0x00200000; } // PF_KTHREAD
};
bool parseStat(const QByteArray &data, Stat &out);

// /proc/PID/schedstat: time on a CPU and time runnable but waiting for one, in ns.
bool parseSchedstat(const QByteArray &data, quint64 &runNs, quint64 &waitNs);

struct Status {
    uint uid = 0;
    qint64 vmRssKb = -1;
    quint64 voluntaryCtxt = 0;
    quint64 nonvoluntaryCtxt = 0;
};
bool parseStatus(const QByteArray &data, Status &out);

struct Smaps {
    qint64 pssKb = 0;
    qint64 privateKb = 0;
    qint64 sharedKb = 0;
    qint64 swapKb = 0;
};
bool parseSmapsRollup(const QByteArray &data, Smaps &out);

struct Io {
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
};
bool parseIo(const QByteArray &data, Io &out);

// The cgroup v2 path ("0::/user.slice/...") or, on v1, the name=systemd one.
QString parseCgroup(const QByteArray &data);

// "app-firefox@3f1a.scope" from a cgroup path, or empty when the process is
// not in a per-app unit of the user manager.
QString appUnitFromCgroup(const QString &cgroup);
// "firefox" from "app-firefox@3f1a.scope", "app-firefox-3f1a.scope" or
// "app-Hyprland-firefox-3f1a.scope" (the caller tries candidates in order
// against the installed desktop files).
QStringList appIdCandidates(const QString &unit);

QString cmdlineToString(const QByteArray &data);

struct CpuTimes {
    quint64 user = 0;   // user + nice
    quint64 system = 0; // system + irq + softirq + steal
    quint64 idle = 0;   // idle + iowait
    quint64 total() const { return user + system + idle; }
};
struct SystemStat {
    CpuTimes all;
    QVector<CpuTimes> cores;
    qint64 bootTime = 0;
};
bool parseSystemStat(const QByteArray &data, SystemStat &out);

struct MemInfo {
    qint64 totalKb = 0;
    qint64 availableKb = 0;
    qint64 cachedKb = 0; // Cached + Buffers + SReclaimable
    qint64 swapTotalKb = 0;
    qint64 swapFreeKb = 0;
};
bool parseMemInfo(const QByteArray &data, MemInfo &out);

struct Pressure {
    bool available = false;
    double some10 = 0, some60 = 0, some300 = 0;
    double full10 = 0, full60 = 0, full300 = 0;
};
bool parsePressure(const QByteArray &data, Pressure &out);

struct LoadAvg {
    double l1 = 0, l5 = 0, l15 = 0;
};
bool parseLoadAvg(const QByteArray &data, LoadAvg &out);

// /proc/diskstats, summed over whole disks (partitions would count twice).
struct DiskStats {
    quint64 reads = 0, writes = 0;           // completed requests
    quint64 readBytes = 0, writeBytes = 0;
    bool any = false;
};
bool parseDiskStats(const QByteArray &data, DiskStats &out);

// Open sockets by inode, from /proc/net/{tcp,tcp6,udp,udp6,unix}.
struct SocketInfo {
    bool listening = false; // TCP in LISTEN, or a bound UDP socket
    int port = 0;           // local port
    uint uid = 0;           // owner of the socket
    bool connected = false; // a TCP connection, not a listener
    QString proto;
    QString local;
    QString remote;
};
void parseNetSockets(const QByteArray &data, const QString &proto, QHash<quint64, SocketInfo> &out);
void parseNetUnix(const QByteArray &data, QHash<quint64, SocketInfo> &out);

} // namespace procfs
