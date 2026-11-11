# Design update: the Folio direction

2026-11-11 · Brief for implementing the restyle. Omactivity now follows the look of [Folio](https://github.com/omacom/folio), Omarchy's file manager, instead of the shell's control styling: same Omarchy colours, fewer borders, fills instead of outlines.

**Mockups:** `mockups/` (PNG) and the frames named "… · Folio direction" in `omactivity.pen`. The older, outline-heavy frames in `omactivity.pen` (without the suffix) are outdated; ignore them.

| Mockup | Shows |
| --- | --- |
| `ipad6-portrait-cpu.png` | Toolbar, tabs, search, table, CPU footer |
| `ipad6-landscape-memory-inspect.png` | Inspect panel, Memory footer |
| `iphone6-cpu.png` | Narrow layout, bottom tabs |
| `iphone6-long-press-menu.png` | **The new long-press menu** |
| `iphone6-inspect-sheet.png` | Inspect as a bottom sheet |
| `iphone6-protected-process.png` | **The new confirm dialog** |

This brief replaces the **Theme** bullet in `SPEC.md` (Stack and architecture) and its acceptance check about `shell.toml`. Everything else in the spec stands.

## 1. Theme source

**Read only the theme's `colors.toml`; stop reading `shell.toml`.** Folio does the same, and its `src/theme.cpp` (MIT) is the model to copy.

- Path unchanged: `~/.local/state/omarchy/current/theme/colors.toml` (then `~/.config/omarchy/current/theme`). Keep the file watching and debounce; Folio uses 80 ms.
- Read the whole palette plus `mode = "light"`, and set the Qt Quick Controls Material style from it: `Material.theme` light or dark, `Material.accent`, `Material.foreground`, `Material.background`, `Material.roundedScale` = radius.
- Remove from `Theme`: the `shell.toml` and user `shell.toml` merge, `resolve()`, the `[controls]` / `[style]` / `[font]` / `[spacing]` keys, `normalBorder`, `borderWidth`, `selectedBorder`, `selectedBorderWidth`, the fill-alpha properties and the font scale from `base-size`.
- **Corners:** Hyprland's `decoration:rounding` via `hyprctl -j getoption decoration:rounding`, polled while the app is active, as Folio does. Without Hyprland (Omarchy A runs labwc) it's 0, so square. Controls use `Math.min(4, radius)`, menus `Math.min(6, radius)`, cards and graphs `Math.min(10, radius)`.
- **Font:** the `monospace` family at **13 px** as the app font (set in `main.cpp`). Sizes: 11 labels and section headings, 12 metadata and numbers in footers, 13 body and table, 15 sheet and panel titles, 16 dialog titles. Weight 600 for emphasis, not bold everywhere.
- **No theme:** fall back to the Tokyo Night values below, as now.

## 2. Colour tokens

Palette keys come straight from `colors.toml`. Everything translucent is derived in code with fixed alphas; themes don't set these.

| Token | Source | Tokyo Night | Used for |
| --- | --- | --- | --- |
| `background` | `background` | `#1a1b26` | Window, menus, dialogs |
| `surface` | `dark_background` (or background mixed 22% toward black) | `#13141c` | Footers, inspect panel and sheets, search field, graph backgrounds |
| `raised` | `lighter_background`; if it's within 0.004 luminance of the background, the background mixed 8% toward the foreground | `#24283b` | App icon tile in the inspect header |
| `foreground` | `foreground` | `#a9b1d6` | Body text, icons |
| `bright` | `bright_foreground` | `#c0caf5` | Titles, emphasised numbers, selected labels |
| `secondary` | background mixed **68%** toward foreground (replaces `dim` = `dark_foreground`, too faint for small text) | `#7b819e` | Labels, metadata, units, column headers |
| `muted` | `muted` / `color8` | `#414868` | Disabled text only (tabs not shipped yet, `—` cells) |
| `accent` | `accent` | `#7aa2f7` | Primary button, selected text, sort arrow, user CPU |
| `accentForeground` | black or white, whichever reads on `accent` (relative luminance above 0.179 means black) | `#000000` | Text and icons on a primary button |
| `line` | foreground at **12%** | `#a9b1d61f` | The few hairlines left (see below) |
| `menuBorder` | foreground at **16%** | `#a9b1d629` | Menu outline, menu separators |
| `fieldBorder` | foreground at **18%** | `#a9b1d62e` | Search field outline (accent while focused) |
| `quiet` | foreground at **5%** | `#a9b1d60d` | Fill of normal text buttons, count badges |
| `hover` | foreground at 6–8% | | Mouse hover only |
| `pressed` | foreground at **10%** | | Any pressed control or row |
| `tint` | accent at **16%** | `#7aa2f729` | Active tab, active toggle (Inspect button) |
| `tintStrong` | accent at **18%** | `#7aa2f72e` | Selected row, highlighted menu entry |
| `scrim` | **black at 55%** (not background-coloured) | `#0000008c` | Behind dialogs, sheets and the long-press menu |
| `redTint` | red at 14% | `#f7768e24` | Force Quit button fill |
| `yellowTint` | yellow at 15% | `#e0af6826` | Paused badge fill |
| `red`, `green`, `yellow`, `orange`, `cyan`, `magenta` | palette | as now | Data, app glyphs, warnings |

Check the derived values with a light theme too (Catppuccin Latte or Flexoki Light).

## 3. General rules

- **No borders on controls.** Buttons, tabs, badges, graph frames and count chips lose their outlines. Borders are left in four places only: the search field (`fieldBorder`), menus (`menuBorder`), dialogs (an accent or red edge, see §5) and keyboard focus (1 px accent ring, only on `activeFocus` from the keyboard).
- **Hairlines (`line`, 1 px) only:** under the column header, on top of the CPU and Memory footers and the bottom tab bar, on the left of the inspect panel, and on top of the inspect sheet. **No row separators** in the table.
- **Selection is a tint:** selected row = `tintStrong` fill, name in `accent`, no border. Active tab = `tint` fill with `bright` text.
- **One solid button per surface.** The inspect header's **Quit** is the primary button (accent fill, `accentForeground` text). The toolbar Quit is a normal quiet button. Force Quit is `redTint` fill with red text and icon.
- **Text sizes and 44 px targets:** visible controls are 44 px tall on touch, not 36 px with a larger hit area. Rows stay 48 px (60 comfortable), the column header 44.

## 4. Components to change

| File | Change |
| --- | --- |
| `src/theme.h`, `src/theme.cpp` | §1 and §2: new token set, drop `shell.toml`, add `mode`, `accentForeground`, the derived alphas and `hyprctl` rounding |
| `src/main.cpp` | App font `monospace` 13 px; `QQuickStyle::setStyle("Material")` |
| `qml/Ui.qml` | `controlHeight` 44; `line` = 1; drop `sp()` scaling from the shell's spacing (keep `sp` as identity or a fixed scale); app glyph colours unchanged, `cpu`/`layers` use `secondary` |
| `qml/Btn.qml` | `normal`: `quiet` fill, no border. `primary`: `accent` fill, `accentForeground` text, weight 600. `danger`: `redTint` fill, red text. Icon-only: transparent, `pressed` on press, `tint` when `selected`. Focus ring as §3 |
| `qml/Segmented.qml` | Tabs as quiet text buttons without an outline or dividers; selected = `tint` + `bright` text; not-yet-shipped tabs `muted` |
| `qml/SearchField.qml` | `surface` fill, `fieldBorder` outline, accent outline while focused |
| `qml/Section.qml` | No rule after the title. Title 11 px, uppercase, letter spacing 1.3, `secondary`, normal weight |
| `qml/ProcessTable.qml` | No row separators; selected row as §3; header without fill, `line` underneath, labels `secondary` 12 px, sorted column `bright` with an accent arrow; count badges `quiet` fill, no border; Paused badge `yellowTint`, no border |
| `qml/Footer.qml`, `qml/BarGraph.qml`, `qml/LineGraph.qml` | Footer on `surface` with a `line` on top; graph box on `surface` (or `background` inside the footer) with no frame; section titles as `Section` |
| `qml/InspectPanel.qml` | `surface` background, `line` on the left; header: Quit primary, Force Quit danger, More icon-only, close icon-only; tabs as `Segmented`; command-line box `background` fill, no border |
| `qml/BottomTabs.qml` | `surface` bar with a `line` on top; active tab `tint` + `bright` |
| `qml/Sheet.qml` | Scrim black 55%; cards without a border except dialogs (`cardBorder` stays, used by §5); bottom sheets get a `line` on top and a grab handle (36 × 4, foreground at 25%) |
| `qml/MenuRow.qml` | Replaced by the menu entry in §6 |

## 5. Confirm dialog

**Folio's `ConfirmDialog`: a red edge for anything destructive, Cancel quiet, the action solid.** The current dialog in `Main.qml` (the "confirmation" `Sheet`) has an accent border, an accent "primary" Cancel and a red "danger" action. Mockup: `iphone6-protected-process.png`.

- **Card:** `background` fill, **1 px red edge** (`cardBorder: theme.red`) for Force Quit, protected processes and anything through `sudo`. The accent edge stays for non-destructive prompts, such as the `sudo` password prompt without an error. Padding 18 px, corner radius from §1.
- **Scrim:** black at 55%.
- **Title row:** the `alert` icon in yellow, 18 px, then the title in `bright`, 15–16 px, weight 600.
- **Message:** `foreground`, 13–14 px, line height 1.4.
- **Note** (the `protected.conf` line): `secondary`, 10–11 px.
- **Buttons**, right-aligned, 10 px apart, 44 px tall, at least 112 px wide:
  - **Cancel:** `quiet` fill, `foreground` text, no border. Keyboard focus starts here, so Enter cancels; it shows the 1 px accent focus ring.
  - **The action** ("Force Quit", "Quit Tree", …): **`primary`**, solid accent with `accentForeground` text, weight 600. Not `danger`: the red edge already says it's destructive.

## 6. Long-press menu

**A menu next to the lifted row, not a sheet of big rows.** The current long-press opens the "more actions" `Sheet` in `Main.qml`: a centred card, or a bottom sheet on the phone, with 48–56 px rows that each have an icon and a detail line. Mockup: `iphone6-long-press-menu.png`.

**Lifting the row**

- On long-press, a black 55% scrim covers the window under the shell bar.
- The pressed row is drawn above it, where it was, as a card: `background` fill, **2 px accent frame**, slight shadow (0, 12, blur 28, black 70%), 4–8 px inset from the screen edges. It shows the glyph, the name in `accent` weight 600, `PID · user · app` in `secondary` 11 px, and on the right the sort column's value (`23.1%` in `bright`, the column name in `secondary` 10 px).
- If the row is too close to the bottom for the menu, the menu opens above the row instead.

**The menu**

- **Box:** directly under the lifted row (6 px gap), left-aligned with it, **300 px wide**. `background` fill, 1 px `menuBorder`, 4 px top and bottom padding, menu corner radius, shadow (0, 12, blur 32, black 60%). No open or close animation, as in Folio.
- **Narrow layout:**
  - At the top, a 60 px row of four equal icon buttons, each an 18 px icon over a 10 px label: **Quit**, **Force Quit** (icon and label red), **Pause** (or **Resume** for a paused process), **Inspect** (hidden when the inspector already shows this process).
  - A separator: a 1 px `menuBorder` line with 4 px above and below.
  - Then entries, 44 px each: **Send Signal** with a `chevron-right` on the right, **Change Priority…**, **Quit Process Tree**.
- **Medium and wide:** the same box with all actions as entries, no icon row: Inspect · *separator* · Quit (`Delete`) · Force Quit (`Shift+Delete`, red text) · *separator* · Pause or Resume · Send Signal › · Change Priority… · Quit Process Tree.
- **Entries:**
  - Each is 44 px tall with an inner box inset 4 px left and right and 10 px text padding. Label 13 px `foreground`; key hints right-aligned in `secondary` 11 px; no icons and **no detail lines** (the signal and nice details move into their own sheets).
  - **Highlight:** the inner box takes `tintStrong` and the label turns `bright`, while a finger slides over the entry or the keyboard is on it. Pressed: `pressed`.
- **Blocked:** when an action isn't allowed ("Owned by root, needs your password" and the like), a line in `yellow` 11 px sits at the top of the box, and the affected entries are `secondary` and disabled.
- **Closing:** tapping the scrim or the lifted row, or Esc. Choosing an entry closes the menu first. Send Signal opens the signal list as a second menu at the same place (narrow) or as a submenu to the right (wide).
- **Unchanged:** the long-press still never scrolls the list underneath (`ProcessTable.qml`, `heldForMenu`).

## 7. Done when

- [ ] The mockups in `mockups/` and screenshots from `dev/shot` match side by side at Tokyo Night.
- [ ] Switching to a light theme and back restyles the open app, with readable secondary text.
- [ ] With Hyprland rounding set, controls and menus round; on labwc everything is square.
- [ ] No control outside the four places in §3 draws a border.
- [ ] The confirm dialog for Force Quit on labwc has a red edge, a quiet Cancel focused by default, and a solid Force Quit.
- [ ] Long-press on a phone shows the lifted row and the compact menu; on a tablet the full entry list; Send Signal opens its list in place.
