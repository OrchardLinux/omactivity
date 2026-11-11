// The look comes from the current Omarchy theme, the same files the shell
// reads: colours from colors.toml, control styling, spacing and the type
// scale from shell.toml merged with the user's ~/.config/omarchy/shell.toml.
// Reloaded when the theme changes. The defaults are the Tokyo Night values
// the mockups were drawn with.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTimer>

class Theme : public QObject
{
    Q_OBJECT
#define THEME_COLOR(name) Q_PROPERTY(QColor name MEMBER m_##name NOTIFY changed)
    THEME_COLOR(background)
    THEME_COLOR(surface)   // footers, the column header, the inspect panel
    THEME_COLOR(raised)    // selected rows and pressed controls
    THEME_COLOR(foreground)
    THEME_COLOR(bright)    // headings, emphasised numbers
    THEME_COLOR(dim)       // labels, secondary text
    THEME_COLOR(muted)     // disabled text, separators
    THEME_COLOR(accent)
    THEME_COLOR(red)
    THEME_COLOR(green)
    THEME_COLOR(yellow)
    THEME_COLOR(orange)
    THEME_COLOR(cyan)
    THEME_COLOR(magenta)
    THEME_COLOR(scrim)
    THEME_COLOR(normalFill)
    THEME_COLOR(normalBorder)
    THEME_COLOR(pressedFill)
    THEME_COLOR(selectedFill)
    THEME_COLOR(selectedBorder)
    THEME_COLOR(separator)
#undef THEME_COLOR
    Q_PROPERTY(int borderWidth MEMBER m_borderWidth NOTIFY changed)
    Q_PROPERTY(int selectedBorderWidth MEMBER m_selectedBorderWidth NOTIFY changed)
    Q_PROPERTY(int radius MEMBER m_radius NOTIFY changed)
    Q_PROPERTY(qreal normalFillAlpha MEMBER m_normalFillAlpha NOTIFY changed)
    Q_PROPERTY(qreal pressedFillAlpha MEMBER m_pressedFillAlpha NOTIFY changed)
    Q_PROPERTY(qreal selectedFillAlpha MEMBER m_selectedFillAlpha NOTIFY changed)
    Q_PROPERTY(qreal scale MEMBER m_scale NOTIFY changed) // spacing: 1.0 at font base-size 12
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily NOTIFY changed)
    Q_PROPERTY(int fontCaption MEMBER m_fontCaption NOTIFY changed)
    Q_PROPERTY(int fontSmall MEMBER m_fontSmall NOTIFY changed)
    Q_PROPERTY(int fontBody MEMBER m_fontBody NOTIFY changed)
    Q_PROPERTY(int fontSubtitle MEMBER m_fontSubtitle NOTIFY changed)
    Q_PROPERTY(int fontTitle MEMBER m_fontTitle NOTIFY changed)
    Q_PROPERTY(int fontHeading MEMBER m_fontHeading NOTIFY changed)
    Q_PROPERTY(int fontDisplay MEMBER m_fontDisplay NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);

    // Paths can be replaced for tests.
    void setPaths(const QString &themeDir, const QString &userShell);
    void reload();

    static QHash<QString, QString> parseToml(const QByteArray &data); // "section.key" -> value; colors.toml has no section
    QColor color(const QString &name) const { return m_colors.value(name); }

signals:
    void changed();

private:
    void watch();
    QColor resolve(const QHash<QString, QString> &shell, const QString &key, const QColor &fallback) const;

    QString m_themeDir;
    QString m_userShell;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QHash<QString, QColor> m_colors;

    QColor m_background, m_surface, m_raised, m_foreground, m_bright, m_dim, m_muted, m_accent;
    QColor m_red, m_green, m_yellow, m_orange, m_cyan, m_magenta, m_scrim;
    QColor m_normalFill, m_normalBorder, m_pressedFill, m_selectedFill, m_selectedBorder, m_separator;
    int m_borderWidth = 1, m_selectedBorderWidth = 0, m_radius = 0;
    qreal m_normalFillAlpha = 0.04, m_pressedFillAlpha = 0.22, m_selectedFillAlpha = 0.18;
    qreal m_scale = 1.0;
    QString m_fontFamily = QStringLiteral("monospace");
    int m_fontCaption = 10, m_fontSmall = 11, m_fontBody = 12, m_fontSubtitle = 13, m_fontTitle = 14;
    int m_fontHeading = 16, m_fontDisplay = 24;
};
