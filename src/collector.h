// Runs the Sampler on its own thread and timer, so a slow /proc read (it
// can block when memory is short) never stalls the interface.
#pragma once

#include "sampler.h"

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <QTimer>

class CollectorWorker : public QObject
{
    Q_OBJECT
public:
    CollectorWorker(const QString &procRoot, const QString &sysRoot);
    SnapshotPtr sampleNow();

public slots:
    void start(int intervalMs);
    void setInterval(int intervalMs);
    void setMemoryDetail(bool on);
    void setPortDetail(bool on);
    void setIoDetail(bool on);
    void refresh();

signals:
    void sampled(SnapshotPtr snapshot);

private:
    Sampler m_sampler;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
};

class Collector : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)

public:
    explicit Collector(const QString &procRoot = QStringLiteral("/proc"),
                       const QString &sysRoot = QStringLiteral("/sys"), QObject *parent = nullptr);
    ~Collector() override;

    // The first sample, taken on the calling thread before the window
    // exists, so the list is filled when it appears.
    SnapshotPtr first() const { return m_first; }
    void start();

    int interval() const { return m_interval; } // seconds: 1, 2 or 5
    void setInterval(int seconds);
    QString procRoot() const { return m_procRoot; }

    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE void setMemoryDetail(bool on);
    void setPortDetail(bool on);
    void setIoDetail(bool on);

signals:
    void snapshot(SnapshotPtr snapshot);
    void intervalChanged();

private:
    QString m_procRoot;
    QThread m_thread;
    CollectorWorker *m_worker;
    SnapshotPtr m_first;
    int m_interval = 2;
};
