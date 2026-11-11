#include "processmodel.h"

#include <QSettings>

#include <algorithm>
#include <cmath>
#include <functional>
#include <pwd.h>
#include <unistd.h>

namespace {
const QString kApps = QStringLiteral("apps");
const QString kTree = QStringLiteral("tree");
const QString kSystemKey = QStringLiteral("g:system");
const QString kBackgroundKey = QStringLiteral("g:background");

struct ViewDef {
    const char *id;
    const char *title;
};
const ViewDef kViews[] = {
    {"apps", "Apps"},
    {"all", "All processes"},
    {"tree", "All processes, hierarchically"},
    {"mine", "My processes"},
    {"system", "System processes"},
    {"others", "Other users' processes"},
    {"active", "Active processes"},
    {"windowed", "Windowed processes"},
    {"ports", "Listening on a port"},
    {"services", "By service"},
    {"selected", "Selected processes"},
};

qint64 addKnown(qint64 sum, qint64 v)
{
    if (v < 0)
        return sum;
    return (sum < 0 ? 0 : sum) + v;
}
} // namespace

QString ProcessModel::defaultSort(const QString &tab)
{
    if (tab == QLatin1String("memory")) return QStringLiteral("memory");
    if (tab == QLatin1String("disk")) return QStringLiteral("written");
    if (tab == QLatin1String("network")) return QStringLiteral("connections");
    return QStringLiteral("cpu");
}

bool ProcessModel::validTab(const QString &tab)
{
    return tab == QLatin1String("cpu") || tab == QLatin1String("memory") || tab == QLatin1String("energy")
        || tab == QLatin1String("disk") || tab == QLatin1String("network");
}

const QVector<ProcessModel::Column> &ProcessModel::columnDefs()
{
    // In display order. "tabs" is where a column can be shown, "defaults"
    // where it is on until the user changes the column picker.
    static const QVector<Column> defs = {
        {"cpu", "% CPU", 70, "cpu,energy", "cpu,energy"},
        {"avgCpu", "Avg CPU", 84, "cpu,energy", "energy"},
        {"cpuTime", "CPU Time", 90, "cpu,energy", "cpu"},
        {"cpuWait", "CPU Wait", 90, "cpu", ""},
        {"ioWait", "I/O Wait", 90, "cpu,disk", ""},
        {"memory", "Memory", 100, "cpu,memory", "memory"},
        {"real", "Real Memory", 116, "memory", ""},
        {"private", "Private Memory", 136, "memory", ""},
        {"shared", "Shared Memory", 128, "memory", ""},
        {"swap", "Swap", 88, "memory", "memory"},
        {"written", "Bytes Written", 140, "disk", "disk"},
        {"read", "Bytes Read", 108, "disk", "disk"},
        {"writeRate", "Written/s", 104, "disk", "disk"},
        {"readRate", "Read/s", 100, "disk", "disk"},
        {"connections", "Connections", 124, "network", "network"},
        {"sockets", "Sockets", 80, "network", "network"},
        {"threads", "Threads", 68, "cpu,memory,energy", "cpu,memory"},
        {"wakeups", "Idle Wake Ups", 112, "cpu,energy", "cpu,energy"},
        {"sleep", "Prevents Sleep", 124, "energy", "energy"},
        {"nice", "Nice", 60, "cpu,memory,energy", ""},
        {"state", "State", 92, "cpu,memory,energy,disk,network", ""},
        {"pid", "PID", 70, "cpu,memory,energy,disk,network", "cpu,memory,energy,disk,network"},
        {"user", "User", 76, "cpu,memory,energy,disk,network", "cpu,memory,energy,disk,network"},
        {"ports", "Ports", 150, "cpu,memory,network", "network"},
        {"command", "Command", 360, "cpu,memory,energy,disk,network", ""},
    };
    return defs;
}

ProcessModel::ProcessModel(AppIndex *apps, const ProtectedList *prot, QObject *parent, int uid)
    : QAbstractListModel(parent)
    , m_apps(apps)
    , m_prot(prot)
    , m_myUid(uid < 0 ? getuid() : uint(uid))
{
    QSettings s;
    const QString tab = s.value(QStringLiteral("tab"), m_tab).toString();
    if (validTab(tab))
        m_tab = tab;
    const QString view = s.value(QStringLiteral("view"), m_view).toString();
    // Only the views that show everything are kept between runs. A filter
    // (ports, active, other users...) can be empty, and an app that opens
    // on an empty list looks broken.
    for (const char *whole : {"apps", "all", "tree", "services"}) {
        if (view == QLatin1String(whole))
            m_view = view;
    }
    loadTabState();
    m_wantedPorts = wantsPorts();
}

void ProcessModel::loadTabState()
{
    QSettings s;
    m_sortKey = s.value(QStringLiteral("sort/%1/key").arg(m_tab), defaultSort(m_tab)).toString();
    m_sortDesc = s.value(QStringLiteral("sort/%1/descending").arg(m_tab), true).toBool();
    m_visibleCols = visibleColumnKeys();
    if (m_sortKey != QLatin1String("name") && !m_visibleCols.contains(m_sortKey)) {
        m_sortKey = defaultSort(m_tab);
        m_sortDesc = true;
    }
}

void ProcessModel::saveState() const
{
    QSettings s;
    s.setValue(QStringLiteral("tab"), m_tab);
    s.setValue(QStringLiteral("view"), m_view);
    s.setValue(QStringLiteral("sort/%1/key").arg(m_tab), m_sortKey);
    s.setValue(QStringLiteral("sort/%1/descending").arg(m_tab), m_sortDesc);
}

QStringList ProcessModel::configuredColumns(const QString &tab) const
{
    QStringList defaults;
    for (const Column &c : columnDefs()) {
        if (QString::fromLatin1(c.defaults).split(QLatin1Char(',')).contains(tab))
            defaults << QLatin1String(c.key);
    }
    QStringList chosen = QSettings().value(QStringLiteral("columns/%1").arg(tab), defaults).toStringList();
    if (!chosen.contains(QLatin1String("pid")))
        chosen << QStringLiteral("pid"); // always present
    return chosen;
}

QStringList ProcessModel::visibleColumnKeys() const
{
    const QStringList chosen = configuredColumns(m_tab);
    QStringList keys;
    for (const Column &c : columnDefs()) {
        const QString key = QLatin1String(c.key);
        if (chosen.contains(key) && QString::fromLatin1(c.tabs).split(QLatin1Char(',')).contains(m_tab))
            keys << key;
    }
    // The ports view is about the Ports column: it leads there.
    if (m_view == QLatin1String("ports")) {
        keys.removeAll(QStringLiteral("ports"));
        keys.prepend(QStringLiteral("ports"));
    }
    // A phone shows two columns without scrolling; the one the list is
    // sorted by must be among them.
    if (m_narrow && keys.indexOf(m_sortKey) > 1)
        keys.move(keys.indexOf(m_sortKey), 0);
    return keys;
}

int ProcessModel::matchCount() const
{
    int n = 0;
    if (m_snap) {
        for (const ProcSample &p : m_snap->procs)
            n += matchesSearch(p) ? 1 : 0;
    }
    return n;
}

bool ProcessModel::wantsPorts() const
{
    // The Network tab's columns come from the same look at every process's sockets.
    return m_view == QLatin1String("ports") || m_tab == QLatin1String("network") || m_visibleCols.contains(QLatin1String("ports"));
}

QString ProcessModel::otherPorts() const
{
    if (!m_snap)
        return QString();
    QStringList parts;
    QList<uint> uids = m_snap->sys.unattributedPorts.keys();
    std::sort(uids.begin(), uids.end());
    for (uint uid : std::as_const(uids))
        parts << userName(uid) + QLatin1String(": ") + m_snap->sys.unattributedPorts.value(uid);
    return parts.join(QLatin1String("  ·  "));
}

void ProcessModel::checkWantsPorts()
{
    if (wantsPorts() != m_wantedPorts) {
        m_wantedPorts = wantsPorts();
        emit wantsPortsChanged();
    }
}

QString ProcessModel::unitOf(const QString &cgroup)
{
    const QStringList parts = cgroup.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = parts.size() - 1; i >= 0; --i) {
        if (parts[i].endsWith(QLatin1String(".service")) || parts[i].endsWith(QLatin1String(".scope")))
            return parts[i];
    }
    return QString();
}

QVariantList ProcessModel::columns() const
{
    QVariantList out;
    for (const QString &key : m_visibleCols) {
        for (const Column &c : columnDefs()) {
            if (key == QLatin1String(c.key))
                out << QVariantMap{{"key", key},
                                   {"title", QString::fromLatin1(c.title)},
                                   {"width", c.width},
                                   {"left", key == QLatin1String("user") || key == QLatin1String("state") || key == QLatin1String("sleep")
                                                || key == QLatin1String("ports") || key == QLatin1String("command")}};
        }
    }
    return out;
}

QVariantList ProcessModel::allColumns() const
{
    QVariantList out;
    for (const Column &c : columnDefs()) {
        if (!QString::fromLatin1(c.tabs).split(QLatin1Char(',')).contains(m_tab))
            continue;
        const QString key = QLatin1String(c.key);
        out << QVariantMap{{"key", key},
                           {"title", QString::fromLatin1(c.title)},
                           {"visible", m_visibleCols.contains(key)},
                           {"fixed", key == QLatin1String("pid")}};
    }
    return out;
}

void ProcessModel::setColumnVisible(const QString &key, bool visible)
{
    if (key == QLatin1String("pid"))
        return;
    QStringList chosen = configuredColumns(m_tab);
    if (visible && !chosen.contains(key))
        chosen << key;
    else if (!visible)
        chosen.removeAll(key);
    QSettings().setValue(QStringLiteral("columns/%1").arg(m_tab), chosen);
    m_visibleCols = visibleColumnKeys();
    checkWantsPorts();
    if (!visible && m_sortKey == key)
        sortBy(key != defaultSort(m_tab) ? defaultSort(m_tab) : m_visibleCols.value(0));
    emit columnsChanged();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {CellsRole});
}

void ProcessModel::setTab(const QString &tab)
{
    if (tab == m_tab || !validTab(tab))
        return;
    m_tab = tab;
    loadTabState();
    saveState();
    checkWantsPorts();
    emit tabChanged();
    emit columnsChanged();
    emit sortChanged();
    rebuild(false);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));
}

void ProcessModel::setNarrow(bool n)
{
    if (n == m_narrow)
        return;
    m_narrow = n;
    m_visibleCols = visibleColumnKeys();
    emit columnsChanged();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {CellsRole});
}

void ProcessModel::setView(const QString &view)
{
    if (view == m_view)
        return;
    m_view = view;
    saveState();
    const QStringList cols = visibleColumnKeys();
    if (cols != m_visibleCols) {
        m_visibleCols = cols;
        emit columnsChanged();
    }
    checkWantsPorts();
    emit viewChanged();
    rebuild(false);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {CellsRole});
}

QString ProcessModel::viewTitle() const
{
    for (const ViewDef &v : kViews) {
        if (m_view == QLatin1String(v.id))
            return QString::fromLatin1(v.title);
    }
    return QString();
}

QVariantList ProcessModel::views() const
{
    QVariantList out;
    for (const ViewDef &v : kViews)
        out << QVariantMap{{"id", QString::fromLatin1(v.id)}, {"title", QString::fromLatin1(v.title)}};
    return out;
}

void ProcessModel::setSearch(const QString &s)
{
    if (s == m_search)
        return;
    m_search = s;
    emit searchChanged();
    rebuild(false);
    emit updated();
}

void ProcessModel::sortBy(const QString &key)
{
    if (key.isEmpty())
        return;
    if (key == m_sortKey) {
        m_sortDesc = !m_sortDesc;
    } else {
        m_sortKey = key;
        // Numbers start largest first, names A to Z.
        m_sortDesc = key != QLatin1String("name") && key != QLatin1String("user") && key != QLatin1String("pid")
                     && key != QLatin1String("ports") && key != QLatin1String("command") && key != QLatin1String("state");
    }
    saveState();
    const QStringList cols = visibleColumnKeys();
    if (cols != m_visibleCols) {
        m_visibleCols = cols;
        emit columnsChanged();
    }
    emit sortChanged();
    rebuild(false);
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {CellsRole});
}

void ProcessModel::setHold(bool h)
{
    if (h == m_hold)
        return;
    m_hold = h;
    emit holdChanged();
    if (!m_hold && m_pending)
        rebuild(false);
}

void ProcessModel::setWindowAppIds(const QStringList &ids)
{
    QSet<QString> set;
    for (const QString &id : ids)
        set.insert(id.toLower());
    if (set == m_windowIds)
        return;
    m_windowIds = set;
    if (m_view == QLatin1String("windowed"))
        rebuild(false);
}

void ProcessModel::setSleepBlockers(const QHash<int, QString> &blockers)
{
    if (blockers == m_sleepBlockers)
        return;
    m_sleepBlockers = blockers;
    rebuild(false);
}

void ProcessModel::setNotResponding(const QStringList &keys)
{
    m_notResponding = QSet<QString>(keys.begin(), keys.end());
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {NotRespondingRole});
}

int ProcessModel::historyLength() const
{
    const double interval = m_snap && m_snap->interval > 0.2 ? m_snap->interval : 2.0;
    return qBound(24, int(std::lround(120.0 / interval)), 120);
}

void ProcessModel::setSnapshot(SnapshotPtr snap)
{
    if (!snap)
        return;
    const int before = processCount();
    m_snap = snap;
    m_pidIndex.clear();
    m_pidIndex.reserve(snap->procs.size());
    for (int i = 0; i < snap->procs.size(); ++i)
        m_pidIndex.insert(snap->procs[i].pid, i);

    if (snap->interval > 0) {
        const int cap = historyLength();
        QHash<quint64, QVector<float>> next;
        next.reserve(snap->procs.size());
        for (const ProcSample &p : snap->procs) {
            QVector<float> h = m_history.value(p.id());
            h.append(float(p.cpuPercent));
            if (h.size() > cap)
                h.remove(0, h.size() - cap);
            next.insert(p.id(), h);
        }
        m_history = next;
    }

    // Selected rows whose process is gone stop being selected.
    bool selectionDirty = false;
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (!target(*it).valid() && *it != kSystemKey && *it != kBackgroundKey) {
            if (m_current == *it)
                m_current.clear();
            it = m_selected.erase(it);
            selectionDirty = true;
        } else {
            ++it;
        }
    }

    if (m_current.isEmpty() && !m_selected.isEmpty())
        m_current = *m_selected.constBegin();

    rebuild(true);
    if (before != processCount())
        emit countsChanged();
    if (selectionDirty)
        emit selectionChanged();
    emit updated();
}

QString ProcessModel::processKey(const ProcSample &p)
{
    return QLatin1Char('p') + QString::number(p.pid) + QLatin1Char('.') + QString::number(p.startTicks);
}

QString ProcessModel::userName(uint uid) const
{
    auto it = m_users.constFind(uid);
    if (it != m_users.constEnd())
        return *it;
    const passwd *pw = getpwuid(uid);
    const QString name = pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(uid);
    m_users.insert(uid, name);
    return name;
}

const ProcSample *ProcessModel::procByPid(int pid) const
{
    if (!m_snap)
        return nullptr;
    const auto it = m_pidIndex.constFind(pid);
    return it == m_pidIndex.constEnd() ? nullptr : &m_snap->procs[*it];
}

const ProcSample *ProcessModel::procByKey(const QString &key) const
{
    if (!key.startsWith(QLatin1Char('p')))
        return nullptr;
    const int dot = key.indexOf(QLatin1Char('.'));
    const ProcSample *p = procByPid(key.mid(1, dot - 1).toInt());
    return p && processKey(*p) == key ? p : nullptr;
}

QString ProcessModel::keyForPid(int pid) const
{
    const ProcSample *p = procByPid(pid);
    return p ? processKey(*p) : QString();
}

QString ProcessModel::findByName(const QString &name) const
{
    for (const Row &r : m_rows) {
        if (r.name == name)
            return r.key;
    }
    if (m_snap) {
        for (const ProcSample &p : m_snap->procs) {
            if (p.name == name)
                return processKey(p);
        }
    }
    return QString();
}

bool ProcessModel::matchesSearch(const ProcSample &p) const
{
    if (m_search.isEmpty() || p.name.contains(m_search, Qt::CaseInsensitive) || p.cmdline.contains(m_search, Qt::CaseInsensitive))
        return true;
    // ":8080" or "8080" finds whoever listens there, while ports are shown.
    if (p.ports.isEmpty())
        return false;
    const QString wanted = m_search.startsWith(QLatin1Char(':')) ? m_search.mid(1) : m_search;
    const QStringList ports = p.ports.split(QLatin1String(", "));
    for (const QString &port : ports) {
        if (port == wanted || port.section(QLatin1Char('/'), 0, 0) == wanted)
            return true;
    }
    return false;
}

bool ProcessModel::hasWindow(const ProcSample &p) const
{
    if (m_windowIds.isEmpty())
        return false;
    if (!p.appUnit.isEmpty()) {
        const AppEntry &e = m_apps->forUnit(p.appUnit);
        return m_windowIds.contains(e.id.toLower()) || (!e.wmClass.isEmpty() && m_windowIds.contains(e.wmClass.toLower()));
    }
    return m_windowIds.contains(p.name.toLower());
}

bool ProcessModel::passesView(const ProcSample &p) const
{
    if (m_view == QLatin1String("mine"))
        return p.uid == m_myUid;
    if (m_view == QLatin1String("system"))
        return p.uid < 1000;
    if (m_view == QLatin1String("others"))
        return p.uid >= 1000 && p.uid != m_myUid;
    if (m_view == QLatin1String("active"))
        return p.cpuPercent > 0;
    if (m_view == QLatin1String("windowed"))
        return hasWindow(p);
    if (m_view == QLatin1String("ports"))
        return !p.ports.isEmpty();
    if (m_view == QLatin1String("selected"))
        return m_selected.contains(processKey(p)) || (!p.appUnit.isEmpty() && m_selected.contains(QLatin1String("a:") + p.appUnit));
    return true;
}

// Ordering by the newest sample alone makes rows of similar load trade
// places at every refresh. The order follows the average of the last few
// samples instead; the number shown stays the newest one.
double ProcessModel::steadyCpu(const ProcSample &p) const
{
    const auto it = m_history.constFind(p.id());
    if (it == m_history.constEnd() || it->isEmpty())
        return p.cpuPercent;
    const int n = qMin<int>(4, it->size());
    double sum = 0;
    for (int i = it->size() - n; i < it->size(); ++i)
        sum += double((*it)[i]);
    return sum / n;
}

ProcessModel::Row ProcessModel::rowForProc(const ProcSample &p, int depth) const
{
    Row r;
    r.key = processKey(p);
    r.kind = Process;
    r.depth = depth;
    r.pid = p.pid;
    r.startTicks = p.startTicks;
    r.uid = p.uid;
    r.name = p.name;
    r.cpu = p.cpuPercent;
    r.cpuSort = steadyCpu(p);
    r.cpuTicks = p.cpuTicks;
    r.threads = p.threads;
    r.wakeups = p.wakeups;
    r.cpuWait = p.cpuWaitPercent;
    r.ioWait = p.ioWaitPercent;
    r.state = p.state;
    {
        // Average since it started: CPU time over lifetime. The kernel keeps
        // both, so this covers the time before the app was opened.
        const double tps = double(m_snap ? m_snap->ticksPerSecond : 100);
        const double alive = (m_snap ? m_snap->sys.uptimeSeconds : 0) - double(p.startTicks) / tps;
        r.avgCpu = alive > 1 ? double(p.cpuTicks) / tps / alive * 100.0 : p.cpuPercent;
    }
    r.sleepBlock = m_sleepBlockers.value(p.pid);
    r.readBytes = p.readBytes;
    r.writeBytes = p.writeBytes;
    r.readRate = p.readRate;
    r.writeRate = p.writeRate;
    r.sockets = p.sockets;
    r.connections = p.connections;
    r.ports = p.ports;
    r.portsKnown = p.portsKnown;
    r.cmdline = p.cmdline;
    r.nice = p.nice;
    r.mem = p.memKb;
    r.rss = p.rssKb;
    r.priv = p.privateKb;
    r.shared = p.sharedKb;
    r.swap = p.swapKb;
    r.paused = p.state == 'T';
    r.prot = m_prot && m_prot->contains(p.pid, p.name);
    return r;
}

bool ProcessModel::lessThan(const Row &a, const Row &b) const
{
    auto cmp = [&]() -> int {
        auto num = [](double x, double y) { return x < y ? -1 : (x > y ? 1 : 0); };
        if (m_sortKey == QLatin1String("name"))
            return QString::compare(a.name, b.name, Qt::CaseInsensitive);
        if (m_sortKey == QLatin1String("user"))
            return QString::compare(a.pid < 0 ? QString() : userName(a.uid), b.pid < 0 ? QString() : userName(b.uid));
        if (m_sortKey == QLatin1String("cpu")) return num(a.cpuSort, b.cpuSort);
        if (m_sortKey == QLatin1String("cpuTime")) return num(double(a.cpuTicks), double(b.cpuTicks));
        if (m_sortKey == QLatin1String("memory")) return num(double(a.mem), double(b.mem));
        if (m_sortKey == QLatin1String("real")) return num(double(a.rss), double(b.rss));
        if (m_sortKey == QLatin1String("private")) return num(double(a.priv), double(b.priv));
        if (m_sortKey == QLatin1String("shared")) return num(double(a.shared), double(b.shared));
        if (m_sortKey == QLatin1String("swap")) return num(double(a.swap), double(b.swap));
        if (m_sortKey == QLatin1String("threads")) return num(a.threads, b.threads);
        if (m_sortKey == QLatin1String("wakeups")) return num(a.wakeups, b.wakeups);
        if (m_sortKey == QLatin1String("avgCpu")) return num(a.avgCpu, b.avgCpu);
        if (m_sortKey == QLatin1String("written")) return num(double(a.writeBytes), double(b.writeBytes));
        if (m_sortKey == QLatin1String("read")) return num(double(a.readBytes), double(b.readBytes));
        if (m_sortKey == QLatin1String("writeRate")) return num(a.writeRate, b.writeRate);
        if (m_sortKey == QLatin1String("readRate")) return num(a.readRate, b.readRate);
        if (m_sortKey == QLatin1String("connections")) return num(a.connections, b.connections);
        if (m_sortKey == QLatin1String("sockets")) return num(a.sockets, b.sockets);
        if (m_sortKey == QLatin1String("sleep")) return num(a.sleepBlock.isEmpty() ? 0 : 1, b.sleepBlock.isEmpty() ? 0 : 1);
        if (m_sortKey == QLatin1String("cpuWait")) return num(a.cpuWait, b.cpuWait);
        if (m_sortKey == QLatin1String("ioWait")) return num(a.ioWait, b.ioWait);
        if (m_sortKey == QLatin1String("state")) return num(a.state, b.state);
        if (m_sortKey == QLatin1String("ports")) return num(a.ports.section(QLatin1Char(','), 0, 0).section(QLatin1Char('/'), 0, 0).toInt(),
                                                            b.ports.section(QLatin1Char(','), 0, 0).section(QLatin1Char('/'), 0, 0).toInt());
        if (m_sortKey == QLatin1String("command")) return QString::compare(a.cmdline, b.cmdline, Qt::CaseInsensitive);
        if (m_sortKey == QLatin1String("nice")) return num(a.nice, b.nice);
        if (m_sortKey == QLatin1String("pid")) return num(a.pid, b.pid);
        return 0;
    };
    const int c = cmp();
    if (c != 0)
        return m_sortDesc ? c > 0 : c < 0;
    // Equal values keep a fixed order, so idle rows don't shuffle.
    if (a.pid != b.pid)
        return a.pid < b.pid;
    return a.key < b.key;
}

QVector<ProcessModel::Row> ProcessModel::buildRows() const
{
    QVector<Row> out;
    if (!m_snap)
        return out;
    const QVector<ProcSample> &procs = m_snap->procs;
    const bool searching = !m_search.isEmpty();
    auto less = [this](const Row &a, const Row &b) { return lessThan(a, b); };

    const bool byService = m_view == QLatin1String("services");
    if (m_view == kApps || byService) {
        struct G {
            Row row;
            QVector<int> members;
            int main = -1;
        };
        QVector<G> groups;
        QHash<QString, int> byKey;
        for (int i = 0; i < procs.size(); ++i) {
            const ProcSample &p = procs[i];
            QString key;
            if (byService) {
                // Every systemd unit is a row; what runs in none (kernel
                // threads, mostly) is the kernel's.
                const QString unit = unitOf(p.cgroup);
                key = unit.isEmpty() ? QStringLiteral("g:kernel") : QLatin1String("u:") + unit;
            } else if (!p.appUnit.isEmpty())
                key = QLatin1String("a:") + p.appUnit;
            else if (p.kernelThread || p.uid != m_myUid)
                key = kSystemKey;
            else
                key = kBackgroundKey;
            auto it = byKey.find(key);
            if (it == byKey.end()) {
                it = byKey.insert(key, groups.size());
                groups.append(G());
                groups.last().row.key = key;
            }
            G &g = groups[*it];
            g.members.append(i);
            if (g.main < 0 || p.startTicks < procs[g.main].startTicks
                || (p.startTicks == procs[g.main].startTicks && p.pid < procs[g.main].pid))
                g.main = i;
        }

        QVector<QPair<Row, QVector<Row>>> result;
        for (G &g : groups) {
            Row &r = g.row;
            const bool isApp = r.key.startsWith(QLatin1String("a:"));
            r.kind = isApp ? App : Group;
            r.count = g.members.size();
            const ProcSample &main = procs[g.main];
            if (isApp) {
                const AppEntry &e = m_apps->forUnit(main.appUnit);
                r.name = e.name;
                r.glyph = e.glyph;
                r.unit = main.appUnit;
                r.appId = e.id;
                r.pid = main.pid;
                r.startTicks = main.startTicks;
                r.uid = main.uid;
                r.nice = main.nice;
                r.paused = main.state == 'T';
            } else if (byService) {
                r.name = r.key.startsWith(QLatin1String("u:")) ? r.key.mid(2) : QStringLiteral("Kernel");
                r.glyph = r.key.startsWith(QLatin1String("u:app-")) ? QStringLiteral("app")
                          : r.key.endsWith(QLatin1String(".scope")) ? QStringLiteral("layers") : QStringLiteral("cpu");
            } else {
                r.name = r.key == kSystemKey ? QStringLiteral("System") : QStringLiteral("Background");
                r.glyph = r.key == kSystemKey ? QStringLiteral("cpu") : QStringLiteral("layers");
            }
            bool childMatch = false;
            for (int i : g.members) {
                const ProcSample &p = procs[i];
                r.cpu += p.cpuPercent;
                r.cpuSort += steadyCpu(p);
                r.cpuTicks += p.cpuTicks;
                r.threads += p.threads;
                r.wakeups += p.wakeups;
                r.cpuWait += p.cpuWaitPercent;
                {
                    const Row one = rowForProc(p, 0);
                    r.avgCpu += one.avgCpu;
                    if (!one.sleepBlock.isEmpty() && r.sleepBlock.isEmpty())
                        r.sleepBlock = one.sleepBlock;
                }
                r.readBytes = addKnown(r.readBytes, p.readBytes);
                r.writeBytes = addKnown(r.writeBytes, p.writeBytes);
                r.readRate += p.readRate;
                r.writeRate += p.writeRate;
                if (p.sockets >= 0) {
                    r.sockets = qMax(0, r.sockets) + p.sockets;
                    r.connections = qMax(0, r.connections) + p.connections;
                }
                if (p.ioWaitPercent >= 0)
                    r.ioWait = qMax(0.0, r.ioWait) + p.ioWaitPercent;
                r.mem += p.memKb;
                r.rss += p.rssKb;
                r.priv = addKnown(r.priv, p.privateKb);
                r.shared = addKnown(r.shared, p.sharedKb);
                r.swap = addKnown(r.swap, p.swapKb);
                if (isApp && m_prot && m_prot->contains(p.pid, p.name))
                    r.prot = true;
                childMatch = childMatch || (searching && matchesSearch(p));
            }
            const bool nameMatch = searching && r.name.contains(m_search, Qt::CaseInsensitive);
            if (searching && !nameMatch && !childMatch)
                continue;
            r.expandable = !isApp || r.count > 1;
            // A match inside a collapsed app would be invisible, so open it.
            if (byService)
                r.expandable = true;
            r.expanded = r.expandable && (m_expandedApps.contains(r.key) || (searching && !nameMatch));
            QVector<Row> children;
            if (r.expanded) {
                for (int i : g.members) {
                    if (searching && !nameMatch && !matchesSearch(procs[i]))
                        continue;
                    children.append(rowForProc(procs[i], 1));
                }
                std::sort(children.begin(), children.end(), less);
            }
            result.append({r, children});
        }
        std::sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
            // Apps are what the view is for and take part in the sorting.
            // Background and System are always there and always busy with
            // something: they keep their place under the apps, in that order.
            const bool fixedA = !byService && a.first.kind == Group, fixedB = !byService && b.first.kind == Group;
            if (fixedA != fixedB)
                return fixedB;
            if (fixedA)
                return a.first.key < b.first.key; // g:background, g:system
            return lessThan(a.first, b.first);
        });
        for (const auto &pair : result) {
            out.append(pair.first);
            out += pair.second;
        }
        return out;
    }

    if (m_view == kTree) {
        QHash<int, QVector<int>> children;
        QVector<int> roots;
        for (int i = 0; i < procs.size(); ++i) {
            const int ppid = procs[i].ppid;
            if (ppid > 0 && ppid != procs[i].pid && m_pidIndex.contains(ppid))
                children[ppid].append(i);
            else
                roots.append(i);
        }
        // While searching, a row stays when it matches or leads to a match.
        QVector<char> keep(procs.size(), searching ? 0 : 1);
        if (searching) {
            for (int i = 0; i < procs.size(); ++i) {
                if (!matchesSearch(procs[i]))
                    continue;
                int at = i;
                for (int guard = 0; guard < 256 && !keep[at]; ++guard) {
                    keep[at] = 1;
                    const auto parent = m_pidIndex.constFind(procs[at].ppid);
                    if (parent == m_pidIndex.constEnd() || *parent == at)
                        break;
                    at = *parent;
                }
            }
        }
        std::function<void(const QVector<int> &, int)> emitLevel = [&](const QVector<int> &level, int depth) {
            QVector<Row> rows;
            for (int i : level) {
                if (keep[i])
                    rows.append(rowForProc(procs[i], depth));
            }
            std::sort(rows.begin(), rows.end(), less);
            for (Row &r : rows) {
                const QVector<int> kids = children.value(r.pid);
                int visibleKids = 0;
                for (int k : kids)
                    visibleKids += keep[k];
                r.expandable = visibleKids > 0;
                r.count = visibleKids;
                r.expanded = r.expandable && (searching || !m_collapsedTree.contains(r.key));
                out.append(r);
                if (r.expanded && depth < 64)
                    emitLevel(kids, depth + 1);
            }
        };
        emitLevel(roots, 0);
        return out;
    }

    for (const ProcSample &p : procs) {
        if (passesView(p) && matchesSearch(p))
            out.append(rowForProc(p, 0));
    }
    std::sort(out.begin(), out.end(), less);
    return out;
}

void ProcessModel::rebuild(bool fromSnapshot)
{
    QVector<Row> next = buildRows();

    if (m_hold) {
        // A finger is on the list: refresh the numbers in place and leave
        // every row where it is until the finger lifts.
        QHash<QString, int> byKey;
        byKey.reserve(next.size());
        for (int i = 0; i < next.size(); ++i)
            byKey.insert(next[i].key, i);
        for (Row &r : m_rows) {
            const auto it = byKey.constFind(r.key);
            if (it == byKey.constEnd())
                continue;
            const int depth = r.depth;
            const bool expanded = r.expanded, expandable = r.expandable;
            r = next[*it];
            r.depth = depth;
            r.expanded = expanded;
            r.expandable = expandable;
        }
        m_pending = true;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(m_rows.size() - 1));
        return;
    }
    m_pending = false;

    if (fromSnapshot) {
        // A process that ended stays for one refresh, marked as ended, so
        // the row fades out instead of the list jumping under the finger.
        QSet<QString> nextKeys;
        nextKeys.reserve(next.size());
        for (const Row &r : next)
            nextKeys.insert(r.key);
        for (int i = 0; i < m_rows.size(); ++i) {
            const Row &old = m_rows[i];
            if (old.kind != Process || old.ended || nextKeys.contains(old.key) || procByKey(old.key))
                continue;
            Row ghost = old;
            ghost.ended = true;
            ghost.cpu = 0;
            next.insert(qMin(i, next.size()), ghost);
        }
    }
    applyRows(next);
}

void ProcessModel::applyRows(const QVector<Row> &next)
{
    QSet<QString> nextKeys;
    nextKeys.reserve(next.size());
    for (const Row &r : next)
        nextKeys.insert(r.key);

    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (nextKeys.contains(m_rows[i].key))
            continue;
        int first = i;
        while (first > 0 && !nextKeys.contains(m_rows[first - 1].key))
            --first;
        beginRemoveRows(QModelIndex(), first, i);
        m_rows.remove(first, i - first + 1);
        endRemoveRows();
        i = first;
    }

    for (int i = 0; i < next.size(); ++i) {
        if (i < m_rows.size() && m_rows[i].key == next[i].key)
            continue;
        int from = -1;
        for (int j = i + 1; j < m_rows.size(); ++j) {
            if (m_rows[j].key == next[i].key) {
                from = j;
                break;
            }
        }
        if (from < 0) {
            beginInsertRows(QModelIndex(), i, i);
            m_rows.insert(i, next[i]);
            endInsertRows();
        } else {
            beginMoveRows(QModelIndex(), from, from, QModelIndex(), i);
            m_rows.move(from, i);
            endMoveRows();
        }
    }

    m_rows = next;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1));
}

int ProcessModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> ProcessModel::roleNames() const
{
    return {
        {KeyRole, "key"}, {KindRole, "kind"}, {DepthRole, "depth"}, {ExpandableRole, "expandable"},
        {ExpandedRole, "expanded"}, {CountRole, "count"}, {NameRole, "name"}, {GlyphRole, "glyph"},
        {SelectedRole, "selected"}, {PausedRole, "paused"}, {ProtectedRole, "isProtected"},
        {EndedRole, "ended"}, {NotRespondingRole, "notResponding"}, {CellsRole, "cells"}, {HotRole, "hot"}, {BlockedRole, "blocked"},
    };
}

QString ProcessModel::formatCount(qint64 n)
{
    QString s = QString::number(qAbs(n));
    for (int i = s.size() - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char(','));
    return n < 0 ? QLatin1Char('-') + s : s;
}

QString ProcessModel::formatBytes(qint64 kb)
{
    if (kb < 0)
        return QStringLiteral("—");
    if (kb < 1024)
        return QString::number(kb) + QLatin1String(" KB");
    const double mb = kb / 1024.0;
    if (mb < 1000)
        return QString::number(mb, 'f', 1) + QLatin1String(" MB");
    return QString::number(mb / 1024.0, 'f', 2) + QLatin1String(" GB");
}

QString ProcessModel::formatByteCount(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("—");
    if (bytes < 1024)
        return QString::number(bytes) + QLatin1String(" B");
    return formatBytes(bytes / 1024);
}

QString ProcessModel::formatRate(double bytesPerSecond)
{
    return formatByteCount(qint64(std::llround(qMax(0.0, bytesPerSecond)))) + QLatin1String("/s");
}

QString ProcessModel::formatCpuTime(quint64 ticks, long tps)
{
    if (tps <= 0)
        tps = 100;
    const quint64 centis = ticks * 100 / quint64(tps);
    const quint64 secs = centis / 100;
    if (secs >= 3600)
        return QStringLiteral("%1:%2:%3").arg(secs / 3600).arg(secs / 60 % 60, 2, 10, QLatin1Char('0')).arg(secs % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')).arg(centis % 100, 2, 10, QLatin1Char('0'));
}

QString ProcessModel::cell(const Row &r, const QString &col) const
{
    static const QString dash = QStringLiteral("—");
    if (col == QLatin1String("cpu")) return QString::number(r.cpu, 'f', 1);
    if (col == QLatin1String("cpuTime")) return formatCpuTime(r.cpuTicks, m_snap ? m_snap->ticksPerSecond : 100);
    if (col == QLatin1String("memory")) return formatBytes(r.mem);
    if (col == QLatin1String("real")) return formatBytes(r.rss);
    if (col == QLatin1String("private")) return formatBytes(r.priv);
    if (col == QLatin1String("shared")) return formatBytes(r.shared);
    if (col == QLatin1String("swap")) return formatBytes(r.swap);
    if (col == QLatin1String("threads")) return formatCount(r.threads);
    if (col == QLatin1String("wakeups")) return formatCount(qint64(std::llround(r.wakeups)));
    if (col == QLatin1String("avgCpu")) return QString::number(r.avgCpu, 'f', 1);
    if (col == QLatin1String("written")) return r.writeBytes < 0 ? dash : formatByteCount(r.writeBytes);
    if (col == QLatin1String("read")) return r.readBytes < 0 ? dash : formatByteCount(r.readBytes);
    if (col == QLatin1String("writeRate")) return r.writeBytes < 0 ? dash : formatRate(r.writeRate);
    if (col == QLatin1String("readRate")) return r.readBytes < 0 ? dash : formatRate(r.readRate);
    if (col == QLatin1String("connections")) return r.connections < 0 ? dash : formatCount(r.connections);
    if (col == QLatin1String("sockets")) return r.sockets < 0 ? dash : formatCount(r.sockets);
    if (col == QLatin1String("sleep")) return r.sleepBlock.isEmpty() ? QString() : QStringLiteral("Yes");
    if (col == QLatin1String("cpuWait")) return QString::number(r.cpuWait, 'f', 1);
    if (col == QLatin1String("ioWait")) return r.ioWait < 0 ? dash : QString::number(r.ioWait, 'f', 1);
    if (col == QLatin1String("state")) {
        switch (r.state) {
        case 'R': return QStringLiteral("Running");
        case 'S': return QStringLiteral("Sleeping");
        case 'D': return QStringLiteral("Waiting");
        case 'I': return QStringLiteral("Idle");
        case 'T': return QStringLiteral("Stopped");
        case 't': return QStringLiteral("Traced");
        case 'Z': return QStringLiteral("Zombie");
        }
        return dash;
    }
    if (col == QLatin1String("ports")) return r.kind != Process ? QString() : !r.portsKnown ? dash : r.ports;
    if (col == QLatin1String("command")) return r.kind != Process ? QString() : r.cmdline.isEmpty() ? QLatin1Char('[') + r.name + QLatin1Char(']') : r.cmdline;
    if (col == QLatin1String("nice")) return r.pid < 0 ? dash : QString::number(r.nice);
    if (col == QLatin1String("pid")) return r.pid < 0 ? dash : QString::number(r.pid);
    if (col == QLatin1String("user")) return r.pid < 0 ? dash : userName(r.uid);
    return QString();
}

QVariant ProcessModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_rows.size())
        return QVariant();
    const Row &r = m_rows[idx.row()];
    switch (role) {
    case KeyRole: return r.key;
    case KindRole: return int(r.kind);
    case DepthRole: return r.depth;
    case ExpandableRole: return r.expandable;
    case ExpandedRole: return r.expanded;
    case CountRole: return r.count;
    case NameRole: return r.name;
    case GlyphRole: return r.glyph;
    case SelectedRole: return m_selected.contains(r.key);
    case PausedRole: return r.paused;
    case ProtectedRole: return r.prot;
    case EndedRole: return r.ended;
    case NotRespondingRole: return m_notResponding.contains(r.key);
    case BlockedRole:
        // What a process is held up by right now, when it is held up a lot:
        // disk ("io") or a busy processor ("cpu").
        if (r.kind != Process)
            return QString();
        if (r.ioWait >= 20.0 || (r.ioWait < 0 && r.state == 'D'))
            return QStringLiteral("io");
        return r.cpuWait >= 20.0 ? QStringLiteral("cpu") : QString();
    case HotRole: {
        const qint64 total = m_snap ? m_snap->sys.memTotalKb : 0;
        if (m_sortKey == QLatin1String("cpu"))
            return r.cpu >= 10.0;
        if (m_sortKey == QLatin1String("memory"))
            return total > 0 && r.mem >= total / 10;
        return false;
    }
    case CellsRole: {
        QStringList cells;
        cells.reserve(m_visibleCols.size());
        for (const QString &c : m_visibleCols)
            cells << cell(r, c);
        return cells;
    }
    }
    return QVariant();
}

void ProcessModel::toggleExpanded(const QString &key)
{
    if (m_view == kApps || m_view == QLatin1String("services")) {
        if (!m_expandedApps.remove(key))
            m_expandedApps.insert(key);
    } else if (m_view == kTree) {
        if (!m_collapsedTree.remove(key))
            m_collapsedTree.insert(key);
    } else {
        return;
    }
    const bool held = m_hold;
    m_hold = false; // the user asked for this change; it happens now
    rebuild(false);
    m_hold = held;
}

void ProcessModel::select(const QString &key)
{
    if (m_selected.size() == 1 && m_selected.contains(key) && m_current == key)
        return;
    m_selected.clear();
    if (!key.isEmpty())
        m_selected.insert(key);
    m_current = key;
    emit selectionChanged();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {SelectedRole});
}

void ProcessModel::toggleSelected(const QString &key)
{
    if (!m_selected.remove(key)) {
        m_selected.insert(key);
        m_current = key;
    } else if (m_current == key) {
        m_current = m_selected.isEmpty() ? QString() : *m_selected.begin();
    }
    emit selectionChanged();
    const int row = indexOfKey(key);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {SelectedRole});
}

void ProcessModel::clearSelection()
{
    select(QString());
}

void ProcessModel::moveCurrent(int delta)
{
    if (m_rows.isEmpty())
        return;
    int row = indexOfKey(m_current);
    row = row < 0 ? (delta > 0 ? 0 : m_rows.size() - 1) : qBound(0, row + delta, int(m_rows.size()) - 1);
    select(m_rows[row].key);
}

int ProcessModel::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].key == key)
            return i;
    }
    return -1;
}

void ProcessModel::reveal(const QString &key)
{
    const ProcSample *p = procByKey(key);
    if (p) {
        if (m_view == QLatin1String("services")) {
            const QString unit = unitOf(p->cgroup);
            m_expandedApps.insert(unit.isEmpty() ? QStringLiteral("g:kernel") : QLatin1String("u:") + unit);
        } else if (m_view == kApps) {
            if (!p->appUnit.isEmpty())
                m_expandedApps.insert(QLatin1String("a:") + p->appUnit);
            else
                m_expandedApps.insert(p->kernelThread || p->uid != m_myUid ? kSystemKey : kBackgroundKey);
        } else if (m_view == kTree) {
            const ProcSample *at = procByPid(p->ppid);
            for (int guard = 0; at && guard < 256; ++guard) {
                m_collapsedTree.remove(processKey(*at));
                at = at->ppid != at->pid ? procByPid(at->ppid) : nullptr;
            }
        }
        if (!m_search.isEmpty() && !matchesSearch(*p)) {
            m_search.clear();
            emit searchChanged();
        }
        if (!passesView(*p)) {
            m_view = QStringLiteral("all");
            emit viewChanged();
        }
        const bool held = m_hold;
        m_hold = false;
        rebuild(false);
        m_hold = held;
    }
    select(key);
}

ProcessModel::Target ProcessModel::target(const QString &key) const
{
    Target t;
    t.key = key;
    if (!m_snap)
        return t;
    if (const ProcSample *p = procByKey(key)) {
        t.kind = Process;
        t.name = p->name;
        t.procs.append({p->pid, p->startTicks, p->uid, p->name, p->nice});
        return t;
    }
    if (key.startsWith(QLatin1String("a:"))) {
        t.kind = App;
        t.unit = key.mid(2);
        for (const ProcSample &p : m_snap->procs) {
            if (p.appUnit != t.unit)
                continue;
            const TargetProc tp{p.pid, p.startTicks, p.uid, p.name, p.nice};
            // Oldest first: that is the app's main process.
            if (!t.procs.isEmpty() && (p.startTicks < t.procs.first().startTicks
                                       || (p.startTicks == t.procs.first().startTicks && p.pid < t.procs.first().pid)))
                t.procs.prepend(tp);
            else
                t.procs.append(tp);
        }
        if (t.valid()) {
            const AppEntry &e = m_apps->forUnit(t.unit);
            t.name = e.name;
            t.appId = e.id;
        }
    }
    return t;
}

QVector<ProcessModel::TargetProc> ProcessModel::descendants(int pid) const
{
    QVector<TargetProc> out;
    if (!m_snap)
        return out;
    QHash<int, QVector<int>> children;
    for (int i = 0; i < m_snap->procs.size(); ++i)
        children[m_snap->procs[i].ppid].append(i);
    std::function<void(int, int)> walk = [&](int parent, int depth) {
        for (int i : children.value(parent)) {
            const ProcSample &p = m_snap->procs[i];
            if (p.pid == parent || depth > 64)
                continue;
            walk(p.pid, depth + 1);
            out.append({p.pid, p.startTicks, p.uid, p.name, p.nice});
        }
    };
    walk(pid, 0);
    return out;
}

QVariantMap ProcessModel::rowInfo(const QString &key) const
{
    QVariantMap m;
    const Target t = target(key);
    m[QStringLiteral("key")] = key;
    m[QStringLiteral("valid")] = t.valid();
    if (!t.valid()) {
        m[QStringLiteral("name")] = key == kSystemKey ? QStringLiteral("System")
                                    : key == kBackgroundKey ? QStringLiteral("Background") : QString();
        return m;
    }
    const TargetProc &main = t.procs.first();
    const ProcSample *p = procByPid(main.pid);
    bool mine = true;
    QString protText;
    QString protName;
    for (const TargetProc &tp : t.procs) {
        mine = mine && tp.uid == m_myUid;
        if (protText.isEmpty() && m_prot) {
            protText = m_prot->consequence(tp.pid, tp.name);
            protName = tp.name;
        }
    }
    m[QStringLiteral("name")] = t.name;
    m[QStringLiteral("isApp")] = t.kind == App;
    m[QStringLiteral("pid")] = main.pid;
    m[QStringLiteral("uid")] = main.uid;
    m[QStringLiteral("user")] = userName(main.uid);
    m[QStringLiteral("mine")] = mine || m_myUid == 0;
    m[QStringLiteral("count")] = t.procs.size();
    m[QStringLiteral("nice")] = main.nice;
    m[QStringLiteral("paused")] = p && p->state == 'T';
    m[QStringLiteral("isProtected")] = !protText.isEmpty();
    m[QStringLiteral("protectedText")] = protText;
    m[QStringLiteral("protectedName")] = protName;
    m[QStringLiteral("processKey")] = p ? processKey(*p) : QString();
    if (p && !p->appUnit.isEmpty()) {
        const AppEntry &e = m_apps->forUnit(p->appUnit);
        m[QStringLiteral("appName")] = e.name;
        m[QStringLiteral("glyph")] = e.glyph;
        m[QStringLiteral("unit")] = p->appUnit;
    } else {
        m[QStringLiteral("glyph")] = QStringLiteral("app");
    }
    return m;
}
