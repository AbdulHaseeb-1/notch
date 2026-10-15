# Noch

A dynamic notch for Windows: a black pill at the top of the screen that holds
your apps, tucks itself away and drops back down when you need it.

## Using it

- **Auto-hide** - after a moment the notch slides up and leaves a 5% sliver at
  the top edge. Push the pointer to the top of the screen over that sliver and it
  springs back down. Turn this off in Settings to keep it pinned.
- **Resize** - drag the small handle under the notch, Ctrl + scroll on the
  notch, or use S / M / L / XL and the slider in Settings (70%-160%).
- **Hub** - click the clock to open the hub: **System**, **Timer** and
  **Clipboard** pages in one card.
  - *System*: CPU, memory, battery, disk, network throughput and uptime. Turn on
    System stats in Settings for CPU, memory and battery beside the clock.
    Numbers turn red when they're in trouble (CPU or memory over 90%, battery
    under 15%).
  - *Timer*: a focus timer (5 / 15 / 25 / 45 / 60 min, `+` adds five). It keeps
    running with the panel closed; a live pill beside the music pill shows a
    progress ring and the time left, and pulses with a toast when the session
    ends. Click the pill to reopen the timer.
  - *Clipboard*: history of what you copied. Click an entry to copy it again,
    pin the ones you want to keep, `Clear` drops the rest. It is **off until you
    turn it on**, text only, kept in memory (nothing is written to disk), gone
    when Noch quits, and copies that password managers mark as private are
    skipped.
- **Music** - the note beside the notch is Noch's own player. Click it to open
  the transport bar (previous, play or pause, next, repeat, volume); the volume
  icon opens a slider, and the note at the start of the bar opens the full
  player. While music plays the pill shrinks to a small gradient level
  indicator. Scroll on the pill to change volume.
  Drop songs or a folder onto the notch, or use **Add music**. The queue, the
  last track and the volume are remembered.
- **Apps** - icons are read from the real installed programs and shown in black
  and white (switchable). On first run Noch looks for Claude, Cursor, VS Code,
  Chrome, Notion, Figma, Spotify, Discord and a few others.
  - Click to open, right-click for Open / Show in folder / Move / Unpin.
  - Drag any app, shortcut or folder onto the notch to pin it, or use
    **Add app** in Settings.
  - A white dot under an icon means the app is running.
- **Open apps** - programs with a window open sit in their own pill to the left
  of the notch, like the music pill on the right. Hover one to preview its
  windows, click a window to bring it forward. Pinned apps stay in the notch.
- **Settings** - the `...` button at the right end (or Ctrl + , while Noch is
  focused). Esc or clicking elsewhere closes it. **Light theme** switches the
  whole notch to pale glass. **Blur behind (beta)** blurs the desktop behind the
  glass (see below); it is off by default.

## Glass and motion

The notch, its pills and its flyouts share one glass surface (`qml/Glass.qml`):
a soft shadow, a single tinted body, light across the top and a 1px rim that is
lit from above. Controls on the glass use translucent white (dark) or black
(light) fills so they look right on either theme.

Motion is spring-driven. The notch drops in with a little overshoot and tucks
away firmly, flyouts spring out of the notch (`qml/Flyout.qml`), dock icons
swell toward the pointer and settle, and toggles, tabs and pills have springy
overshoots. Only the glass changes shape, so nothing is re-laid-out per frame.

**About the blur.** Noch's window is a transparent overlay as wide as the
screen. The system acrylic material blurs the *whole* window, so earlier builds
fogged most of the desktop. Blur behind now asks the compositor
(`DwmEnableBlurBehindWindow`) to blur only the rounded shapes of the notch and
its open flyouts. It is a beta: with it off the glass is simply denser, so it
reads on any wallpaper.

## Build

Requires Qt 6.9+ (uses `RectangularShadow` from `QtQuick.Effects`) and CMake 3.21+. `run.bat` builds,
deploys the Qt runtime next to `noch.exe` once, and launches it:

```bat
run.bat
```

Or by hand, from a shell with the Qt tools on `PATH`:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\noch.exe
```

## Layout

| File | What it does |
| --- | --- |
| `qml/Main.qml` | Window, notch, reveal/auto-hide, resize, tooltip, context menu, click-through mask |
| `qml/SettingsPanel.qml` | Settings flyout (General / Apps tabs; scrolls on short screens) |
| `qml/Flyout.qml` | The spring-animated glass card every flyout and menu sits in |
| `qml/HubPanel.qml` | The hub card: tab strip and the System / Timer / Clipboard pages |
| `qml/FocusTimer.qml`, `qml/TimerView.qml`, `qml/TimerPill.qml` | Focus timer: logic, panel page and the live pill |
| `qml/ClipboardView.qml` | Clipboard page |
| `qml/MusicPanel.qml`, `qml/NowPlaying.qml` | Music player and the compact control in the notch |
| `qml/SystemPanel.qml`, `qml/StatusBlock.qml` | System page of the hub and the clock / stats block |
| `qml/DockItem.qml` | One app icon: magnification, launch bounce, running dot |
| `qml/Theme.qml` | Colour tokens (dark and light) |
| `qml/Toggle.qml`, `Segmented.qml`, `SizeSlider.qml`, `MenuRow.qml`, `MoreButton.qml`, `Glyph.qml` | Controls |
| `src/DockController.*` | Pinned apps: discovery, persistence, launching, running detection, start-up entry |
| `src/MusicPlayer.*` | Local playback queue, metadata, cover art |
| `src/SystemMonitor.*` | CPU, memory, battery, disk, network, uptime |
| `src/AppIconProvider.*` | `image://appicon/...` - extracts real app icons, renders them black and white |
| `src/OverlayController.*` | Limits mouse input to the visible shapes so the rest clicks through |
| `src/RunningApps.*` | Programs with open windows, window previews, bringing a window forward |
| `src/GlassBackdrop.*` | Optional blur behind the visible glass shapes (Windows) |
| `src/ClipboardHistory.*` | In-memory clipboard history (text only, private copies skipped) |
| `src/ShapeRegion.h` | Builds a region from the rounded shapes QML reports |
| `src/Appearance.*` | The dark / light theme setting |
| `qml/Glass.qml` | The shared glass surface |
| `qml/OpenApps.qml`, `qml/WindowPreview.qml` | Open-apps pill and its hover preview |

Settings live in the registry under `HKCU\Software\noch\noch`.
