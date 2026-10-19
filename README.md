# Noch

A dynamic notch for Windows: a black pill at the top of the screen that holds
your apps, tucks itself away and drops back down when you need it.

## Using it

- **Auto-hide** - after a moment the notch slides up and leaves a 5% sliver at
  the top edge. Push the pointer to the top of the screen over that sliver and it
  springs back down. Turn this off in Settings to keep it pinned.
- **Palette** - Ctrl + Alt + Space (from anywhere) drops the notch with a search
  field: type to launch any installed app, switch to an open window, do quick
  maths (`1+2*3` is `7`; Enter copies the result), search the web, or run a
  built-in command - lock, sleep, mute the mic, Task Manager, Windows settings,
  Noch's own settings. Arrows move, Enter runs, Esc closes. If another app
  already owns Ctrl + Alt + Space, Noch quietly falls back (Alt + Shift + Space,
  then Ctrl + Alt + P) and Settings says which shortcut is live.
- **Resize** - drag the small handle under the notch, Ctrl + scroll on the
  notch, or use S / M / L / XL and the slider in Settings (70%-160%).
- **Hub** - click the clock to open the hub: **Timer** and
  **Clipboard** pages in one card.
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
  Noch also follows Windows itself: when Spotify, a browser or anything else
  plays, the pill and the card show that song and the transport buttons drive
  it (the header names the app; repeat and volume stay with Noch's own player).
- **Apps** - icons are read from the real installed programs and shown in black
  and white (switchable). On first run Noch looks for Claude, Cursor, VS Code,
  Chrome, Notion, Figma, Spotify, Discord and a few others.
  - Click to open, right-click for Open / Show in folder / Move / Unpin.
  - Drag any app, shortcut or folder onto the notch to pin it, or use
    **Add app** in Settings.
  - A white dot under an icon means the app is running.
- **Games and full-screen apps** - while a full-screen app (a game, a video, a
  presentation) is in front, the notch stays tucked away even when the pointer
  touches the top edge, so it never drops over the action. Ctrl + Alt + Space
  still summons it. Turn **Keep quiet over games** off in Settings to ignore
  this.
- **Settings** - the `...` button at the right end (or Ctrl + , while Noch is
  focused). Esc or clicking elsewhere closes it. **Blur behind (beta)** blurs the desktop behind the
  glass (see below); it is off by default.
- **Launch at startup** - the Settings page can add Noch to the Windows start-up
  list; it starts tucked, showing the sliver until you hover it.

## Glass and motion

The notch, its pills and its flyouts share one glass surface (`qml/Glass.qml`):
a soft shadow, a single tinted body, light across the top and a 1px rim that is
lit from above. There is one theme, **dark stone**: every colour, radius,
spacing step, font size and panel width is a token in `qml/Theme.qml` (a QML
singleton), so no component carries its own hex codes. Panels size themselves
with `Theme.fit(preferred, available)` and keep `Theme.screenMargin` clear of
the screen edges, so they stay wide on big monitors and fit on small or
high-DPI ones.

**Icons** are SVG files in `qml/icons/` (24×24, `currentColor`, 1.6 stroke,
derived from Lucide, ISC licence in `qml/icons/LICENSE-lucide.txt`).
`src/IconSet` loads their path data once and `qml/Glyph.qml` draws them with
`Shape`, so they stay sharp at any scale and take any tint. To add one, put it
in the map in `tools/build_icons.py`, regenerate, and reconfigure CMake (the
folder is globbed).

**Buddy agent safety.** Tool calls pass through a permission policy with
approval cards, guardrails, context compaction, retries and local traces. See
[`docs/agent-safety.md`](docs/agent-safety.md).

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
| `qml/SettingsPanel.qml` | Settings flyout (General / Apps / Buddy tabs; scrolls on short screens) |
| `qml/Flyout.qml` | The spring-animated glass card every flyout and menu sits in |
| `qml/HubPanel.qml` | The hub card: tab strip and the Timer / Clipboard pages |
| `qml/FocusTimer.qml`, `qml/TimerView.qml`, `qml/TimerPill.qml` | Focus timer: logic, panel page and the live pill |
| `qml/ClipboardView.qml` | Clipboard page |
| `qml/MusicPanel.qml`, `qml/NowPlaying.qml` | Music player and the compact control in the notch |
| `qml/PaletteCard.qml` | Command palette card: search, calculator, web, commands |
| `qml/StatusBlock.qml` | Clock block in the notch |
| `qml/DockItem.qml` | One app icon: magnification, launch bounce, running dot |
| `qml/Theme.qml` | The dark stone theme singleton: colours, glass, status, type, spacing, radii, widths |
| `qml/icons/*.svg`, `src/IconSet.*`, `tools/build_icons.py` | SVG icon set, its loader, and the generator |
| `qml/ToolStepRow.qml`, `ToolStepCard.qml`, `ToolTraceBar.qml`, `ToolStatusIcon.qml`, `ToolApprovalCard.qml` | Agent tool-call UI: live rows, trace cards, trace header, approval card |
| `qml/BuddyBubble.qml`, `qml/BuddyPanel.qml`, `qml/Buddy.qml` | Desktop buddy: speech bubble (chat, trace, history), expanded panel, the character |
| `src/BuddyProvider.*` | Buddy's model client and agent tool loop (streaming, handoffs, session memory) |
| `src/StreamAccumulator.*` | SSE parser that rebuilds a streamed reply into the buffered response JSON |
| `src/QmlSingletons.h` | Registers the backend objects as typed QML singletons (`Media`, `BuddyAgent`, `Dock`, …) |
| `src/AgentPolicy.*` | Tool permission policy, secret redaction, injection heuristics (see `docs/agent-safety.md`) |
| `qml/Toggle.qml`, `Segmented.qml`, `SizeSlider.qml`, `MenuRow.qml`, `MoreButton.qml`, `Glyph.qml` | Controls |
| `src/DockController.*` | Pinned apps: discovery, persistence, launching, running detection, start-up entry |
| `src/MusicPlayer.*` | Local playback queue, metadata, cover art |
| `src/AppIconProvider.*` | `image://appicon/...` - extracts real app icons, renders them black and white |
| `src/OverlayController.*` | Limits mouse input to the visible shapes so the rest clicks through |
| `src/RunningApps.*` | Programs with open windows, window previews, bringing a window forward |
| `src/MediaHub.*`, `src/SmtcMedia.*` | Now playing: Noch's player and Windows media sessions, unified |
| `src/AppSearch.*`, `src/Calc.h` | Palette backend: app index, fuzzy matching, inline calculator |
| `src/GlobalHotkey.*` | The palette shortcut (Ctrl + Alt + Space, with fallbacks) |
| `src/AudioDevices.*` | Default sound output and microphone mute |
| `src/Radios.*` | Wi-Fi and Bluetooth radios |
| `src/NightLight.*` | Night light, through Windows' own state blob |
| `src/PowerActions.*`, `src/FullscreenWatcher.*` | Lock / sleep, and the full-screen guard |
| `src/WinRtAbi.h` | Hand-written WinRT ABI the media / radio backends ride on |
| `src/GlassBackdrop.*` | Optional blur behind the visible glass shapes (Windows) |
| `src/ClipboardHistory.*` | In-memory clipboard history (text only, private copies skipped) |
| `src/ShapeRegion.h` | Builds a region from the rounded shapes QML reports |
| `src/Appearance.*` | System appearance hints (reduced motion) |
| `qml/Glass.qml` | The shared glass surface |
| `qml/WindowPreview.qml` | Window hover preview for running apps |

Settings live in the registry under `HKCU\Software\noch\noch`.

## Tests

```sh
cmake -S . -B build-tests -G Ninja -DNOCH_BUILD_TESTS=ON
cmake --build build-tests && ctest --test-dir build-tests
```

`tst_agentpolicy` covers the permission policy, redaction and injection checks.
`tst_iconset` checks that every SVG icon parses into usable path data.
`tst_stream` checks the SSE parser (Anthropic events and OpenAI chunks, any
chunking, cut-off tool input, error events, completeness).
`tst_buddyagent` drives the real agent loop against a fake Anthropic/OpenAI
server: deny, approve, reject, plan/bypass modes, 529 retry, redaction,
compaction, loop guard, cancel during an approval, streamed text and tool
calls, cancel mid-stream, stream error and truncation retries, specialist
handoffs and their toolsets, session memory, and the per-run memory index.
`tst_buddysfx` checks the synthesized buddy cues (no NaN, headroom, silent
edges, no pops), resampling, pacing, the speak-run cap, mute and volume.
