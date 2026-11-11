#include "procfs.h"

#include <QFile>
#include <QList>
#include <QStringList>

#include <cctype>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace procfs {

bool readFile(const QString &path, QByteArray &out, int *err)
{
    out.resize(0);
    const QByteArray native = QFile::encodeName(path);
    int fd = ::open(native.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (err)
            *err = errno;
        return false;
    }
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n > 0) {
            out.append(buf, int(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            // A process can end between open and read (ESRCH).
            if (err)
                *err = errno;
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    return true;
}

namespace {

// Splits on runs of spaces without allocating a QList of QByteArrays.
struct Fields {
    const char *p;
    const char *end;
    bool next(const char *&tok, int &len)
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n'))
            ++p;
        if (p >= end)
            return false;
        tok = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n')
            ++p;
        len = int(p - tok);
        return true;
    }
};

quint64 toU64(const char *tok, int len)
{
    quint64 v = 0;
    for (int i = 0; i < len; ++i) {
        if (tok[i] < '0' || tok[i] > '9')
            break;
        v = v * 10 + quint64(tok[i] - '0');
    }
    return v;
}

qint64 toI64(const char *tok, int len)
{
    if (len > 0 && tok[0] == '-')
        return -qint64(toU64(tok + 1, len - 1));
    return qint64(toU64(tok, len));
}

// Value of a "Key:   123 kB" line; -1 when the key is absent.
qint64 keyValue(const QByteArray &data, const char *key)
{
    const int klen = int(qstrlen(key));
    int pos = 0;
    while (pos < data.size()) {
        int eol = data.indexOf('\n', pos);
        if (eol < 0)
            eol = data.size();
        if (eol - pos > klen && data[pos + klen] == ':' && !qstrncmp(data.constData() + pos, key, klen)) {
            Fields f{data.constData() + pos + klen + 1, data.constData() + eol};
            const char *tok;
            int len;
            if (f.next(tok, len))
                return toI64(tok, len);
            return -1;
        }
        pos = eol + 1;
    }
    return -1;
}

} // namespace

bool parseStat(const QByteArray &data, Stat &out)
{
    // "pid (comm) state ppid ...". comm may itself contain spaces and
    // parentheses, so the fields start after the last ')'.
    const int open = data.indexOf('(');
    const int close = data.lastIndexOf(')');
    if (open < 0 || close < open)
        return false;
    out.pid = int(toU64(data.constData(), open));
    out.comm = data.mid(open + 1, close - open - 1);

    Fields f{data.constData() + close + 1, data.constData() + data.size()};
    const char *tok;
    int len;
    int field = 3; // state is field 3 in proc(5) numbering
    bool sawStart = false;
    while (f.next(tok, len)) {
        switch (field) {
        case 3: out.state = tok[0]; break;
        case 4: out.ppid = int(toI64(tok, len)); break;
        case 5: out.pgrp = int(toI64(tok, len)); break;
        case 9: out.flags = quint32(toU64(tok, len)); break;
        case 10: out.minflt = toU64(tok, len); break;
        case 12: out.majflt = toU64(tok, len); break;
        case 14: out.utime = toU64(tok, len); break;
        case 15: out.stime = toU64(tok, len); break;
        case 19: out.nice = int(toI64(tok, len)); break;
        case 20: out.threads = int(toI64(tok, len)); break;
        case 22: out.starttime = toU64(tok, len); sawStart = true; break;
        case 23: out.vsizeBytes = toU64(tok, len); break;
        case 24: out.rssPages = toI64(tok, len); break;
        case 42: out.blkioTicks = toU64(tok, len); return true;
        }
        ++field;
    }
    return sawStart;
}

bool parseSchedstat(const QByteArray &data, quint64 &runNs, quint64 &waitNs)
{
    Fields f{data.constData(), data.constData() + data.size()};
    const char *tok;
    int len;
    if (!f.next(tok, len))
        return false;
    runNs = toU64(tok, len);
    if (!f.next(tok, len))
        return false;
    waitNs = toU64(tok, len);
    return true;
}

bool parseStatus(const QByteArray &data, Status &out)
{
    const qint64 uid = keyValue(data, "Uid");
    if (uid < 0)
        return false;
    out.uid = uint(uid);
    out.vmRssKb = keyValue(data, "VmRSS");
    const qint64 vol = keyValue(data, "voluntary_ctxt_switches");
    const qint64 nonvol = keyValue(data, "nonvoluntary_ctxt_switches");
    out.voluntaryCtxt = vol < 0 ? 0 : quint64(vol);
    out.nonvoluntaryCtxt = nonvol < 0 ? 0 : quint64(nonvol);
    return true;
}

bool parseSmapsRollup(const QByteArray &data, Smaps &out)
{
    const qint64 pss = keyValue(data, "Pss");
    if (pss < 0)
        return false;
    auto get = [&](const char *k) { return qMax<qint64>(0, keyValue(data, k)); };
    out.pssKb = pss;
    out.privateKb = get("Private_Clean") + get("Private_Dirty");
    out.sharedKb = get("Shared_Clean") + get("Shared_Dirty");
    out.swapKb = get("Swap");
    return true;
}

bool parseIo(const QByteArray &data, Io &out)
{
    const qint64 r = keyValue(data, "read_bytes");
    const qint64 w = keyValue(data, "write_bytes");
    if (r < 0 || w < 0)
        return false;
    out.readBytes = quint64(r);
    out.writeBytes = quint64(w);
    return true;
}

QString parseCgroup(const QByteArray &data)
{
    QString v1;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("0::"))
            return QString::fromUtf8(line.mid(3));
        const int c = line.indexOf(":name=systemd:");
        if (c >= 0)
            v1 = QString::fromUtf8(line.mid(c + 14));
    }
    return v1;
}

QString appUnitFromCgroup(const QString &cgroup)
{
    // Only units of a user manager count: /user.slice/user-1000.slice/
    // user@1000.service/app.slice/app-foo@id.scope[/...]
    if (!cgroup.contains(QLatin1String("/user@")))
        return QString();
    const QStringList parts = cgroup.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1String("app-"))
            && (part.endsWith(QLatin1String(".scope")) || part.endsWith(QLatin1String(".service"))))
            return part;
    }
    return QString();
}

static QString unescapeUnit(const QString &s)
{
    // systemd escapes "-" in names as \x2d.
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] == QLatin1Char('\\') && i + 3 < s.size() && s[i + 1] == QLatin1Char('x')) {
            bool ok = false;
            const int code = s.mid(i + 2, 2).toInt(&ok, 16);
            if (ok) {
                out += QChar(code);
                i += 3;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

QStringList appIdCandidates(const QString &unit)
{
    QString name = unit;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const bool scope = name.endsWith(QLatin1String(".scope"));
    if (dot > 0)
        name.truncate(dot);
    if (name.startsWith(QLatin1String("app-")))
        name.remove(0, 4);

    QStringList out;
    auto add = [&](const QString &s) {
        const QString u = unescapeUnit(s);
        if (!u.isEmpty() && !out.contains(u))
            out << u;
    };
    const int at = name.indexOf(QLatin1Char('@'));
    if (at > 0) {
        name.truncate(at);
    } else if (scope) {
        // app-<id>-<random>.scope: the last dash-separated part is the id.
        const int dash = name.lastIndexOf(QLatin1Char('-'));
        if (dash > 0) {
            add(name.left(dash));
            const int first = name.indexOf(QLatin1Char('-'));
            if (first > 0 && first < dash)
                add(name.mid(first + 1, dash - first - 1)); // launcher prefix dropped
        }
    }
    add(name);
    // app-<launcher>-<id>@...: also try without the launcher prefix.
    const int first = name.indexOf(QLatin1Char('-'));
    if (first > 0)
        add(name.mid(first + 1));
    return out;
}

QString cmdlineToString(const QByteArray &data)
{
    QByteArray d = data;
    while (d.endsWith('\0'))
        d.chop(1);
    d.replace('\0', ' ');
    return QString::fromUtf8(d);
}

static void parseCpuLine(Fields f, CpuTimes &t)
{
    quint64 v[8] = {0};
    const char *tok;
    int len;
    for (int i = 0; i < 8 && f.next(tok, len); ++i)
        v[i] = toU64(tok, len);
    t.user = v[0] + v[1];
    t.system = v[2] + v[5] + v[6] + v[7];
    t.idle = v[3] + v[4];
}

bool parseSystemStat(const QByteArray &data, SystemStat &out)
{
    out.cores.clear();
    bool ok = false;
    int pos = 0;
    while (pos < data.size()) {
        int eol = data.indexOf('\n', pos);
        if (eol < 0)
            eol = data.size();
        const char *line = data.constData() + pos;
        const int len = eol - pos;
        if (len > 4 && !qstrncmp(line, "cpu", 3)) {
            if (line[3] == ' ') {
                parseCpuLine(Fields{line + 4, line + len}, out.all);
                ok = true;
            } else {
                const char *sp = static_cast<const char *>(memchr(line, ' ', size_t(len)));
                if (sp) {
                    CpuTimes t;
                    parseCpuLine(Fields{sp, line + len}, t);
                    out.cores.append(t);
                }
            }
        } else if (len > 6 && !qstrncmp(line, "btime ", 6)) {
            out.bootTime = toI64(line + 6, len - 6);
        }
        pos = eol + 1;
    }
    return ok;
}

bool parseMemInfo(const QByteArray &data, MemInfo &out)
{
    out.totalKb = keyValue(data, "MemTotal");
    if (out.totalKb < 0)
        return false;
    auto get = [&](const char *k) { return qMax<qint64>(0, keyValue(data, k)); };
    const qint64 avail = keyValue(data, "MemAvailable");
    out.availableKb = avail >= 0 ? avail : get("MemFree") + get("Cached") + get("Buffers");
    out.cachedKb = get("Cached") + get("Buffers") + get("SReclaimable");
    out.swapTotalKb = get("SwapTotal");
    out.swapFreeKb = get("SwapFree");
    return true;
}

static double avgField(const QByteArray &line, const char *key)
{
    const int i = line.indexOf(key);
    if (i < 0)
        return 0;
    const int start = i + int(qstrlen(key));
    int end = line.indexOf(' ', start);
    if (end < 0)
        end = line.size();
    return line.mid(start, end - start).toDouble();
}

bool parsePressure(const QByteArray &data, Pressure &out)
{
    out = Pressure();
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("some ")) {
            out.some10 = avgField(line, "avg10=");
            out.some60 = avgField(line, "avg60=");
            out.some300 = avgField(line, "avg300=");
            out.available = true;
        } else if (line.startsWith("full ")) {
            out.full10 = avgField(line, "avg10=");
            out.full60 = avgField(line, "avg60=");
            out.full300 = avgField(line, "avg300=");
        }
    }
    return out.available;
}

bool parseLoadAvg(const QByteArray &data, LoadAvg &out)
{
    const QList<QByteArray> parts = data.split(' ');
    if (parts.size() < 3)
        return false;
    out.l1 = parts[0].toDouble();
    out.l5 = parts[1].toDouble();
    out.l15 = parts[2].toDouble();
    return true;
}

bool parseDiskStats(const QByteArray &data, DiskStats &out)
{
    out = DiskStats();
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 10)
            continue;
        // Whole disks only: sda, vda, nvme0n1, mmcblk0. Not sda1, nvme0n1p2,
        // mmcblk0p1, and not loop, ram or zram devices.
        const QByteArray &name = f[2];
        bool whole = false;
        if ((name.startsWith("sd") || name.startsWith("vd") || name.startsWith("hd")) && name.size() >= 3)
            whole = !std::isdigit(static_cast<unsigned char>(name.back()));
        else if (name.startsWith("nvme") || name.startsWith("mmcblk"))
            whole = !name.mid(4).contains('p') && (name.startsWith("mmcblk") || name.contains('n'));
        if (!whole)
            continue;
        out.reads += f[3].toULongLong();
        out.readBytes += f[5].toULongLong() * 512;
        out.writes += f[7].toULongLong();
        out.writeBytes += f[9].toULongLong() * 512;
        out.any = true;
    }
    return out.any;
}

// /proc/net prints each 32-bit word of an address as a host integer, so
// copying the parsed word back to memory restores the network byte order.
static QString hexAddress(const QByteArray &hex)
{
    const int colon = hex.lastIndexOf(':');
    if (colon < 0)
        return QString::fromLatin1(hex);
    const QByteArray addr = hex.left(colon);
    bool ok = false;
    const uint port = hex.mid(colon + 1).toUInt(&ok, 16);
    QString host;
    if (addr.size() == 8) {
        const quint32 word = addr.toUInt(&ok, 16);
        const uchar *b = reinterpret_cast<const uchar *>(&word);
        host = QStringLiteral("%1.%2.%3.%4").arg(b[0]).arg(b[1]).arg(b[2]).arg(b[3]);
    } else if (addr.size() == 32) {
        uchar bytes[16];
        for (int w = 0; w < 4; ++w) {
            const quint32 word = addr.mid(w * 8, 8).toUInt(&ok, 16);
            memcpy(bytes + w * 4, &word, 4);
        }
        bool allZero = true;
        for (uchar c : bytes)
            allZero = allZero && c == 0;
        const bool v4mapped = !memcmp(bytes, "\0\0\0\0\0\0\0\0\0\0\xff\xff", 12);
        if (allZero) {
            host = QStringLiteral("[::]");
        } else if (v4mapped) {
            host = QStringLiteral("%1.%2.%3.%4").arg(bytes[12]).arg(bytes[13]).arg(bytes[14]).arg(bytes[15]);
        } else {
            QStringList groups;
            for (int g = 0; g < 8; ++g)
                groups << QString::number((bytes[g * 2] << 8) | bytes[g * 2 + 1], 16);
            host = QLatin1Char('[') + groups.join(QLatin1Char(':')) + QLatin1Char(']');
        }
    } else {
        host = QString::fromLatin1(addr);
    }
    return host + QLatin1Char(':') + (port ? QString::number(port) : QStringLiteral("*"));
}

void parseNetSockets(const QByteArray &data, const QString &proto, QHash<quint64, SocketInfo> &out)
{
    const QList<QByteArray> lines = data.split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines[i].simplified().split(' ');
        if (f.size() < 10)
            continue;
        const quint64 inode = f[9].toULongLong();
        if (!inode)
            continue;
        SocketInfo s;
        s.proto = proto;
        s.port = f[1].mid(f[1].lastIndexOf(':') + 1).toInt(nullptr, 16);
        s.uid = f[7].toUInt();
        // TCP: state 0A is LISTEN. UDP has no such state; a socket bound to
        // a port with no fixed peer is one that receives.
        s.listening = proto == QLatin1String("TCP") ? f[3] == "0A" : (s.port != 0 && f[2].endsWith(":0000"));
        s.connected = proto == QLatin1String("TCP") && f[3] != "0A";
        s.local = hexAddress(f[1]);
        s.remote = hexAddress(f[2]);
        out.insert(inode, s);
    }
}

void parseNetUnix(const QByteArray &data, QHash<quint64, SocketInfo> &out)
{
    const QList<QByteArray> lines = data.split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = lines[i].simplified().split(' ');
        if (f.size() < 7)
            continue;
        SocketInfo s;
        s.proto = QStringLiteral("unix");
        if (f.size() > 7)
            s.local = QString::fromUtf8(f[7]);
        out.insert(f[6].toULongLong(), s);
    }
}

} // namespace procfs
