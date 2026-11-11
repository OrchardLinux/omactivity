#include "actions.h"

#include <QProcess>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <grp.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
const int kGraceMs = 5000;

struct SignalDef {
    int number;
    const char *name;
    const char *meaning;
};
const SignalDef kSignals[] = {
    {SIGHUP, "SIGHUP", "Hang up: many services reload their configuration"},
    {SIGINT, "SIGINT", "Interrupt, like Ctrl+C in a terminal"},
    {SIGQUIT, "SIGQUIT", "Quit and write a core dump"},
    {SIGTERM, "SIGTERM", "Ask the process to end; it can clean up first"},
    {SIGKILL, "SIGKILL", "End it now; it cannot save or clean up"},
    {SIGSTOP, "SIGSTOP", "Pause the process until SIGCONT"},
    {SIGCONT, "SIGCONT", "Resume a paused process"},
    {SIGUSR1, "SIGUSR1", "User signal 1: meaning depends on the program"},
    {SIGUSR2, "SIGUSR2", "User signal 2: meaning depends on the program"},
};

bool inAdminGroup()
{
    if (geteuid() == 0)
        return true;
    int n = getgroups(0, nullptr);
    if (n <= 0)
        return false;
    QVector<gid_t> groups(n);
    n = getgroups(n, groups.data());
    for (int i = 0; i < n; ++i) {
        const group *g = getgrgid(groups[i]);
        if (g && (!qstrcmp(g->gr_name, "wheel") || !qstrcmp(g->gr_name, "sudo")))
            return true;
    }
    return false;
}
} // namespace

Actions::Actions(ProcessModel *model, Toplevels *toplevels, const QString &procRoot, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_toplevels(toplevels)
    , m_proc(procRoot)
    , m_uid(getuid())
{
    // The PIDs of a recorded tree mean something else on this machine.
    m_live = procRoot == QLatin1String("/proc");
    m_canSudo = m_uid == 0 || (inAdminGroup() && !QStandardPaths::findExecutable(QStringLiteral("sudo")).isEmpty());
    m_clock.start();
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &Actions::tick);
    connect(model, &ProcessModel::updated, this, &Actions::pruneNotResponding);
}

QVariantList Actions::signalList() const
{
    QVariantList out;
    for (const SignalDef &s : kSignals)
        out << QVariantMap{{"number", s.number}, {"name", QString::fromLatin1(s.name)}, {"meaning", QString::fromLatin1(s.meaning)}};
    return out;
}

QString Actions::signalName(int signal)
{
    for (const SignalDef &s : kSignals) {
        if (s.number == signal)
            return QString::fromLatin1(s.name);
    }
    return QStringLiteral("signal %1").arg(signal);
}

bool Actions::sameProcess(int pid, quint64 startTicks) const
{
    QByteArray buf;
    procfs::Stat st;
    return procfs::readFile(QStringLiteral("%1/%2/stat").arg(m_proc).arg(pid), buf) && procfs::parseStat(buf, st)
        && st.starttime == startTicks && st.state != 'Z' && st.state != 'X';
}

bool Actions::alive(const ProcessModel::Target &t) const
{
    for (const ProcessModel::TargetProc &p : t.procs) {
        if (sameProcess(p.pid, p.startTicks))
            return true;
    }
    return false;
}

bool Actions::needsSudo(const QString &key) const
{
    if (m_uid == 0)
        return false;
    const ProcessModel::Target t = m_model->target(key);
    for (const ProcessModel::TargetProc &p : t.procs) {
        if (p.uid != m_uid)
            return true;
    }
    return false;
}

QString Actions::disabledReason(const QString &key) const
{
    const ProcessModel::Target t = m_model->target(key);
    if (!t.valid())
        return QStringLiteral("This row is a group. Open it and pick a process.");
    if (m_canSudo)
        return QString();
    for (const ProcessModel::TargetProc &p : t.procs) {
        if (p.uid != m_uid)
            return QStringLiteral("Owned by %1. Your account has no sudo rights, so it can't be changed from here.")
                .arg(m_model->userName(p.uid));
    }
    return QString();
}

void Actions::setPrompt(const QVariantMap &prompt)
{
    m_prompt = prompt;
    emit promptChanged();
}

static QString describe(const QVector<ProcessModel::Target> &targets)
{
    if (targets.size() == 1)
        return targets.first().name;
    return QStringLiteral("%1 processes").arg(targets.size());
}

void Actions::quit(const QStringList &keys)
{
    Request r;
    r.type = Quit;
    r.signal = SIGTERM;
    r.verb = QStringLiteral("Quit");
    for (const QString &k : keys)
        r.targets << m_model->target(k);
    begin(r);
}

void Actions::forceQuit(const QStringList &keys)
{
    Request r;
    r.type = ForceQuit;
    r.signal = SIGKILL;
    r.verb = QStringLiteral("Force Quit");
    for (const QString &k : keys)
        r.targets << m_model->target(k);
    begin(r);
}

void Actions::sendSignal(const QStringList &keys, int signal)
{
    if (signal <= 0 || signal >= NSIG)
        return;
    Request r;
    r.type = Signal;
    r.signal = signal;
    r.verb = signal == SIGSTOP ? QStringLiteral("Pause")
             : signal == SIGCONT ? QStringLiteral("Resume")
             : signal == SIGKILL ? QStringLiteral("Force Quit")
                                 : QStringLiteral("Send %1 to").arg(signalName(signal));
    for (const QString &k : keys)
        r.targets << m_model->target(k);
    begin(r);
}

void Actions::renice(const QString &key, int nice)
{
    Request r;
    r.type = Renice;
    r.nice = qBound(-20, nice, 19);
    r.verb = QStringLiteral("Change the priority of");
    r.targets << m_model->target(key);
    begin(r);
}

void Actions::quitTree(const QString &key)
{
    Request r;
    r.type = QuitTree;
    r.signal = SIGTERM;
    r.verb = QStringLiteral("Quit the process tree of");
    ProcessModel::Target t = m_model->target(key);
    if (t.valid()) {
        // Children first, so a parent cannot restart them on the way down.
        QVector<ProcessModel::TargetProc> all;
        QSet<int> seen;
        for (const ProcessModel::TargetProc &root : std::as_const(t.procs)) {
            for (const ProcessModel::TargetProc &d : m_model->descendants(root.pid)) {
                if (!seen.contains(d.pid)) {
                    seen.insert(d.pid);
                    all << d;
                }
            }
        }
        for (const ProcessModel::TargetProc &root : std::as_const(t.procs)) {
            if (!seen.contains(root.pid)) {
                seen.insert(root.pid);
                all << root;
            }
        }
        t.procs = all;
    }
    r.targets << t;
    begin(r);
}

void Actions::begin(Request req)
{
    if (m_req.active)
        return; // one question at a time
    for (int i = req.targets.size() - 1; i >= 0; --i) {
        if (!req.targets[i].valid())
            req.targets.remove(i);
    }
    if (req.targets.isEmpty()) {
        emit message(QStringLiteral("That process has already ended."), false);
        return;
    }
    req.subject = describe(req.targets);

    bool sudo = false;
    QString owner;
    QString protText;
    for (const ProcessModel::Target &t : std::as_const(req.targets)) {
        for (const ProcessModel::TargetProc &p : t.procs) {
            if (m_uid != 0 && p.uid != m_uid) {
                sudo = true;
                if (owner.isEmpty())
                    owner = m_model->userName(p.uid);
            }
            if (req.type == Renice && m_uid != 0 && req.nice < p.nice)
                sudo = true; // only root may raise a priority
            if (protText.isEmpty() && m_model->protectedList())
                protText = m_model->protectedList()->consequence(p.pid, p.name);
        }
    }
    if (!m_live)
        sudo = false; // a recorded tree: nothing is sent, so no rights are needed
    if (sudo && !m_canSudo) {
        emit message(owner.isEmpty()
                         ? QStringLiteral("Raising a priority needs sudo rights, which your account does not have.")
                         : QStringLiteral("%1 is owned by %2. Your account has no sudo rights, so nothing was sent.").arg(req.subject, owner),
                     true);
        return;
    }

    const bool kills = req.type == ForceQuit || (req.type == Signal && req.signal == SIGKILL);
    m_req = req;
    m_req.active = true;
    m_req.label = req.type == Renice ? QStringLiteral("Change")
                  : req.type == QuitTree ? QStringLiteral("Quit Tree")
                  : req.type == Signal && !kills && req.signal != SIGSTOP && req.signal != SIGCONT ? QStringLiteral("Send")
                                                                                                  : req.verb;
    if (protText.isEmpty() && !kills && !sudo) {
        execute();
        return;
    }

    QVariantMap prompt{{"type", QStringLiteral("confirm")},
                       {"title", QStringLiteral("%1 %2?").arg(req.verb, req.subject)},
                       {"confirmLabel", m_req.label}};
    QStringList text;
    if (!protText.isEmpty()) {
        text << protText;
        prompt[QStringLiteral("note")] = QStringLiteral("Protected: %1").arg(m_model->protectedList()->path());
    } else if (kills) {
        text << QStringLiteral("%1 closes at once, without a chance to save. Unsaved work is lost.").arg(req.subject);
    }
    if (sudo) {
        text << (owner.isEmpty() ? QStringLiteral("Raising a priority is done as root, through sudo.")
                                 : QStringLiteral("It is owned by %1, so this is done through sudo.").arg(owner));
        if (protText.isEmpty() && !kills && req.type != Renice)
            text << QStringLiteral("Unsaved work in it may be lost.");
    }
    prompt[QStringLiteral("message")] = text.join(QLatin1Char(' '));
    setPrompt(prompt);
}

void Actions::confirm()
{
    if (!m_req.active || m_prompt.value(QStringLiteral("type")) != QLatin1String("confirm"))
        return;
    setPrompt({});
    execute();
}

void Actions::cancel()
{
    // A Quit that never got sent must not turn into "Not responding".
    if (m_req.active && !m_sudoCommand.isEmpty()) {
        for (const ProcessModel::Target &t : std::as_const(m_req.targets)) {
            for (int i = 0; i < m_jobs.size(); ++i) {
                if (m_jobs[i].key == t.key)
                    m_jobs.remove(i--);
            }
        }
    }
    m_req = Request();
    m_sudoCommand.clear();
    setPrompt({});
}

void Actions::announce()
{
    QStringList keys;
    for (const ProcessModel::Target &t : std::as_const(m_req.targets))
        keys << t.key;
    emit sent(keys, m_req.type == ForceQuit || (m_req.type == Signal && m_req.signal == SIGKILL));
}

void Actions::finish()
{
    m_req = Request();
    m_sudoCommand.clear();
    if (!m_prompt.isEmpty())
        setPrompt({});
}

QVector<ProcessModel::TargetProc> Actions::signalProcs(const QVector<ProcessModel::TargetProc> &procs, int signal)
{
    QVector<ProcessModel::TargetProc> forSudo;
    for (const ProcessModel::TargetProc &p : procs) {
        // The PID may have been reused since the list was drawn.
        if (!sameProcess(p.pid, p.startTicks))
            continue;
        if (m_uid == 0 || p.uid == m_uid) {
            if (::kill(p.pid, signal) != 0 && errno == EPERM)
                forSudo << p;
        } else {
            forSudo << p;
        }
    }
    return forSudo;
}

void Actions::execute()
{
    if (!m_live) {
        emit message(QStringLiteral("This is a recorded process list, not this device. Nothing was sent."), false);
        finish();
        return;
    }
    QVector<ProcessModel::TargetProc> forSudo;

    if (m_req.type == Renice) {
        for (const ProcessModel::Target &t : std::as_const(m_req.targets)) {
            for (const ProcessModel::TargetProc &p : t.procs) {
                if (!sameProcess(p.pid, p.startTicks))
                    continue;
                if (m_uid != 0 && (p.uid != m_uid || m_req.nice < p.nice)) {
                    forSudo << p;
                } else if (setpriority(PRIO_PROCESS, id_t(p.pid), m_req.nice) != 0) {
                    forSudo << p;
                }
            }
        }
        if (!forSudo.isEmpty()) {
            QStringList cmd{QStringLiteral("renice"), QStringLiteral("-n"), QString::number(m_req.nice), QStringLiteral("-p")};
            for (const ProcessModel::TargetProc &p : std::as_const(forSudo))
                cmd << QString::number(p.pid);
            runSudo(cmd, QByteArray());
            return;
        }
        announce();
        finish();
        return;
    }

    for (const ProcessModel::Target &t : std::as_const(m_req.targets)) {
        bool mine = true;
        for (const ProcessModel::TargetProc &p : t.procs)
            mine = mine && (m_uid == 0 || p.uid == m_uid);

        if (m_req.type == Quit && t.kind == ProcessModel::App && mine && m_toplevels
            && m_toplevels->closeWindows(t.appId) > 0) {
            // The app gets to ask "save changes?". SIGTERM follows if it is
            // still there after the grace period.
            track(t, 0);
            continue;
        }
        if (m_req.type == ForceQuit && t.kind == ProcessModel::App && mine && !t.unit.isEmpty()) {
            // The scope catches children that were forked after the list
            // was read; the direct signals below cover a failing systemctl.
            QProcess::startDetached(QStringLiteral("systemctl"),
                                    {QStringLiteral("--user"), QStringLiteral("kill"), QStringLiteral("--signal=KILL"), t.unit});
        }
        forSudo += signalProcs(t.procs, m_req.signal);
        if (m_req.type == Quit)
            track(t, 1);
    }

    if (!forSudo.isEmpty()) {
        QStringList cmd{QStringLiteral("kill"), QStringLiteral("-s"), signalName(m_req.signal).mid(3)};
        for (const ProcessModel::TargetProc &p : std::as_const(forSudo))
            cmd << QString::number(p.pid);
        runSudo(cmd, QByteArray());
        return;
    }
    announce();
    finish();
}

void Actions::runSudo(const QStringList &command, const QByteArray &password)
{
    m_sudoCommand = command;
    auto *proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C")); // the replies below are matched as text
    proc->setProcessEnvironment(env);
    const bool withPassword = !password.isEmpty();
    QStringList args;
    if (withPassword)
        args << QStringLiteral("-S") << QStringLiteral("-p") << QString(); // read it from stdin, print no prompt
    else
        args << QStringLiteral("-n"); // never ask: tells us whether a password is needed
    args << QStringLiteral("--") << command;
    connect(proc, &QProcess::finished, this, [this, proc, withPassword] { sudoFinished(proc, withPassword); });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        emit message(QStringLiteral("sudo could not be started. Nothing was sent."), true);
        finish();
    });
    proc->start(QStringLiteral("sudo"), args);
    if (withPassword) {
        // stdin only: never an argument, never a file.
        proc->write(password);
        proc->write("\n");
    }
    proc->closeWriteChannel();
}

void Actions::sudoFinished(QProcess *proc, bool withPassword)
{
    const QString err = QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
    const bool ok = proc->exitStatus() == QProcess::NormalExit && proc->exitCode() == 0;
    proc->deleteLater();
    if (!m_req.active)
        return; // cancelled meanwhile

    if (ok) {
        announce();
        finish();
        return;
    }
    const bool wantsPassword = err.contains(QLatin1String("password is required"))
                               || err.contains(QLatin1String("a terminal is required"));
    const bool wrongPassword = err.contains(QLatin1String("incorrect password")) || err.contains(QLatin1String("try again"))
                               || err.contains(QLatin1String("no password was provided"));
    if ((!withPassword && wantsPassword) || (withPassword && wrongPassword)) {
        setPrompt({{"type", QStringLiteral("password")},
                   {"title", QStringLiteral("Password needed")},
                   {"message", QStringLiteral("To %1 %2, sudo needs your password.")
                                   .arg(m_req.verb == QLatin1String("Force Quit") ? QStringLiteral("force quit")
                                        : m_req.verb.left(1).toLower() + m_req.verb.mid(1),
                                        m_req.subject)},
                   {"confirmLabel", m_req.label},
                   {"error", withPassword ? QStringLiteral("Wrong password. Nothing was sent.") : QString()}});
        return;
    }
    QString reason = err.section(QLatin1Char('\n'), -1);
    if (reason.contains(QLatin1String("not in the sudoers")) || reason.contains(QLatin1String("not allowed")))
        reason = QStringLiteral("Your account has no sudo rights.");
    emit message(QStringLiteral("%1 Nothing was sent.").arg(reason.isEmpty() ? QStringLiteral("sudo failed.") : reason), true);
    finish();
}

void Actions::submitPassword(const QString &password)
{
    if (!m_req.active || m_prompt.value(QStringLiteral("type")) != QLatin1String("password") || m_sudoCommand.isEmpty())
        return;
    if (password.isEmpty())
        return;
    // Processes may have ended while the dialog was open: check again and
    // drop PIDs that now belong to something else.
    QStringList cmd;
    bool pids = false;
    QSet<QString> valid;
    for (const ProcessModel::Target &t : std::as_const(m_req.targets)) {
        for (const ProcessModel::TargetProc &p : t.procs) {
            if (sameProcess(p.pid, p.startTicks))
                valid.insert(QString::number(p.pid));
        }
    }
    const int firstPid = m_req.type == Renice ? 4 : 3;
    for (int i = 0; i < m_sudoCommand.size(); ++i) {
        if (i < firstPid) {
            cmd << m_sudoCommand[i];
        } else if (valid.contains(m_sudoCommand[i])) {
            cmd << m_sudoCommand[i];
            pids = true;
        }
    }
    QVariantMap waiting = m_prompt;
    waiting[QStringLiteral("busy")] = true;
    waiting[QStringLiteral("error")] = QString();
    setPrompt(waiting);
    if (!pids) {
        emit message(QStringLiteral("That process has already ended."), false);
        finish();
        return;
    }
    QByteArray pw = password.toUtf8();
    runSudo(cmd, pw);
    pw.fill('\0');
}

void Actions::track(const ProcessModel::Target &t, int stage)
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].key == t.key)
            m_jobs.remove(i--);
    }
    m_jobs.append({t.key, t, stage, m_clock.elapsed() + kGraceMs});
    if (!m_timer.isActive())
        m_timer.start();
}

void Actions::tick()
{
    const qint64 now = m_clock.elapsed();
    bool changed = false;
    for (int i = 0; i < m_jobs.size(); ++i) {
        Job &job = m_jobs[i];
        if (!alive(job.target)) {
            m_jobs.remove(i--);
            continue;
        }
        if (now < job.deadline)
            continue;
        if (job.stage == 0) {
            // The windows were asked to close and the app is still here.
            signalProcs(job.target.procs, SIGTERM);
            job.stage = 1;
            job.deadline = now + kGraceMs;
        } else {
            if (!m_notResponding.contains(job.key)) {
                m_notResponding << job.key;
                changed = true;
            }
            m_jobs.remove(i--);
        }
    }
    if (m_jobs.isEmpty())
        m_timer.stop();
    if (changed)
        m_model->setNotResponding(m_notResponding);
}

void Actions::pruneNotResponding()
{
    if (m_notResponding.isEmpty())
        return;
    bool changed = false;
    for (int i = m_notResponding.size() - 1; i >= 0; --i) {
        if (!m_model->target(m_notResponding[i]).valid()) {
            m_notResponding.removeAt(i);
            changed = true;
        }
    }
    if (changed)
        m_model->setNotResponding(m_notResponding);
}
