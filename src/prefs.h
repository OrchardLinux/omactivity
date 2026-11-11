// The few interface settings that are not part of the list state.
#pragma once

#include <QObject>
#include <QSettings>

class Prefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool comfortable READ comfortable WRITE setComfortable NOTIFY changed)
    Q_PROPERTY(bool perCore READ perCore WRITE setPerCore NOTIFY changed)
    // Until a process has been opened once, the list says how.
    Q_PROPERTY(bool hintSeen READ hintSeen WRITE setHintSeen NOTIFY changed)

public:
    explicit Prefs(QObject *parent = nullptr)
        : QObject(parent)
    {
        QSettings s;
        m_comfortable = s.value(QStringLiteral("rows"), QStringLiteral("compact")).toString() == QLatin1String("comfortable");
        m_perCore = s.value(QStringLiteral("perCore"), false).toBool();
        m_hintSeen = s.value(QStringLiteral("hintSeen"), false).toBool();
    }
    bool comfortable() const { return m_comfortable; }
    void setComfortable(bool c)
    {
        if (c == m_comfortable)
            return;
        m_comfortable = c;
        QSettings().setValue(QStringLiteral("rows"), c ? QStringLiteral("comfortable") : QStringLiteral("compact"));
        emit changed();
    }
    bool hintSeen() const { return m_hintSeen; }
    void setHintSeen(bool seen)
    {
        if (seen == m_hintSeen)
            return;
        m_hintSeen = seen;
        QSettings().setValue(QStringLiteral("hintSeen"), seen);
        emit changed();
    }
    bool perCore() const { return m_perCore; }
    void setPerCore(bool p)
    {
        if (p == m_perCore)
            return;
        m_perCore = p;
        QSettings().setValue(QStringLiteral("perCore"), p);
        emit changed();
    }

signals:
    void changed();

private:
    bool m_comfortable = false;
    bool m_perCore = false;
    bool m_hintSeen = false;
};
