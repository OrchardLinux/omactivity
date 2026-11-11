// Quit, Force Quit, signals, priority. Own processes are signalled
// directly; root's and other users' go through sudo, which is asked for a
// password only when it needs one. Every signal is preceded by a check that
// the PID still belongs to the process the user picked.
#pragma once

#include "processmodel.h"
#include "toplevels.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariant>

class QProcess;

class Actions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canSudo READ canSudo CONSTANT)
    // What the interface must ask the user right now: type "" (nothing),
    // "confirm" or "password", with title, message, note, confirmLabel, error.
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(QVariantList signalList READ signalList CONSTANT)

public:
    Actions(ProcessModel *model, Toplevels *toplevels, const QString &procRoot, QObject *parent = nullptr);

    bool canSudo() const { return m_canSudo; }
    QVariantMap prompt() const { return m_prompt; }
    QVariantList signalList() const;

    Q_INVOKABLE void quit(const QStringList &keys);
    Q_INVOKABLE void forceQuit(const QStringList &keys);
    Q_INVOKABLE void sendSignal(const QStringList &keys, int signal);
    Q_INVOKABLE void renice(const QString &key, int nice);
    Q_INVOKABLE void quitTree(const QString &key);
    // Why actions on this row are not possible, or an empty string.
    Q_INVOKABLE QString disabledReason(const QString &key) const;
    Q_INVOKABLE bool needsSudo(const QString &key) const;

    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void submitPassword(const QString &password);

    // True when the PID still is the process that started at startTicks.
    bool sameProcess(int pid, quint64 startTicks) const;

signals:
    void promptChanged();
    void message(const QString &text, bool error);
    // The action went out (after any confirmation and password). ends: it
    // was a Force Quit, so the rows are about to disappear.
    void sent(const QStringList &keys, bool ends);

private:
    enum Type { Quit, ForceQuit, Signal, Renice, QuitTree };
    struct Request {
        Type type = Quit;
        int signal = 0;
        int nice = 0;
        QVector<ProcessModel::Target> targets;
        QString verb;    // "Force Quit"
        QString subject; // "Firefox" or "3 processes"
        QString label;   // the confirming button
        bool active = false;
    };
    struct Job { // a Quit that is waiting for the app to go away
        QString key;
        ProcessModel::Target target;
        int stage = 0; // 0: asked the windows to close, 1: SIGTERM sent
        qint64 deadline = 0;
    };

    void begin(Request req);
    void execute();
    void finish();
    void announce();
    // Signals own processes now and returns the rest for sudo.
    QVector<ProcessModel::TargetProc> signalProcs(const QVector<ProcessModel::TargetProc> &procs, int signal);
    void runSudo(const QStringList &command, const QByteArray &password);
    void sudoFinished(QProcess *proc, bool withPassword);
    void setPrompt(const QVariantMap &prompt);
    void tick();
    void track(const ProcessModel::Target &t, int stage);
    void pruneNotResponding();
    bool alive(const ProcessModel::Target &t) const;
    static QString signalName(int signal);

    ProcessModel *m_model;
    Toplevels *m_toplevels;
    QString m_proc;
    bool m_canSudo = false;
    bool m_live = true; // false when showing a recorded tree: nothing is ever sent
    uint m_uid = 0;
    QVariantMap m_prompt;
    Request m_req;
    QStringList m_sudoCommand;
    QVector<Job> m_jobs;
    QStringList m_notResponding;
    QTimer m_timer;
    QElapsedTimer m_clock;
};
