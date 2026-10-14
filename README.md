# Noch

A dynamic notch for Windows: a black pill at the top of the screen that holds
your apps, tucks itself away and drops back down when you need it.

## Using it

- **Auto-hide** - after a moment the notch slides up and leaves a 5% sliver at
  the top edge. Push the pointer to the top of the screen over that sliver and it
  springs back down. Turn this off in Settings to keep it pinned.
- **Resize** - drag the small handle under the notch, Ctrl + scroll on the
  notch, or use S / M / L / XL and the slider in Settings (70%-160%).
- **System** - CPU, memory and battery sit under the clock. Click that block
  for disk, network throughput and uptime. Numbers turn red when they're in
  trouble (CPU or memory over 90%, battery under 15%).
- **Music** - the note in the notch is Noch's own player. Click it to open the
  player, click the mark to play or pause, and scroll on it to change volume.
  Drop songs or a folder onto the notch, or use **Add music**. The queue, the
  last track and the volume are remembered.
- **Apps** - icons are read from the real installed programs and shown in black
  and white (switchable). On first run Noch looks for Claude, Cursor, VS Code,
  Chrome, Notion, Figma, Spotify, Discord and a few others.
  - Click to open, right-click for Open / Show in folder / Move / Unpin.
  - Drag any app, shortcut or folder onto the notch to pin it, or use
    **Add app** in Settings.
  - A white dot under an icon means the app is running.
- **Settings** - the `...` button at the right end (or Ctrl + , while Noch is
  focused). Esc or clicking elsewhere closes it.

## Build

Requires Qt 6.9+ (uses `RectangularShadow`) and CMake 3.21+. `run.bat` builds,
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
| `qml/SettingsPanel.qml` | Settings flyout (General / Apps tabs) |
| `qml/MusicPanel.qml`, `qml/NowPlaying.qml` | Music player and the compact control in the notch |
| `qml/SystemPanel.qml`, `qml/StatusBlock.qml` | System monitor and the clock / stats block |
| `qml/DockItem.qml` | One app icon: magnification, launch bounce, running dot |
| `qml/Theme.qml` | Colour tokens (X / Grok dark palette) |
| `qml/Toggle.qml`, `Segmented.qml`, `SizeSlider.qml`, `MenuRow.qml`, `MoreButton.qml`, `Glyph.qml` | Controls |
| `src/DockController.*` | Pinned apps: discovery, persistence, launching, running detection, start-up entry |
| `src/MusicPlayer.*` | Local playback queue, metadata, cover art |
| `src/SystemMonitor.*` | CPU, memory, battery, disk, network, uptime |
| `src/AppIconProvider.*` | `image://appicon/...` - extracts real app icons, renders them black and white |
| `src/OverlayController.*` | Limits mouse input to the visible shapes so the rest clicks through |

Settings live in the registry under `HKCU\Software\noch\noch`.
