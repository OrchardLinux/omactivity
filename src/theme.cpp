#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cmath>

namespace {
QColor withAlpha(QColor c, qreal a)
{
    c.setAlphaF(float(qBound(0.0, a, 1.0)));
    return c;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
{
    const QString home = QDir::homePath();
    // Omarchy A keeps the current theme under ~/.local/state; upstream
    // Omarchy under ~/.config.
    m_themeDir = home + QLatin1String("/.local/state/omarchy/current/theme");
    if (!QFileInfo::exists(m_themeDir) && QFileInfo::exists(home + QLatin1String("/.config/omarchy/current/theme")))
        m_themeDir = home + QLatin1String("/.config/omarchy/current/theme");
    m_userShell = home + QLatin1String("/.config/omarchy/shell.toml");

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150); // a theme switch replaces several files
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    reload();
}

void Theme::setPaths(const QString &themeDir, const QString &userShell)
{
    m_themeDir = themeDir;
    m_userShell = userShell;
    reload();
}

void Theme::watch()
{
    // A theme switch may replace the directory or the symlink to it, which
    // drops the watches, so they are set again after every reload.
    QStringList paths{m_themeDir, QFileInfo(m_themeDir).path(), m_themeDir + QLatin1String("/colors.toml"),
                      m_themeDir + QLatin1String("/shell.toml"), m_userShell, QFileInfo(m_userShell).path()};
    for (const QString &p : std::as_const(paths)) {
        if (QFileInfo::exists(p) && !m_watcher.files().contains(p) && !m_watcher.directories().contains(p))
            m_watcher.addPath(p);
    }
}

QHash<QString, QString> Theme::parseToml(const QByteArray &data)
{
    // The subset the shell's own reader accepts: [section] headers and
    // key = "string" | number | bare-word lines, with trailing comments.
    QHash<QString, QString> out;
    QString section;
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('[')) {
            const int end = line.indexOf(']');
            if (end > 0)
                section = QString::fromUtf8(line.mid(1, end - 1).trimmed());
            continue;
        }
        const int eq = line.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = QString::fromUtf8(line.left(eq).trimmed());
        QByteArray value = line.mid(eq + 1).trimmed();
        if (value.startsWith('"') || value.startsWith('\'')) {
            const int close = value.indexOf(value[0], 1);
            value = close > 0 ? value.mid(1, close - 1) : value.mid(1);
        } else {
            const int hash = value.indexOf('#');
            if (hash >= 0)
                value = value.left(hash).trimmed();
        }
        out.insert(section.isEmpty() ? key : section + QLatin1Char('.') + key, QString::fromUtf8(value));
    }
    return out;
}

QColor Theme::resolve(const QHash<QString, QString> &shell, const QString &key, const QColor &fallback) const
{
    // A value is a hex colour, a palette role, or a reference to another
    // "section.key" (the shell follows those too).
    QString token = shell.value(key).section(QLatin1Char(' '), 0, 0).trimmed();
    for (int guard = 0; guard < 8 && shell.contains(token); ++guard)
        token = shell.value(token).section(QLatin1Char(' '), 0, 0).trimmed();
    if (token.isEmpty())
        return fallback;
    const QString role = token.toLower();
    if (role == QLatin1String("foreground") || role == QLatin1String("text")) return m_foreground;
    if (role == QLatin1String("accent")) return m_accent;
    if (role == QLatin1String("urgent")) return m_red;
    if (role == QLatin1String("muted")) return m_muted;
    if (role == QLatin1String("background")) return m_background;
    const QColor c(token);
    return c.isValid() ? c : fallback;
}

void Theme::reload()
{
    const QHash<QString, QString> colors = parseToml(readAll(m_themeDir + QLatin1String("/colors.toml")));
    QHash<QString, QString> shell = parseToml(readAll(m_themeDir + QLatin1String("/shell.toml")));
    const QHash<QString, QString> user = parseToml(readAll(m_userShell));
    for (auto it = user.constBegin(); it != user.constEnd(); ++it)
        shell.insert(it.key(), it.value()); // the user's file wins

    m_colors.clear();
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
        const QColor c(it.value());
        if (c.isValid())
            m_colors.insert(it.key(), c);
    }
    // Named keys first, then the 16-colour terminal palette older themes
    // ship, then Tokyo Night.
    auto pick = [&](std::initializer_list<const char *> names, const char *fallback) {
        for (const char *n : names) {
            const auto it = m_colors.constFind(QLatin1String(n));
            if (it != m_colors.constEnd())
                return *it;
        }
        return QColor(QLatin1String(fallback));
    };
    auto mix = [](const QColor &a, const QColor &b, qreal t) {
        return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                                float(a.blueF() + (b.blueF() - a.blueF()) * t));
    };
    const bool themed = !m_colors.isEmpty();
    m_background = pick({"background", "color0"}, "#1a1b26");
    m_foreground = pick({"foreground", "color7"}, "#a9b1d6");
    m_accent = pick({"accent", "blue", "color4"}, "#7aa2f7");
    m_red = pick({"red", "color1"}, "#f7768e");
    m_green = pick({"green", "color2"}, "#9ece6a");
    m_yellow = pick({"yellow", "color3"}, "#e0af68");
    m_orange = pick({"orange", "bright_yellow", "color11"}, "#eb927b");
    m_cyan = pick({"cyan", "color6"}, "#449dab");
    m_magenta = pick({"magenta", "color5"}, "#ad8ee6");
    // Derived from background and foreground when a theme has no such key,
    // so a light theme still gets a darker-than-paper footer, not a black one.
    const QColor black(0, 0, 0);
    m_surface = themed ? (m_colors.contains(QStringLiteral("dark_background")) ? m_colors[QStringLiteral("dark_background")] : mix(m_background, black, 0.22))
                       : QColor(QLatin1String("#13141c"));
    m_raised = themed ? (m_colors.contains(QStringLiteral("lighter_background")) ? m_colors[QStringLiteral("lighter_background")] : mix(m_background, m_foreground, 0.10))
                      : QColor(QLatin1String("#24283b"));
    m_bright = themed ? (m_colors.contains(QStringLiteral("bright_foreground")) ? m_colors[QStringLiteral("bright_foreground")] : m_foreground)
                      : QColor(QLatin1String("#c0caf5"));
    m_dim = themed ? (m_colors.contains(QStringLiteral("dark_foreground")) ? m_colors[QStringLiteral("dark_foreground")] : mix(m_foreground, m_background, 0.45))
                   : QColor(QLatin1String("#565f89"));
    m_muted = themed ? (m_colors.contains(QStringLiteral("muted")) ? m_colors[QStringLiteral("muted")]
                        : m_colors.contains(QStringLiteral("color8")) ? m_colors[QStringLiteral("color8")] : mix(m_foreground, m_background, 0.65))
                     : QColor(QLatin1String("#414868"));

    auto num = [&](const char *key, double fallback) {
        bool ok = false;
        const double v = shell.value(QLatin1String(key)).toDouble(&ok);
        return ok ? v : fallback;
    };
    // [style] is the older name of [controls].
    auto control = [&](const char *key, double fallback) {
        const QByteArray k(key);
        return num(("controls." + k).constData(), num(("style." + k).constData(), fallback));
    };
    auto controlColor = [&](const char *key, const QColor &fallback) {
        const QString k = QLatin1String(key);
        return resolve(shell, shell.contains(QLatin1String("controls.") + k) ? QLatin1String("controls.") + k : QLatin1String("style.") + k, fallback);
    };

    m_normalFillAlpha = qBound(0.0, control("normal-fill-alpha", 0.04), 1.0);
    m_pressedFillAlpha = qBound(0.0, control("pressed-fill-alpha", 0.22), 1.0);
    m_selectedFillAlpha = qBound(0.0, control("selected-fill-alpha", 0.18), 1.0);
    const QColor normal = controlColor("normal-color", m_foreground);
    const QColor selected = controlColor("selected-color", m_foreground);
    m_normalFill = withAlpha(normal, m_normalFillAlpha);
    m_normalBorder = withAlpha(controlColor("normal-border", normal), control("normal-border-alpha", 0.4));
    m_pressedFill = withAlpha(controlColor("pressed-color", normal), m_pressedFillAlpha);
    m_selectedFill = withAlpha(selected, m_selectedFillAlpha);
    m_selectedBorder = withAlpha(controlColor("selected-border", selected), control("selected-border-alpha", 1.0));
    m_separator = withAlpha(normal, control("normal-border-alpha", 0.4) * 0.5);
    m_borderWidth = qMax(0, int(std::lround(control("normal-border-width", 1))));
    m_selectedBorderWidth = qMax(0, int(std::lround(control("selected-border-width", 0))));
    // The shell takes its rounding from Hyprland and is square elsewhere;
    // a theme can state it here for this app.
    m_radius = qMax(0, int(std::lround(control("corner-radius", 0))));
    m_scrim = withAlpha(resolve(shell, QStringLiteral("menu.scrim"), m_background), num("menu.scrim-alpha", 0.5) * 0.4 + 0.5);

    const int base = qMax(1, int(std::lround(num("font.base-size", 12))));
    auto font = [&](const char *key, double mult) {
        const double pinned = num((QByteArray("font.") + key).constData(), 0);
        return pinned > 0 ? int(std::lround(pinned)) : qMax(1, int(std::lround(base * mult)));
    };
    m_fontCaption = font("caption", 0.833);
    m_fontSmall = font("body-small", 0.917);
    m_fontBody = font("body", 1.0);
    m_fontSubtitle = font("subtitle", 1.083);
    m_fontTitle = font("title", 1.167);
    m_fontHeading = font("heading", 1.333);
    m_fontDisplay = font("display", 2.0);

    const QString withFont = shell.value(QStringLiteral("spacing.scale-with-font"), QStringLiteral("true")).toLower();
    const bool scaleWithFont = withFont != QLatin1String("false") && withFont != QLatin1String("0") && withFont != QLatin1String("no");
    m_scale = qMax(0.25, num("spacing.scale", 1.0)) * (scaleWithFont ? qMax(1.0 / 12, base / 12.0) : 1.0);

    watch();
    emit changed();
}
