#include "collector.h"

#include <QSettings>

CollectorWorker::CollectorWorker(const QString &procRoot, const QString &sysRoot)
    : m_sampler(procRoot, sysRoot)
{
}

SnapshotPtr CollectorWorker::sampleNow()
{
    const double elapsed = m_clock.isValid() ? m_clock.restart() / 1000.0 : 0;
    if (!m_clock.isValid())
        m_clock.start();
    return m_sampler.sample(elapsed);
}

void CollectorWorker::start(int intervalMs)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(intervalMs);
    connect(m_timer, &QTimer::timeout, this, &CollectorWorker::refresh);
    m_timer->start();
    // CPU percentages need two samples; don't make the user wait a whole
    // interval for the first ones.
    QTimer::singleShot(500, this, &CollectorWorker::refresh);
}

void CollectorWorker::setInterval(int intervalMs)
{
    if (m_timer)
        m_timer->start(intervalMs);
}

void CollectorWorker::setMemoryDetail(bool on)
{
    m_sampler.setMemoryDetail(on);
}

void CollectorWorker::setPortDetail(bool on)
{
    m_sampler.setPortDetail(on);
}

void CollectorWorker::setIoDetail(bool on)
{
    m_sampler.setIoDetail(on);
}

void CollectorWorker::refresh()
{
    // Two samples a few milliseconds apart give meaningless percentages.
    if (m_clock.isValid() && m_clock.elapsed() < 200)
        return;
    emit sampled(sampleNow());
}

Collector::Collector(const QString &procRoot, const QString &sysRoot, QObject *parent)
    : QObject(parent)
    , m_procRoot(procRoot)
    , m_worker(new CollectorWorker(procRoot, sysRoot))
{
    qRegisterMetaType<SnapshotPtr>("SnapshotPtr");
    const int saved = QSettings().value(QStringLiteral("refresh"), 2).toInt();
    m_interval = (saved == 1 || saved == 5) ? saved : 2;
    m_first = m_worker->sampleNow();
    m_thread.setObjectName(QStringLiteral("collector")); // the thread's name in /proc, for measuring it
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &CollectorWorker::sampled, this, &Collector::snapshot);
}

Collector::~Collector()
{
    m_thread.quit();
    m_thread.wait();
}

void Collector::start()
{
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection, Q_ARG(int, m_interval * 1000));
}

void Collector::setInterval(int seconds)
{
    if (seconds != 1 && seconds != 2 && seconds != 5)
        return;
    if (seconds == m_interval)
        return;
    m_interval = seconds;
    QSettings().setValue(QStringLiteral("refresh"), seconds);
    QMetaObject::invokeMethod(m_worker, "setInterval", Qt::QueuedConnection, Q_ARG(int, seconds * 1000));
    emit intervalChanged();
}

void Collector::refreshNow()
{
    QMetaObject::invokeMethod(m_worker, "refresh", Qt::QueuedConnection);
}

void Collector::setPortDetail(bool on)
{
    QMetaObject::invokeMethod(m_worker, "setPortDetail", Qt::QueuedConnection, Q_ARG(bool, on));
}

void Collector::setIoDetail(bool on)
{
    QMetaObject::invokeMethod(m_worker, "setIoDetail", Qt::QueuedConnection, Q_ARG(bool, on));
}

void Collector::setMemoryDetail(bool on)
{
    QMetaObject::invokeMethod(m_worker, "setMemoryDetail", Qt::QueuedConnection, Q_ARG(bool, on));
}
