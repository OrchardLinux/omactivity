// The one process list all tabs share. Turns a Snapshot into rows for the
// current view (apps, flat, tree, the filters), search and sort, and keeps
// expansion and selection across refreshes and tab switches.
#pragma once

#include "apps.h"
#include "protectedlist.h"
#include "sampler.h"

#include <QAbstractListModel>
#include <QSet>
#include <QVariant>

class ProcessModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY tabChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString viewTitle READ viewTitle NOTIFY viewChanged)
    Q_PROPERTY(QVariantList views READ views CONSTANT)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString sortKey READ sortKey NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY sortChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(QVariantList allColumns READ allColumns NOTIFY columnsChanged)
    Q_PROPERTY(bool narrow READ narrow WRITE setNarrow NOTIFY columnsChanged)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY holdChanged)
    Q_PROPERTY(int processCount READ processCount NOTIFY countsChanged)
    // Processes that match the search, while there is one.
    Q_PROPERTY(int matchCount READ matchCount NOTIFY updated)
    // True while a ports column or the ports view is showing.
    Q_PROPERTY(bool wantsPorts READ wantsPorts NOTIFY wantsPortsChanged)
    // "root: 22, 5353/udp": listeners that cannot be matched to a process.
    Q_PROPERTY(QString otherPorts READ otherPorts NOTIFY updated)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedKeys READ selectedKeys NOTIFY selectionChanged)

public:
    enum Kind { Process, App, Group };
    Q_ENUM(Kind)
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        KindRole, DepthRole, ExpandableRole, ExpandedRole, CountRole, NameRole, GlyphRole,
        SelectedRole, PausedRole, ProtectedRole, EndedRole, NotRespondingRole, CellsRole, HotRole, BlockedRole,
    };

    struct Row {
        QString key;
        Kind kind = Process;
        int depth = 0;
        bool expandable = false;
        bool expanded = false;
        int count = 1;
        int pid = -1; // main process for an app; -1 for System and Background
        quint64 startTicks = 0;
        uint uid = 0;
        QString name;
        QString glyph;
        QString unit;  // app rows: the systemd unit
        QString appId; // app rows: desktop id
        double cpu = 0;
        double cpuSort = 0; // % CPU averaged over the last samples: what the list is ordered by
        quint64 cpuTicks = 0;
        int threads = 0;
        double wakeups = 0;
        double cpuWait = 0;
        double ioWait = -1;
        char state = 0; // processes only
        double avgCpu = 0; // % CPU averaged over the process's whole life
        QString sleepBlock; // why it keeps the device awake, or empty
        qint64 readBytes = -1, writeBytes = -1;
        double readRate = 0, writeRate = 0;
        int sockets = -1, connections = -1;
        QString ports;
        bool portsKnown = false;
        QString cmdline;
        int nice = 0;
        qint64 mem = 0, rss = 0, priv = -1, shared = -1, swap = -1;
        bool paused = false;
        bool prot = false;
        bool ended = false;
    };

    // What an action needs to know about a row.
    struct TargetProc {
        int pid;
        quint64 startTicks;
        uint uid;
        QString name;
        int nice;
    };
    struct Target {
        QString key;
        QString name;
        Kind kind = Process;
        QString unit;
        QString appId;
        QVector<TargetProc> procs; // main process first
        bool valid() const { return !procs.isEmpty(); }
    };

    // uid: whose processes count as "mine"; the tests pass the fixture's user.
    ProcessModel(AppIndex *apps, const ProtectedList *prot, QObject *parent = nullptr, int uid = -1);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString tab() const { return m_tab; }
    void setTab(const QString &tab);
    QString view() const { return m_view; }
    void setView(const QString &view);
    QString viewTitle() const;
    QVariantList views() const;
    QString search() const { return m_search; }
    void setSearch(const QString &s);
    QString sortKey() const { return m_sortKey; }
    bool sortDescending() const { return m_sortDesc; }
    QVariantList columns() const;
    QVariantList allColumns() const;
    bool narrow() const { return m_narrow; }
    void setNarrow(bool n);
    bool hold() const { return m_hold; }
    void setHold(bool h);
    int processCount() const { return m_snap ? m_snap->procs.size() : 0; }
    bool wantsPorts() const;
    int matchCount() const;
    static QString defaultSort(const QString &tab);
    static bool validTab(const QString &tab);
    static QString formatRate(double bytesPerSecond);
    static QString formatByteCount(qint64 bytes);
    QString otherPorts() const;
    int selectionCount() const { return m_selected.size(); }
    QString currentKey() const { return m_current; }
    QStringList selectedKeys() const { return m_selected.values(); }

    Q_INVOKABLE void sortBy(const QString &key);
    Q_INVOKABLE void setColumnVisible(const QString &key, bool visible);
    Q_INVOKABLE void toggleExpanded(const QString &key);
    Q_INVOKABLE void select(const QString &key); // single selection
    Q_INVOKABLE void toggleSelected(const QString &key);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void moveCurrent(int delta); // arrow keys
    Q_INVOKABLE int indexOfKey(const QString &key) const;
    Q_INVOKABLE QString keyForPid(int pid) const;
    // The first row, else the first process, with this name.
    Q_INVOKABLE QString findByName(const QString &name) const;
    Q_INVOKABLE QVariantMap rowInfo(const QString &key) const;
    // Makes sure the row is visible (expanding its parents) and selects it.
    Q_INVOKABLE void reveal(const QString &key);

    SnapshotPtr snapshot() const { return m_snap; }
    Target target(const QString &key) const;
    QVector<TargetProc> descendants(int pid) const; // children first
    const ProcSample *procByPid(int pid) const;
    const ProcSample *procByKey(const QString &key) const;
    QString userName(uint uid) const;
    uint myUid() const { return m_myUid; }
    QString sleepBlocker(int pid) const { return m_sleepBlockers.value(pid); }
    const ProtectedList *protectedList() const { return m_prot; }
    const QVector<float> historyFor(quint64 id) const { return m_history.value(id); }
    int historyLength() const;

    static QString processKey(const ProcSample &p);
    // "NetworkManager.service" from a cgroup path: the unit a process runs in.
    static QString unitOf(const QString &cgroup);
    static QString formatBytes(qint64 kb);
    static QString formatCpuTime(quint64 ticks, long ticksPerSecond);
    static QString formatCount(qint64 n);

public slots:
    void setSnapshot(SnapshotPtr snap);
    void setWindowAppIds(const QStringList &ids);
    void setNotResponding(const QStringList &keys);
    // PID -> reason, for processes that hold a sleep or idle inhibitor.
    void setSleepBlockers(const QHash<int, QString> &blockers);

signals:
    void tabChanged();
    void viewChanged();
    void searchChanged();
    void sortChanged();
    void columnsChanged();
    void holdChanged();
    void countsChanged();
    void wantsPortsChanged();
    void selectionChanged();
    void updated();

private:
    struct Column {
        const char *key;
        const char *title;
        int width;
        const char *tabs;     // tabs it can appear on
        const char *defaults; // tabs it is on by default
    };
    static const QVector<Column> &columnDefs();
    QStringList visibleColumnKeys() const;
    QStringList configuredColumns(const QString &tab) const;

    void rebuild(bool fromSnapshot);
    QVector<Row> buildRows() const;
    void applyRows(const QVector<Row> &next);
    Row rowForProc(const ProcSample &p, int depth) const;
    double steadyCpu(const ProcSample &p) const;
    bool matchesSearch(const ProcSample &p) const;
    bool passesView(const ProcSample &p) const;
    bool hasWindow(const ProcSample &p) const;
    bool lessThan(const Row &a, const Row &b) const;
    QString cell(const Row &r, const QString &col) const;
    void loadTabState();
    void checkWantsPorts();
    bool m_wantedPorts = false;
    void saveState() const;

    AppIndex *m_apps;
    const ProtectedList *m_prot;
    SnapshotPtr m_snap;
    QVector<Row> m_rows;
    QHash<int, int> m_pidIndex; // pid -> index in m_snap->procs

    QString m_tab = QStringLiteral("cpu");
    QString m_view = QStringLiteral("apps");
    QString m_search;
    QString m_sortKey = QStringLiteral("cpu");
    bool m_sortDesc = true;
    bool m_narrow = false;
    bool m_hold = false;
    bool m_pending = false;
    uint m_myUid = 0;

    QSet<QString> m_expandedApps;   // apps view: collapsed by default
    QSet<QString> m_collapsedTree;  // tree view: expanded by default
    QSet<QString> m_selected;
    QString m_current;
    QSet<QString> m_windowIds;
    QSet<QString> m_notResponding;
    QHash<int, QString> m_sleepBlockers;
    QStringList m_visibleCols; // cache for the current tab
    mutable QHash<uint, QString> m_users;
    QHash<quint64, QVector<float>> m_history; // % CPU per process, oldest first
};
