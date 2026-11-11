// Everything about one process, for the inspect panel. Follows the list's
// refresh; the open-files and environment lists are read only when asked.
#pragma once

#include "processmodel.h"

#include <QObject>
#include <QVariant>

class Inspector : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString key READ key NOTIFY keyChanged)           // the row the buttons act on
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY infoChanged)
    Q_PROPERTY(int historyLength READ historyLength NOTIFY infoChanged)
    Q_PROPERTY(QVariantList children READ children NOTIFY infoChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesNote READ filesNote NOTIFY filesChanged)
    Q_PROPERTY(bool filesLoaded READ filesLoaded NOTIFY filesChanged)
    Q_PROPERTY(QStringList environment READ environment NOTIFY environmentChanged)
    Q_PROPERTY(QString environmentNote READ environmentNote NOTIFY environmentChanged)
    Q_PROPERTY(bool environmentLoaded READ environmentLoaded NOTIFY environmentChanged)

public:
    Inspector(ProcessModel *model, const QString &procRoot, QObject *parent = nullptr);

    QString key() const { return m_key; }
    QVariantMap info() const { return m_info; }
    QVariantList cpuHistory() const;
    int historyLength() const { return m_model->historyLength(); }
    QVariantList children() const { return m_children; }
    QVariantList files() const { return m_files; }
    QString filesNote() const { return m_filesNote; }
    bool filesLoaded() const { return m_filesLoaded; }
    QStringList environment() const { return m_env; }
    QString environmentNote() const { return m_envNote; }
    bool environmentLoaded() const { return m_envLoaded; }

    Q_INVOKABLE void open(const QString &rowKey);
    Q_INVOKABLE void close();
    Q_INVOKABLE void loadFiles();
    Q_INVOKABLE void loadEnvironment();

    static QString stateName(char state);
    static QString formatElapsed(qint64 seconds);

signals:
    void keyChanged();
    void infoChanged();
    void filesChanged();
    void environmentChanged();

private:
    void refresh();
    QString notAvailable(uint uid) const;

    ProcessModel *m_model;
    QString m_proc;
    QString m_key;
    QString m_processKey;
    quint64 m_id = 0;
    QVariantMap m_info;
    QVariantList m_children;
    QVariantList m_files;
    QString m_filesNote;
    bool m_filesLoaded = false;
    QStringList m_env;
    QString m_envNote;
    bool m_envLoaded = false;
};
