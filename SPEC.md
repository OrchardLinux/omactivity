# Omactivity: spec

2026-11-11 · Updated to match what was built and tried on the iPad 6.

A touch-first task manager for Omarchy A on tablets and phones, built to the bar of Apple's Activity Monitor: find any process by name, see the process tree, sort by CPU or memory, inspect it, and quit or force-quit it. It is the GUI successor to btop on touch devices, where btop is hidden and has no touch path. It lives in its own repository and ships to Omarchy A as a package, the way upstream Omarchy ships Omacalc.

Name: **Omactivity** (binary `omactivity`, repo `OrchardLinux/omactivity`). "The app" below means Omactivity.

## Where it lives

**Decision: its own repository under OrchardLinux, packaged into Omarchy A by a PKGBUILD.** Omarchy A only gets the package, a launcher entry, a bar meter and the shell hooks.

- **Upstream does it this way.** Omarchy's own apps are separate repos (Omacalc: Qt 6, its own releases), and Omarchy A already packages Omacalc from a pinned commit in `desktop/packages/omacalc`. The app follows the same pattern.
- **It is not tied to one device.** Nothing in it is iPad- or iPhone-specific: it reads `/proc`, sends signals and draws with Qt. Upstream Omarchy on laptops (and our omarchy-orchard proof of concept on Hyprland) can use it too, and Omarchy A must not be the only way to get it.
- **Its own release pace and CI.** App fixes ship without an Omarchy A image release, and its tests (a fake `/proc` tree, signal handling) don't run in the image CI.

What stays in Omarchy A: the PKGBUILD (aarch64 and armv7h), the menu and bar entries, and launching apps in per-app systemd scopes (see Process list, Apps view). btop is already hidden from the launcher there.

## Stack and architecture

**Qt 6 and C++ throughout: a QML front end and a C++ collector in one process, opened on demand like btop.** Nothing runs while the app is closed: no daemon, no background recorder, no root helper. Same toolkit as Omacalc and as our Quickshell shell, so it costs no new runtime, draws through the same GPU or software path, and touch works out of the box.

Why not Rust for the collector:

- **The collector's output is Qt models.** In C++ these are plain Qt list models. In Rust they need a bridge such as cxx-qt: generated glue code, Cargo next to qmake, and a second toolchain to cross-compile for aarch64 and armv7h, all to wrap a few hundred lines of `/proc` parsing.
- **The collector only parses text.** It reads small `/proc` files, diffs them against the last sample and sums numbers. The tests against a recorded `/proc` tree catch the realistic bugs: format quirks and processes that disappear mid-read.
- **Packaging stays a copy of Omacalc's.** Omacalc's PKGBUILD builds with `qmake6`, needs only `qt6-base` and `qt6-declarative`, and already builds for armv7h. Rust would add a Cargo build step and Rust packages to an image that has none today.
- **Memory.** The 40 MB target is mostly the Qt and QML runtime, which Rust doesn't change. One language also means one allocator and no bridge layer.

- **Collector (C++, in-process).** Reads `/proc` and `/sys` on a timer (1, 2 or 5 s), diffs against the last sample and exposes Qt models: process table, per-app groups, system totals. Takes its first sample at startup, so the list is never empty. No external daemon, no btop code. Data sources per column are in the Tabs section.
- **History.** Kept in memory while the app is open, nowhere else: the graphs start when the app opens and cover up to the last 2 minutes. To show the past straight away, the footers and the inspect sheet use what the kernel already averages: load average over 1, 5 and 15 minutes, memory pressure over 10 s, 1 min and 5 min, uptime, and each process's total CPU time and start time. A background recorder is under Later in Releases.
- **UI (QML).** Tabs, list, search, inspect sheet. Reads only the models. Icons and line graphs are drawn by small C++ items, not `QtQuick.Shapes`: Omarchy A runs Qt Quick with the software renderer on boards without a GPU, and there shapes are not clipped to the list they scroll in.
- **Recorded trees.** `OMACTIVITY_PROC` and `OMACTIVITY_SYS` point the app at a recorded tree (tests, pictures). Every action is then disabled: the PIDs of a recording mean something else on the machine showing it.
- **Root actions through `sudo`.** Acting on a process owned by root or another user, or raising a priority (nice below 0), runs `sudo kill` or `sudo renice`. The app first tries `sudo -n`; only when sudo needs a password does it show its own password field (on-screen keyboard) and pass the password to `sudo -S` on stdin, never on the command line or to disk. sudo's own timestamp then covers further actions for a few minutes (5 by default) while the app stays open. Before sending, the app re-reads the PID's start time from `/proc/PID/stat` and stops if it changed, so a recycled PID is never hit. This needs the user to have sudo rights (`wheel`, the Omarchy default); without them, these actions are disabled with a note saying who owns the process.
- **Theme.** Everything visual comes from the current Omarchy theme (`~/.local/state/omarchy/current/theme`, the path the shell reads), so the app looks like a shell panel and nothing is hard-coded: colours from its `colors.toml`; control styling from its `shell.toml`, merged with the user's `~/.config/omarchy/shell.toml` the way the shell's `Color.qml` and `Style.qml` do it. That covers corner radius, border widths and alphas, fill alphas for the normal, pressed and selected states, spacing and the type scale. Font: the `monospace` fontconfig alias that `omarchy font set` writes, like the shell. Reloaded on theme change. The mockups (`omactivity.pen`) use the Tokyo Night values.
- **Packaging.** qmake (`qmake6`) like Omacalc. Runtime dependencies `qt6-base` (which brings Qt D-Bus), `qt6-declarative` and `wayland` (libwayland-client, for closing windows). Ships a `.desktop` file, an icon and the protected-process list; builds for aarch64 and armv7h.

Why not reuse btop's collectors: btop's collection code is tied to its global config and terminal drawing, and the parts we need (per-process stat, status, io, smaps_rollup) are a few hundred lines of plain `/proc` parsing. Owning them keeps the app small and lets the Apps view group by cgroup, which btop doesn't do.

```
+-- omactivity (user process) ----+
|                                 |      +--------------------------------+
|  QML interface                  |----->| Compositor (labwc or Hyprland) |
|  tabs, list, search, inspect    |      | close app windows              |
|  Quit, Force Quit, signals      |      +--------------------------------+
|                                 |----->| systemd user manager           |
|        ^ Qt models              |      | kill a whole app scope         |
|        |                        |      +--------------------------------+
|  C++ collector                  |----->| sudo kill, sudo renice         |
|  samples every 1, 2 or 5 s      |      | root's and other users' PIDs   |
|                                 |      +---------------+----------------+
+---------------------------------+                      v
            | reads /proc, /sys          +--------------------------------+
            +--------------------------->| Linux kernel                   |
                                         | /proc, /sys; kill(2) on own    |
                                         +--------------------------------+
```

The app talks to the compositor, systemd and the kernel as the user; only signals and priority changes on processes owned by root or another user go through `sudo`.

## Process list

**One list, shared by all tabs; the tab only changes the columns and the footer.** Search, view filter, sort and selection persist across tab switches.

**Layouts.** Picked by window width, not by device, so a split or resized window gets the right one:

| Width | Example | Tabs | Inspect | Default columns |
| --- | --- | --- | --- | --- |
| Wide (≥ 1000 px) | Tablet landscape | Segmented control under the toolbar | Side panel, list stays visible | As in the Tabs section |
| Medium (600–999 px) | Tablet portrait | Segmented control under the toolbar | Sheet slides up | As in the Tabs section |
| Narrow (< 600 px) | Phone portrait | Bottom tab bar with icons | Sheet slides up | Name, the tab's sort column and one more (CPU: % CPU, CPU Time; Memory: Memory, Swap); the rest by scrolling sideways |

On narrow widths the toolbar keeps View, Select and Quit as icon buttons, and search gets its own row under it. The footer shrinks to the three headline numbers and the graph.

**Toolbar.** The title with a one-line summary (current view, process count, refresh rate), then: the View menu, **Select**, **Inspect** (i), which opens the inspect panel or sheet for the selected row, and **Quit**, the primary button. Quit and Inspect act on the selection and are disabled without one.

**Search.** A field at the top filters by process name and command line as you type, case-insensitive. Matches keep their parents visible in hierarchical view. Opening it brings up the on-screen keyboard.

**View filters** (a menu, like Activity Monitor's View menu):

| View | Shows |
| --- | --- |
| Apps (default) | One row per app, its processes summed; expands to the processes |
| All processes | Flat list |
| All processes, hierarchically | Parent/child tree, expand and collapse per node |
| My processes | Owned by the logged-in user |
| System processes | root and system users |
| Other users' processes | Everyone else |
| Active processes | Used CPU in the last sample |
| Windowed processes | Processes that own a window |
| Listening on a port | Processes with a listening TCP or UDP port, with the Ports column first |
| By service | One row per systemd unit (service or scope), expands to its processes; kernel threads under Kernel |
| Selected processes | Only the rows the user picked |

The app reopens in the view it was closed in only for the views that show everything (Apps, All processes, hierarchically, By service). A filter view can be empty, and an app that opens on an empty list looks broken.

**Apps view.** An app = a systemd user scope (`app-<name>@<id>.scope` in `/proc/PID/cgroup`). This needs Omarchy A's launcher to start apps with `systemd-run --user --scope`, like upstream Omarchy's `uwsm-app`. Its name and icon come from the matching `.desktop` file. Processes outside an app scope fall under **System** or **Background**. A window maps to its app through the scope, so labwc needs no PID protocol.

An app row sums its processes' numeric columns and shows the PID and user of its main process, the oldest one in the scope. **System** and **Background** have no main process: their PID and User cells show "—", and the row shows a badge with its process count. The two groups do not take part in the sorting: they always sit under the apps, Background first, so they don't trade places at every refresh.

**Sorting.** Tap a column header to sort, tap again to reverse; an arrow shows the order. Default sort per tab: % CPU, Memory, % CPU (Energy), Bytes Written, Connections. In tree view, siblings sort and children stay under their parent. Ordering by % CPU follows the average of the last four samples, so rows of similar load don't swap at every refresh; the number shown is the newest sample.

**Columns.** Each tab has a default set (Tabs section) plus optional columns the user turns on in a column picker (in the view menu). Always present: name with icon, PID. Choices persist per tab. When the columns don't fit, the name keeps a readable width and the rest scrolls sideways by dragging a row or the header row sideways.

Optional on several tabs:

| Column | Linux source |
| --- | --- |
| Command | `/proc/PID/cmdline`, in full |
| State | `/proc/PID/stat` state: running, sleeping, waiting (blocked in I/O), stopped, zombie |
| CPU Wait | `/proc/PID/schedstat` run-queue delay ÷ interval: runnable but no free core |
| I/O Wait | `/proc/PID/stat` delayacct_blkio_ticks ÷ interval. Needs `kernel.task_delayacct=1`; shows "—" while that is off (it is off by default, also on Omarchy A today) |
| Ports | Listening ports, from `/proc/PID/fd` → `/proc/net/{tcp,tcp6,udp,udp6}` |

A process held up 20 % of the time or more shows a badge in the list, "CPU wait" or "I/O wait", whatever columns are on. Searching `:8080` or `8080` finds the process listening there while ports are being shown.

**Refresh.** 2 s by default; 1 or 5 s in settings. Rows don't jump while a finger is on the list; the re-sort waits until it lifts. Ended processes fade out over one refresh rather than vanishing.

## Tabs

**Five tabs, as in Activity Monitor; all five ship in v1.** Each tab = default columns + a footer of system totals with a live graph. Graphs without a natural maximum scale themselves so the tallest bar in view reaches three quarters of the height; the CPU graph says what its top currently stands for.

### CPU

| Column | Linux source |
| --- | --- |
| % CPU | `/proc/PID/stat` utime+stime delta ÷ interval |
| CPU Time | utime+stime |
| Threads | `/proc/PID/stat` num_threads |
| Idle Wake Ups | `/proc/PID/status` voluntary + nonvoluntary_ctxt_switches delta |
| User | `/proc/PID/status` Uid → name |
| Nice (optional) | `/proc/PID/stat` nice |

Footer: System %, User %, Idle %, a CPU history graph (one line per core optional), load average, uptime, CPU frequency from `/sys/devices/system/cpu/cpu*/cpufreq`.

### Memory

| Column | Linux source |
| --- | --- |
| Memory | `/proc/PID/smaps_rollup` Pss (RSS fallback when unreadable) |
| Real Memory (optional) | `/proc/PID/status` VmRSS |
| Private Memory (optional) | smaps_rollup Private_Clean + Private_Dirty |
| Shared Memory (optional) | smaps_rollup Shared_Clean + Shared_Dirty |
| Swap | smaps_rollup Swap |
| Threads, User | as CPU |

Footer: **memory pressure graph**: bar height is the share of memory in use, bar colour is the kernel's pressure figure from `/proc/pressure/memory` (some avg10: green under 10, yellow under 40, red above), so it is never empty and turns colour when memory gets tight. The figures some/full avg10, avg60 and avg300 are next to it, physical memory, used, cached, swap used, compressed (zram `mm_stat` when zram is in use), all from `/proc/meminfo`.

### Energy

| Column | Linux source |
| --- | --- |
| % CPU | as CPU |
| Avg CPU | utime+stime ÷ time since the process started: its average over its whole life, which the kernel keeps, so it covers the time before the app was opened |
| Idle Wake Ups | context-switch delta, as CPU |
| Prevents Sleep | holder of a logind sleep or idle inhibitor in block mode (`org.freedesktop.login1` ListInhibitors, asked every 10 s while the tab is open). The reason is in the inspect sheet |

Footer: battery level, charging state, time left (or until full), power draw in W with a graph, all from `/sys/class/power_supply` (power from `power_now`, or current × voltage; time from the driver, or charge ÷ draw).

Not in v1: "% CPU over the last hour" (needs the background recorder; Avg CPU stands in), and idle inhibitors that apps set through the compositor (the shell would have to report them).

### Disk

| Column | Linux source |
| --- | --- |
| Bytes Written, Bytes Read | `/proc/PID/io` write_bytes, read_bytes |
| Written/s, Read/s | their delta ÷ interval |

Another user's `io` is unreadable without root; those cells show "—". Footer: reads/s, writes/s, data read/s and written/s with a graph, from `/proc/diskstats` (whole disks only), and free space on `/`.

### Network

Linux has no per-process byte counters in `/proc`.

| Column | Linux source |
| --- | --- |
| Connections | TCP connections among the process's sockets |
| Sockets | all open sockets (`/proc/PID/fd`) |
| Ports | listening ports, as above |

Another user's sockets are unreadable without root ("—"). Listening ports that can't be matched to a process are listed by owner under the "Listening on a port" view ("root: 22, 67/udp"). Footer: data received/s and sent/s with a graph, totals since boot, the busiest interface, from `/sys/class/net/*/statistics`. Per-process bytes need an eBPF program or `nethogs`-style capture; out of scope until someone needs it.

## Actions

**Quit and Force Quit are the core; everything acts on the selected rows, one or many.**

**Quit** (the primary button):

1. An app with a window: ask the compositor to close its windows (`wlr-foreign-toplevel` close), so the app can say "save changes?".
2. No window, or still running after 5 s: SIGTERM to the process, or to every process in the app's scope.
3. Still running 5 s later: the row shows **Not responding · Force Quit**.

**Force Quit**: SIGKILL to the process, or to the whole scope via `systemctl --user kill --signal=KILL <scope>` so no child survives.

**More actions** (in the inspect sheet and the long-press menu):

- Send signal: a list of SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGKILL, SIGSTOP, SIGCONT, SIGUSR1, SIGUSR2, each with a one-line meaning.
- Pause / Resume (SIGSTOP / SIGCONT); a paused row shows a pause badge.
- Change priority: renice -20 to 19, as a slider with Lower / Normal / Higher marks.
- Quit process tree: the selected process and all its descendants, children first.

**Privilege.** Own processes: direct `kill(2)` and `setpriority(2)`. Root's or another user's, and nice below 0: through `sudo`, asking for the password only when sudo needs it (see Stack and architecture).

**Protected processes.** Before acting on PID 1, the compositor (labwc or Hyprland), the shell (Quickshell), the greeter, `systemd-logind`, `dbus`, NetworkManager or the app itself, a confirmation says what will happen ("The screen will go black and you will be logged out"). The list is a config file, `/etc/omactivity/protected.conf`, that the distro can extend; the confirmation names that file so the user can see why the process is protected. Protected rows show a shield in the list. There is no list of processes that can never be killed.

**Confirmation.** Quit needs none. Force Quit and anything through `sudo` confirm once, naming the process and warning that unsaved work is lost. The dialog uses the shell's `ConfirmDialog` look, with Cancel focused by default.

## Inspect sheet

**Tap a row to open a sheet with everything about that process and the action buttons.** The System and Background rows only open and close; they have no sheet. After a confirmed Force Quit from the sheet, the sheet closes. On wide layouts (tablet landscape) it is a side panel and the list stays visible; on medium and narrow layouts (tablet portrait, phones) it slides up as a sheet over the dimmed list (see Layouts).

The header holds the icon, name, PID, user and app, a close button, then **Quit**, **Force Quit** and **More** (…, the More actions menu). Below it, four tabs group the sections in the table below:

- **Info:** Process, CPU, Memory, Statistics
- **Files:** Open Files and Ports
- **Env:** Environment
- **Children:** Children

| Section | Contents | Linux source |
| --- | --- | --- |
| Header | Icon, name, PID, Quit and Force Quit buttons | |
| Process | Parent (tap to jump), user, process group, state (running, sleeping, stopped, zombie), started at, elapsed, the full command line, working directory, App (its app scope, or "—"), Unit (the systemd service or scope it runs in), and what it holds sleep back for, if it does | `stat`, `status`, `cmdline`, `cwd`, `cgroup`, logind |
| CPU | % CPU graph for the last 2 minutes, CPU time, threads, nice, share of time waiting for a CPU and for disk I/O | `stat`, `schedstat` |
| Memory | Real, private, shared, swap, virtual | `status`, `smaps_rollup` |
| Statistics | Context switches, page faults (minor, major), bytes read and written | `status`, `stat`, `io` |
| Open Files and Ports | Every open file, socket and pipe, with search; sockets show local and remote address | `fd`, `/proc/net/*` |
| Environment | Environment variables, hidden behind a tap | `environ` |
| Children | Child processes, tappable | process tree |

Another user's `fd`, `environ` and `io` can't be read without root. Those tabs and rows say "Not available: owned by root" (or the owner's name). `sudo` is only used for kill and renice, not for reading.

**Later:** Sample Process, a 5 s stack sample through `perf` when installed, shown as a call tree.

## Touch and reachability

**The app must open within 1 s and stay usable when the device is nearly out of memory, because that is when people need it.**

**Touch.**

- Rows at least 48 px tall at default scale, column headers 44 px; nothing needs hover.
- No swipe actions on rows: a sideways drag on a row scrolls the columns, like dragging the header. Quit is the toolbar button and in the inspect sheet; Force Quit and the other actions are in the inspect sheet and the long-press menu. (The mockup `iphone6-cpu-swipe-actions.png` shows the swipe buttons that were tried and dropped.)
- Long-press: menu with all actions. The finger that opened it does not go on scrolling the list underneath.
- Until a process has been opened once, a line above the footer says how the list works (tap, hold, drag sideways, where the columns are).
- Select mode for several rows: tap **Select**, then tap rows; actions apply to all.
- Pull down on the list to refresh now. Pinch or a toggle switches between compact and comfortable rows.
- The table scrolls sideways when the columns don't fit, by dragging a row or the header; the name column stays pinned.
- The app opens maximized.
- Works with a keyboard and mouse too: arrow keys, Delete = Quit, Shift+Delete = Force Quit, Ctrl+F = search.

**Getting there.**

- A bar meter in the shell (CPU or memory, user's choice); tap opens the app. On a phone the current Omarchy A bar layout does not fit: at 375 px the right-hand widgets run into the clock. Which widgets the phone bar keeps is the shell's call, but the meter or the monitor widget must stay.
- A menu entry and launcher icon ("Activity Monitor" as a search keyword).
- Over a full-screen app: no gesture of its own. The existing labwc bottom-edge swipe leaves the app, and the bar meter or launcher opens Omactivity from there.
- Super+Shift+T opens it instead of btop on Omarchy A.

**Under memory pressure.**

- Memory use. The original target, under 40 MB resident, is not reachable with Qt Quick: an empty Qt Quick window is about 56 MB resident, 44 MB of it shared Qt libraries. Measured on the iPad 6 with the list open: 97 MB (CPU tab) to 119 MB (Memory tab) resident, of which about 23 MB are the window's buffers at 1536×2048 and 43–59 MB shared libraries; about 26 MB is the app's own. Accepted for now; no new number set.
- Opened on demand and never kept running, so it holds no memory while closed. No OOM protection: when memory runs out, the kernel kills the largest process, which is the hog, not a 40 MB app.
- Collector work stays under 2 % of one core at the 2 s rate on A10 and on A8. Measured on the iPad 6 (A10): 1.2 % on the CPU tab, 1.6 % on the Memory tab before the wait counters were added. First frame about 0.25 s after launch.

## Releases

**v1 is a complete task manager with all five tabs.**

| Release | Contents |
| --- | --- |
| v1 | CPU, Memory, Energy, Disk and Network tabs with footers; search; all view filters incl. hierarchical, Apps, by service and listening ports; waiting-for-CPU and waiting-for-I/O figures; command and ports columns; sort by any column; column picker; Quit, Force Quit, send signal, pause/resume, renice, quit tree; root actions through `sudo`; protected-process confirmation; inspect sheet incl. open files; touch gestures; bar meter and launcher entry in Omarchy A |
| Later | Sample Process; per-process network bytes; GPU column for boards with a GPU driver; background recorder, a small optional systemd user service that logs CPU and memory (system and per app) so graphs show history from before the app opened, and that the Energy tab's CPU share needs |

**v1 acceptance checks** (on J71b, an iPhone 6, and one armv7 board without the GPU):

- [ ] Search "fire" shows Firefox and its child processes, in tree view under their parent.
- [ ] Sorting by Memory puts the same top process first as a by-hand sum of Pss from every smaps_rollup (within one refresh).
- [ ] Quit on Firefox with an unsaved form shows Firefox's own close prompt.
- [ ] Force Quit on a frozen app (`kill -STOP` it first) removes all its processes within 2 s.
- [ ] Force Quit on a root process asks for the password in the app; a second one within 5 min, with the app still open, does not ask. A wrong password says so and sends nothing.
- [ ] Opening the app shows a filled list straight away (first sample at startup), with load average and memory pressure averages already in the footers.
- [ ] Force Quit on labwc shows the logout warning first.
- [ ] With a memory hog filling RAM until swap is full, the app opens from the bar in under 2 s and kills the hog.
- [ ] Collector under 2 % of one core at the 2 s rate (memory: see Under memory pressure).
- [ ] Theme switch restyles the open app: colours and the control styling from `shell.toml`.
- [ ] On the iPhone 6 the app opens in the narrow layout; inspect opens as a sheet; the long-press menu's Quit and Force Quit work one-handed in portrait.
- [ ] Energy shows the battery and who prevents sleep; Disk shows a copy's bytes climbing; Network shows a browser's connections and the traffic graph moving.
- [ ] A process listening on a port is found by searching the port number.
- [ ] Resizing the window on J71b switches between wide, medium and narrow layouts without losing search, sort or selection.
- [ ] Unit tests run the collector against a recorded `/proc` tree in the repo, no device needed.

## Open questions

- Turn on `kernel.task_delayacct` in Omarchy A, so I/O Wait shows real numbers? Small scheduler cost.
- Read other users' sockets through `sudo`, so root's listening ports can be matched to a process? Today `sudo` is only for kill and renice.
- A new memory target, now that 40 MB resident is known to be out of reach.
