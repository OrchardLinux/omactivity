// The look follows Folio, Omarchy's file manager: the palette of the current
// Omarchy theme (its colors.toml, nothing else), fills instead of outlines.
// Everything translucent is derived here with fixed alphas; themes do not
// set those. Reloaded when the theme changes. Without a theme the values are
// Tokyo Night, which the mockups were drawn with.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

class QProcess;

class Theme : public QObject
{
    Q_OBJECT
#define THEME_COLOR(name) Q_PROPERTY(QColor name MEMBER m_##name NOTIFY changed)
    THEME_COLOR(background)       // window, menus, dialogs
    THEME_COLOR(surface)          // footers, inspect panel and sheets, search field
    THEME_COLOR(well)             // graph and command-line boxes
    THEME_COLOR(raised)           // the app icon tile in the inspect header
    THEME_COLOR(foreground)       // body text, icons
    THEME_COLOR(bright)           // titles, emphasised numbers, selected labels
    THEME_COLOR(secondary)        // labels, metadata, units, column headers
    THEME_COLOR(muted)            // disabled text only
    THEME_COLOR(accent)
    THEME_COLOR(accentForeground) // text and icons on a primary button
    THEME_COLOR(line)             // the few hairlines left
    THEME_COLOR(menuBorder)
    THEME_COLOR(fieldBorder)
    THEME_COLOR(quiet)            // fill of normal buttons and count badges
    THEME_COLOR(hover)
    THEME_COLOR(pressed)
    THEME_COLOR(tint)             // active tab, active toggle
    THEME_COLOR(tintStrong)       // selected row, highlighted menu entry
    THEME_COLOR(scrim)
    THEME_COLOR(handle)           // the grab handle of a bottom sheet
    THEME_COLOR(redTint)
    THEME_COLOR(yellowTint)
    THEME_COLOR(orangeTint)
    THEME_COLOR(red)
    THEME_COLOR(green)
    THEME_COLOR(yellow)
    THEME_COLOR(orange)
    THEME_COLOR(cyan)
    THEME_COLOR(magenta)
#undef THEME_COLOR
    Q_PROPERTY(bool light MEMBER m_light NOTIFY changed)
    // Hyprland's window rounding; 0 (square) everywhere else.
    Q_PROPERTY(int radius MEMBER m_radius NOTIFY changed)
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily CONSTANT)
    Q_PROPERTY(int fontCaption MEMBER m_fontCaption CONSTANT) // 10: the smallest notes
    Q_PROPERTY(int fontLabel MEMBER m_fontLabel CONSTANT)     // 11: labels, section headings
    Q_PROPERTY(int fontMeta MEMBER m_fontMeta CONSTANT)       // 12: metadata, footer numbers
    Q_PROPERTY(int fontText MEMBER m_fontText CONSTANT)       // 13: body and table
    Q_PROPERTY(int fontTitle MEMBER m_fontTitle CONSTANT)     // 15: sheet and panel titles
    Q_PROPERTY(int fontDialog MEMBER m_fontDialog CONSTANT)   // 16: dialog titles
    Q_PROPERTY(int fontDisplay MEMBER m_fontDisplay CONSTANT) // 24: the one big number

public:
    explicit Theme(QObject *parent = nullptr);

    // The path can be replaced for tests.
    void setThemeDir(const QString &themeDir);
    void reload();
    // Asked from Hyprland while the app is in front; a no-op elsewhere.
    void pollRounding();

    static QHash<QString, QString> parseToml(const QByteArray &data); // key -> value
    static int roundingFrom(const QByteArray &hyprctlJson);
    static qreal luminance(const QColor &c); // WCAG relative luminance

signals:
    void changed();

private:
    void watch();

    QString m_themeDir;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_roundingTimer;
    QProcess *m_hyprctl = nullptr;
    QHash<QString, QColor> m_colors;

    QColor m_background, m_surface, m_well, m_raised, m_foreground, m_bright, m_secondary, m_muted;
    QColor m_accent, m_accentForeground, m_line, m_menuBorder, m_fieldBorder, m_quiet, m_hover, m_pressed;
    QColor m_tint, m_tintStrong, m_scrim, m_handle, m_redTint, m_yellowTint, m_orangeTint;
    QColor m_red, m_green, m_yellow, m_orange, m_cyan, m_magenta;
    bool m_light = false;
    int m_radius = 0;
    QString m_fontFamily = QStringLiteral("monospace");
    int m_fontCaption = 10, m_fontLabel = 11, m_fontMeta = 12, m_fontText = 13, m_fontTitle = 15;
    int m_fontDialog = 16, m_fontDisplay = 24;
};
