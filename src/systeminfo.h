// System totals for the tab footers, plus their in-memory history. The
// history starts when the app opens; the kernel's own averages (load,
// pressure) cover the time before that.
#pragma once

#include "sampler.h"

#include <QObject>
#include <QVariant>

class SystemInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double userPercent MEMBER m_user NOTIFY changed)
    Q_PROPERTY(double systemPercent MEMBER m_system NOTIFY changed)
    Q_PROPERTY(double idlePercent MEMBER m_idle NOTIFY changed)
    Q_PROPERTY(QVariantList userHistory READ userHistory NOTIFY changed)
    Q_PROPERTY(QVariantList systemHistory READ systemHistory NOTIFY changed)
    Q_PROPERTY(QVariantList coreHistories READ coreHistories NOTIFY changed)
    Q_PROPERTY(int historyLength MEMBER m_cap NOTIFY changed)
    Q_PROPERTY(QString loadAverage MEMBER m_load NOTIFY changed)
    Q_PROPERTY(QString processes MEMBER m_processes NOTIFY changed)
    Q_PROPERTY(QString threads MEMBER m_threads NOTIFY changed)
    Q_PROPERTY(QString frequency MEMBER m_frequency NOTIFY changed)
    Q_PROPERTY(QString uptime MEMBER m_uptime NOTIFY changed)
    Q_PROPERTY(QString memTotal MEMBER m_memTotal NOTIFY changed)
    Q_PROPERTY(QString memUsed MEMBER m_memUsed NOTIFY changed)
    Q_PROPERTY(QString memCached MEMBER m_memCached NOTIFY changed)
    Q_PROPERTY(QString swapUsed MEMBER m_swapUsed NOTIFY changed)
    Q_PROPERTY(QString compressed MEMBER m_compressed NOTIFY changed)
    Q_PROPERTY(double memUsedPercent MEMBER m_memUsedPercent NOTIFY changed)
    Q_PROPERTY(bool pressureAvailable MEMBER m_pressureAvailable NOTIFY changed)
    Q_PROPERTY(double pressureSome MEMBER m_some10 NOTIFY changed)
    Q_PROPERTY(double pressureFull MEMBER m_full10 NOTIFY changed)
    Q_PROPERTY(QString pressureNow MEMBER m_pressureNow NOTIFY changed)
    Q_PROPERTY(QString pressureAverages MEMBER m_pressureAverages NOTIFY changed)
    // Memory pressure (some avg10, 0-100) per sample; memory used % when
    // the kernel has no pressure information.
    Q_PROPERTY(QVariantList pressureHistory READ pressureHistory NOTIFY changed)
    Q_PROPERTY(QVariantList memUsedHistory READ memUsedHistory NOTIFY changed) // % of physical memory

    // Energy
    Q_PROPERTY(bool hasBattery MEMBER m_hasBattery NOTIFY changed)
    Q_PROPERTY(QString batteryLevel MEMBER m_batteryLevel NOTIFY changed)
    Q_PROPERTY(QString batteryState MEMBER m_batteryState NOTIFY changed)
    Q_PROPERTY(QString batteryTime MEMBER m_batteryTime NOTIFY changed)
    Q_PROPERTY(QString batteryTimeLabel MEMBER m_batteryTimeLabel NOTIFY changed)
    Q_PROPERTY(QString powerDraw MEMBER m_powerDraw NOTIFY changed)
    Q_PROPERTY(QVariantList powerHistory READ powerHistory NOTIFY changed) // watts
    // Disk
    Q_PROPERTY(QString diskReads MEMBER m_diskReads NOTIFY changed)
    Q_PROPERTY(QString diskWrites MEMBER m_diskWrites NOTIFY changed)
    Q_PROPERTY(QString diskReadRate MEMBER m_diskReadRate NOTIFY changed)
    Q_PROPERTY(QString diskWriteRate MEMBER m_diskWriteRate NOTIFY changed)
    Q_PROPERTY(QString diskFree MEMBER m_diskFree NOTIFY changed)          // "12.3 GB of 59.0 GB" on /
    Q_PROPERTY(QStringList mounts MEMBER m_mountLines NOTIFY changed)       // "/boot: 180.2 MB of 511.0 MB free"
    Q_PROPERTY(QVariantList diskReadHistory READ diskReadHistory NOTIFY changed)   // bytes a second
    Q_PROPERTY(QVariantList diskWriteHistory READ diskWriteHistory NOTIFY changed)
    // Network
    Q_PROPERTY(QString netRxRate MEMBER m_netRxRate NOTIFY changed)
    Q_PROPERTY(QString netTxRate MEMBER m_netTxRate NOTIFY changed)
    Q_PROPERTY(QString netRxTotal MEMBER m_netRxTotal NOTIFY changed)
    Q_PROPERTY(QString netTxTotal MEMBER m_netTxTotal NOTIFY changed)
    Q_PROPERTY(QString netInterface MEMBER m_netInterface NOTIFY changed)
    Q_PROPERTY(QVariantList netRxHistory READ netRxHistory NOTIFY changed) // bytes a second
    Q_PROPERTY(QVariantList netTxHistory READ netTxHistory NOTIFY changed)

public:
    explicit SystemInfo(QObject *parent = nullptr);

    QVariantList userHistory() const { return toList(m_userHist); }
    QVariantList systemHistory() const { return toList(m_systemHist); }
    QVariantList pressureHistory() const { return toList(m_pressureHist); }
    QVariantList coreHistories() const;
    QVariantList memUsedHistory() const { return toList(m_memUsedHist); }
    QVariantList powerHistory() const { return toList(m_powerHist); }
    QVariantList diskReadHistory() const { return toList(m_diskReadHist); }
    QVariantList diskWriteHistory() const { return toList(m_diskWriteHist); }
    QVariantList netRxHistory() const { return toList(m_netRxHist); }
    QVariantList netTxHistory() const { return toList(m_netTxHist); }

    static QString formatUptime(double seconds);

public slots:
    void setSnapshot(SnapshotPtr snap);

signals:
    void changed();

private:
    static QVariantList toList(const QVector<float> &v);
    void push(QVector<float> &v, float value) const;

    double m_user = 0, m_system = 0, m_idle = 100;
    QVector<float> m_userHist, m_systemHist, m_pressureHist;
    QVector<QVector<float>> m_coreHist;
    int m_cap = 60;
    QString m_load, m_processes, m_threads, m_frequency, m_uptime;
    QString m_memTotal, m_memUsed, m_memCached, m_swapUsed, m_compressed;
    QString m_pressureNow, m_pressureAverages;
    double m_memUsedPercent = 0;
    bool m_pressureAvailable = false;
    double m_some10 = 0, m_full10 = 0;

    bool m_hasBattery = false;
    QString m_batteryLevel, m_batteryState, m_batteryTime, m_batteryTimeLabel, m_powerDraw;
    QString m_diskReads, m_diskWrites, m_diskReadRate, m_diskWriteRate, m_diskFree;
    QStringList m_mountLines;
    QString m_netRxRate, m_netTxRate, m_netRxTotal, m_netTxTotal, m_netInterface;
    QVector<float> m_memUsedHist;
    QVector<float> m_powerHist, m_diskReadHist, m_diskWriteHist, m_netRxHist, m_netTxHist;
};
