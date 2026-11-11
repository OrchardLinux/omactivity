#include "apps.h"
#include "procfs.h"

#include <QFile>
#include <QStandardPaths>

AppIndex::AppIndex(const QStringList &dataDirs)
    : m_dirs(dataDirs)
{
    if (m_dirs.isEmpty())
        m_dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
}

QString AppIndex::glyphForCategories(const QStringList &c)
{
    static const struct { const char *category; const char *glyph; } map[] = {
        {"WebBrowser", "globe"},      {"TerminalEmulator", "terminal"}, {"FileManager", "folder"},
        {"Calculator", "calculator"}, {"Monitor", "activity"},          {"TextEditor", "file-text"},
        {"Settings", "settings"},     {"Email", "message"},             {"InstantMessaging", "message"},
        {"Chat", "message"},          {"Player", "play"},               {"AudioVideo", "play"},
        {"Game", "gamepad"},          {"Graphics", "image"},            {"Development", "code"},
        {"Office", "file-text"},      {"Network", "globe"},
    };
    for (const auto &m : map) {
        if (c.contains(QLatin1String(m.category)))
            return QLatin1String(m.glyph);
    }
    return QStringLiteral("app");
}

bool AppIndex::parseDesktopFile(const QString &path, AppEntry &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    bool inEntry = false;
    QStringList categories;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.startsWith('[')) {
            if (inEntry)
                break; // actions and other groups follow the main one
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry)
            continue;
        const int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        const QByteArray key = line.left(eq).trimmed();
        const QString value = QString::fromUtf8(line.mid(eq + 1).trimmed());
        if (key == "Name")
            out.name = value;
        else if (key == "Icon")
            out.icon = value;
        else if (key == "StartupWMClass")
            out.wmClass = value;
        else if (key == "Categories")
            categories = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    }
    out.glyph = glyphForCategories(categories);
    out.found = true;
    return !out.name.isEmpty();
}

const AppEntry &AppIndex::forUnit(const QString &unit)
{
    // Every window of an app has its own instance id; cache by the part
    // before it.
    QString cacheKey = unit;
    const int at = cacheKey.indexOf(QLatin1Char('@'));
    if (at > 0)
        cacheKey.truncate(at);
    else if (const int dash = cacheKey.lastIndexOf(QLatin1Char('-')); dash > 4)
        cacheKey.truncate(dash);

    auto it = m_byUnitId.find(cacheKey);
    if (it != m_byUnitId.end())
        return *it;

    AppEntry entry;
    const QStringList candidates = procfs::appIdCandidates(unit);
    for (const QString &id : candidates) {
        for (const QString &dir : m_dirs) {
            AppEntry e;
            if (parseDesktopFile(dir + QLatin1Char('/') + id + QLatin1String(".desktop"), e)) {
                e.id = id;
                entry = e;
                break;
            }
        }
        if (entry.found)
            break;
    }
    if (!entry.found) {
        entry.id = candidates.value(0, unit);
        entry.name = entry.id;
        entry.glyph = QStringLiteral("app");
    }
    return *m_byUnitId.insert(cacheKey, entry);
}
