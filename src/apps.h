// Names and icons for apps, from the .desktop file that matches a systemd
// app unit. Files are looked up by id on demand; nothing is scanned.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

struct AppEntry {
    QString id;    // desktop file id, also what compositors report as app_id
    QString name;
    QString icon;  // icon theme name or path, as written in the file
    QString glyph; // built-in line icon picked from the categories
    QString wmClass;
    bool found = false;
};

class AppIndex
{
public:
    explicit AppIndex(const QStringList &dataDirs = QStringList());
    // unit: "app-firefox@3f1a.scope". Always returns an entry; without a
    // desktop file the name is the id from the unit.
    const AppEntry &forUnit(const QString &unit);

    static bool parseDesktopFile(const QString &path, AppEntry &out);
    static QString glyphForCategories(const QStringList &categories);

private:
    QStringList m_dirs;
    QHash<QString, AppEntry> m_byUnitId; // keyed by the unit name up to the instance id
};
