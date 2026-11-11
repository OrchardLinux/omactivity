#include "systeminfo.h"
#include "processmodel.h"

#include <cmath>

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
}

QVariantList SystemInfo::toList(const QVector<float> &v)
{
    QVariantList out;
    out.reserve(v.size());
    for (float f : v)
        out << double(f);
    return out;
}

QVariantList SystemInfo::coreHistories() const
{
    QVariantList out;
    for (const QVector<float> &core : m_coreHist)
        out << QVariant(toList(core));
    return out;
}

void SystemInfo::push(QVector<float> &v, float value) const
{
    v.append(value);
    if (v.size() > m_cap)
        v.remove(0, v.size() - m_cap);
}

QString SystemInfo::formatUptime(double seconds)
{
    const qint64 s = qint64(seconds);
    const QString hm = QStringLiteral("%1:%2").arg(s / 3600 % 24, 2, 10, QLatin1Char('0')).arg(s / 60 % 60, 2, 10, QLatin1Char('0'));
    return s >= 86400 ? QStringLiteral("%1d %2").arg(s / 86400).arg(hm) : hm;
}

void SystemInfo::setSnapshot(SnapshotPtr snap)
{
    if (!snap)
        return;
    const SystemSample &s = snap->sys;
    m_user = s.userPercent;
    m_system = s.systemPercent;
    m_idle = s.idlePercent;

    if (snap->interval > 0.2)
        m_cap = qBound(24, int(std::lround(120.0 / snap->interval)), 120);

    m_load = QStringLiteral("%1 %2 %3").arg(s.load1, 0, 'f', 2).arg(s.load5, 0, 'f', 2).arg(s.load15, 0, 'f', 2);
    m_processes = ProcessModel::formatCount(s.processes);
    m_threads = ProcessModel::formatCount(s.threads);
    m_frequency = s.frequencyKhz <= 0 ? QString()
                  : s.frequencyKhz >= 1e6 ? QString::number(s.frequencyKhz / 1e6, 'f', 2) + QLatin1String(" GHz")
                                          : QString::number(s.frequencyKhz / 1e3, 'f', 0) + QLatin1String(" MHz");
    m_uptime = formatUptime(s.uptimeSeconds);

    m_memTotal = ProcessModel::formatBytes(s.memTotalKb);
    m_memUsed = ProcessModel::formatBytes(s.memUsedKb);
    m_memCached = ProcessModel::formatBytes(s.cachedKb);
    m_swapUsed = ProcessModel::formatBytes(s.swapUsedKb);
    m_compressed = s.compressedKb < 0 ? QString() : ProcessModel::formatBytes(s.compressedKb);
    m_memUsedPercent = s.memTotalKb > 0 ? double(s.memUsedKb) / double(s.memTotalKb) * 100.0 : 0;

    m_pressureAvailable = s.pressure.available;
    m_some10 = s.pressure.some10;
    m_full10 = s.pressure.full10;
    if (m_pressureAvailable) {
        m_pressureNow = QStringLiteral("some %1 · full %2").arg(s.pressure.some10, 0, 'f', 1).arg(s.pressure.full10, 0, 'f', 1);
        m_pressureAverages = QStringLiteral("%1 · %2").arg(s.pressure.some60, 0, 'f', 1).arg(s.pressure.some300, 0, 'f', 1);
    } else {
        m_pressureNow.clear();
        m_pressureAverages.clear();
    }

    // The first sample has no interval behind it; its CPU numbers are the
    // average since boot and don't belong in a graph of the last minutes.
    if (snap->interval > 0) {
        push(m_userHist, float(s.userPercent));
        push(m_systemHist, float(s.systemPercent));
        if (m_coreHist.size() != s.corePercent.size())
            m_coreHist = QVector<QVector<float>>(s.corePercent.size());
        for (int i = 0; i < s.corePercent.size(); ++i)
            push(m_coreHist[i], s.corePercent[i]);
    }
    push(m_pressureHist, float(m_pressureAvailable ? s.pressure.some10 : m_memUsedPercent));
    push(m_memUsedHist, float(m_memUsedPercent));

    const QString dash = QStringLiteral("—");
    m_hasBattery = s.hasBattery;
    m_batteryLevel = s.batteryPercent >= 0 ? QString::number(s.batteryPercent) + QLatin1Char('%') : dash;
    m_batteryState = s.hasBattery ? (s.batteryStatus.isEmpty() ? dash : s.batteryStatus) : QStringLiteral("No battery");
    m_batteryTimeLabel = s.batteryStatus == QLatin1String("Charging") ? QStringLiteral("Time until full") : QStringLiteral("Time left");
    if (s.batterySeconds >= 0) {
        const qint64 minutes = qint64(s.batterySeconds / 60);
        m_batteryTime = QStringLiteral("%1:%2").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
    } else {
        m_batteryTime = dash;
    }
    m_powerDraw = s.powerWatts >= 0 ? QString::number(s.powerWatts, 'f', 2) + QLatin1String(" W") : dash;

    m_diskReads = s.diskKnown ? ProcessModel::formatCount(qint64(std::llround(s.diskReadsPerSec))) : dash;
    m_diskWrites = s.diskKnown ? ProcessModel::formatCount(qint64(std::llround(s.diskWritesPerSec))) : dash;
    m_diskReadRate = s.diskKnown ? ProcessModel::formatRate(s.diskReadBytesPerSec) : dash;
    m_diskWriteRate = s.diskKnown ? ProcessModel::formatRate(s.diskWriteBytesPerSec) : dash;
    m_diskFree = dash;
    m_mountLines.clear();
    for (const SystemSample::Mount &m : s.mounts) {
        const QString text = QStringLiteral("%1 of %2").arg(ProcessModel::formatBytes(m.freeKb), ProcessModel::formatBytes(m.totalKb));
        if (m.path == QLatin1String("/"))
            m_diskFree = text;
        m_mountLines << m.path + QLatin1String(": ") + text + QLatin1String(" free");
    }

    m_netRxRate = s.netKnown ? ProcessModel::formatRate(s.netRxBytesPerSec) : dash;
    m_netTxRate = s.netKnown ? ProcessModel::formatRate(s.netTxBytesPerSec) : dash;
    m_netRxTotal = s.netKnown ? ProcessModel::formatByteCount(qint64(s.netRxBytes)) : dash;
    m_netTxTotal = s.netKnown ? ProcessModel::formatByteCount(qint64(s.netTxBytes)) : dash;
    m_netInterface = s.netInterface.isEmpty() ? dash : s.netInterface;

    if (snap->interval > 0) {
        push(m_diskReadHist, float(s.diskReadBytesPerSec));
        push(m_diskWriteHist, float(s.diskWriteBytesPerSec));
        push(m_netRxHist, float(s.netRxBytesPerSec));
        push(m_netTxHist, float(s.netTxBytesPerSec));
    }
    if (s.powerWatts >= 0)
        push(m_powerHist, float(s.powerWatts));
    emit changed();
}
