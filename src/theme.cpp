#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

#include <cmath>

namespace {
QColor withAlpha(QColor c, qreal a)
{
    c.setAlphaF(float(qBound(0.0, a, 1.0)));
    return c;
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
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

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80); // a theme switch replaces several files
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    reload();

    // Corners follow Hyprland's window rounding, as in Folio. Omarchy A runs
    // labwc, where there is nobody to ask and everything stays square.
    if (qEnvironmentVariableIsSet("HYPRLAND_INSTANCE_SIGNATURE")) {
        m_roundingTimer.setInterval(2000);
        connect(&m_roundingTimer, &QTimer::timeout, this, &Theme::pollRounding);
        auto follow = [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                pollRounding();
                m_roundingTimer.start();
            } else {
                m_roundingTimer.stop();
            }
        };
        if (qGuiApp) {
            connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, follow);
            follow(qGuiApp->applicationState());
        }
        pollRounding();
    }
}

void Theme::setThemeDir(const QString &themeDir)
{
    m_themeDir = themeDir;
    reload();
}

void Theme::watch()
{
    // A theme switch may replace the directory or the symlink to it, which
    // drops the watches, so they are set again after every reload.
    const QStringList paths{m_themeDir, QFileInfo(m_themeDir).path(), m_themeDir + QLatin1String("/colors.toml")};
    for (const QString &p : paths) {
        if (QFileInfo::exists(p) && !m_watcher.files().contains(p) && !m_watcher.directories().contains(p))
            m_watcher.addPath(p);
    }
}

int Theme::roundingFrom(const QByteArray &hyprctlJson)
{
    // {"option": "decoration:rounding", "int": 8, "set": true}
    const QJsonObject o = QJsonDocument::fromJson(hyprctlJson).object();
    return qBound(0, o.value(QLatin1String("int")).toInt(0), 64);
}

void Theme::pollRounding()
{
    if (m_hyprctl && m_hyprctl->state() != QProcess::NotRunning)
        return;
    if (!m_hyprctl) {
        m_hyprctl = new QProcess(this);
        connect(m_hyprctl, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const int r = status == QProcess::NormalExit && code == 0 ? roundingFrom(m_hyprctl->readAllStandardOutput()) : 0;
            if (r != m_radius) {
                m_radius = r;
                emit changed();
            }
        });
        connect(m_hyprctl, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                m_roundingTimer.stop(); // no hyprctl: stay square
        });
    }
    m_hyprctl->start(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
}

qreal Theme::luminance(const QColor &c)
{
    auto lin = [](qreal v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

QHash<QString, QString> Theme::parseToml(const QByteArray &data)
{
    // colors.toml is flat: key = "string" | number | bare-word lines, with
    // trailing comments. A [section] header prefixes the keys under it.
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

void Theme::reload()
{
    const QHash<QString, QString> colors = parseToml(readAll(m_themeDir + QLatin1String("/colors.toml")));
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
    auto has = [&](const char *name) { return m_colors.contains(QLatin1String(name)); };
    const bool themed = !m_colors.isEmpty();
    const QColor black(0, 0, 0), white(255, 255, 255);

    m_background = pick({"background", "color0"}, "#1a1b26");
    m_foreground = pick({"foreground", "color7"}, "#a9b1d6");
    m_accent = pick({"accent", "blue", "color4"}, "#7aa2f7");
    m_red = pick({"red", "color1"}, "#f7768e");
    m_green = pick({"green", "color2"}, "#9ece6a");
    m_yellow = pick({"yellow", "color3"}, "#e0af68");
    m_orange = pick({"orange", "bright_yellow", "color11"}, "#eb927b");
    m_cyan = pick({"cyan", "color6"}, "#449dab");
    m_magenta = pick({"magenta", "color5"}, "#ad8ee6");

    const QString mode = colors.value(QStringLiteral("mode")).toLower();
    m_light = mode == QLatin1String("light") || (mode.isEmpty() && luminance(m_background) > 0.4);

    // Derived from the background when a theme has no such key: 22 % toward
    // black on a dark theme. On paper that would be a grey slab that small
    // secondary text drowns in, so a light theme only goes 6 %.
    m_surface = themed ? (has("dark_background") ? m_colors[QStringLiteral("dark_background")] : mix(m_background, black, m_light ? 0.06 : 0.22))
                       : QColor(QLatin1String("#13141c"));
    m_well = mix(m_surface, black, m_light ? 0.05 : 0.28);
    m_raised = themed && has("lighter_background") ? m_colors[QStringLiteral("lighter_background")]
               : themed ? m_background : QColor(QLatin1String("#24283b"));
    // Some themes set it to the background itself, which hides the tile;
    // the tile sits on the surface, so it must stand out from that too.
    if (std::abs(luminance(m_raised) - luminance(m_background)) < 0.004)
        m_raised = mix(m_background, m_foreground, 0.08);
    if (std::abs(luminance(m_raised) - luminance(m_surface)) < 0.004)
        m_raised = mix(m_surface, m_foreground, 0.08);
    m_bright = themed ? (has("bright_foreground") ? m_colors[QStringLiteral("bright_foreground")] : m_foreground)
                      : QColor(QLatin1String("#c0caf5"));
    // Not the theme's dark_foreground: that is too faint for small text.
    m_secondary = mix(m_background, m_foreground, 0.68);
    m_muted = themed ? (has("muted") ? m_colors[QStringLiteral("muted")]
                        : has("color8") ? m_colors[QStringLiteral("color8")] : mix(m_foreground, m_background, 0.65))
                     : QColor(QLatin1String("#414868"));
    m_accentForeground = luminance(m_accent) > 0.179 ? black : white;

    m_line = withAlpha(m_foreground, 0.12);
    m_menuBorder = withAlpha(m_foreground, 0.16);
    m_fieldBorder = withAlpha(m_foreground, 0.18);
    m_quiet = withAlpha(m_foreground, 0.05);
    m_hover = withAlpha(m_foreground, 0.07);
    m_pressed = withAlpha(m_foreground, 0.10);
    m_handle = withAlpha(m_foreground, 0.25);
    m_tint = withAlpha(m_accent, 0.16);
    m_tintStrong = withAlpha(m_accent, 0.18);
    m_scrim = withAlpha(black, 0.55);
    m_redTint = withAlpha(m_red, 0.14);
    m_yellowTint = withAlpha(m_yellow, 0.15);
    m_orangeTint = withAlpha(m_orange, 0.15);

    watch();
    emit changed();
}
