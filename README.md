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

The notch, its pills and its flyouts share one glass surface
(`qml/components/Glass.qml`): a soft shadow, a single tinted body, light across
the top and a 1px rim that is lit from above. There is one theme, **dark
stone**: every colour, radius, spacing step, font size and panel width is a
token in `qml/theme/Theme.qml` (a QML singleton), so no component carries its
own hex codes. Panels size themselves with `Theme.fit(preferred, available)` and
keep `Theme.screenMargin` clear of the screen edges, so they stay wide on big
monitors and fit on small or high-DPI ones.

**Icons** are SVG files in `qml/icons/` (24×24, `currentColor`, 1.6 stroke,
derived from Lucide, ISC licence in `qml/icons/LICENSE-lucide.txt`).
`src/desktop/IconSet` loads their path data once and
`qml/components/Glyph.qml` draws them with `Shape`, so they stay sharp at any
scale and take any tint. To add one, put it in the map in
`tools/build_icons.py`, regenerate, and reconfigure CMake (the folder is
globbed).

**Buddy agent safety.** Tool calls pass through a permission policy with
approval cards, guardrails, context compaction, retries and local traces. See
[`docs/agent-safety.md`](docs/agent-safety.md).

Motion is spring-driven. The notch drops in with a little overshoot and tucks
away firmly, flyouts spring out of the notch (`qml/components/Flyout.qml`), dock
icons swell toward the pointer and settle, and toggles, tabs and pills have
springy overshoots. Only the glass changes shape, so nothing is re-laid-out per
frame.

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

`CMakePresets.json` also provides `release` and `tests` presets. Set `QTDIR` to
the Qt kit (for example `C:\Qt\6.12.0\mingw_64`) before using them:

```powershell
$env:QTDIR = "C:\Qt\6.12.0\mingw_64"
cmake --preset release
cmake --build --preset release
```

## Layout

Code is grouped by responsibility. C++ services live under `src/<domain>/`;
QML types live under `qml/<area>/` and are still exposed at the module root.

```text
src/
  main.cpp            application entry point
  agent/              Buddy model client, tool loop, safety policy, computer use
  app/                QML singleton registration
  chat/               saved chats and session serialization
  common/             small header-only helpers
  desktop/            dock, app search/icons, clipboard, running windows
  media/              music player, Windows media sessions, audio, radios
  platform/           Win32 integration: overlay, blur, hotkeys, power, appearance
  voice/              microphone input, wake word, speech output, buddy SFX
qml/
  Main.qml            window shell: notch, reveal, resize, input mask
  theme/Theme.qml     design tokens (singleton)
  components/         shared glass, controls and small UI primitives
  panels/             notch pages and extracted settings controls
  flyouts/            spring-animated flyout wrappers
  buddy/              buddy character, bubble, panel and result cards
  agent/              agent HUD, cursor, tool trace and approval cards
  icons/              SVG icon set
tests/                 unit tests and QML component tests
cmake/                 shared CMake helpers
docs/                  architecture and safety documentation
tools/                 icon generator and diagnostic scripts
```

QML sources are compiled into the executable's resources under a flat module
namespace: `qml/buddy/BuddyBubble.qml` is registered as `BuddyBubble`, so
imports and type names stay unchanged. `CMakeLists.txt` sets this up with
`QT_RESOURCE_ALIAS`; new QML files must be added to `NOCH_QML_FILES`.

| Area | Key entry points |
| --- | --- |
| Notch shell | `qml/Main.qml`, `qml/components/{Glass,Flyout,DockItem,StatusBlock}.qml` |
| Settings | `qml/panels/SettingsPanel.qml`, `qml/panels/settings/*`, `qml/flyouts/SettingsFlyout.qml` |
| Hub | `qml/panels/{HubPanel,FocusTimer,TimerView,TimerPill,ClipboardView}.qml` |
| Music | `qml/panels/{MusicPanel,NowPlaying}.qml`, `qml/flyouts/MusicFlyout.qml` |
| Palette | `qml/panels/PaletteCard.qml`, `qml/flyouts/PaletteFlyout.qml` |
| Buddy | `qml/buddy/*`, `src/agent/{BuddyProvider,AgentPolicy,ComputerUse}*` |
| Voice | `src/voice/*` |
| Agent trace | `qml/agent/*` |
| Platform | `src/platform/*` |

The largest implementation units are split by responsibility. `AgentPolicy`
is separated into core path/query code, shell rules, saved-rule matching,
evaluation and output safety. `BuddyProvider` is separated into configuration,
prompts, protocol, run loop, storage/approvals, tools and web/catalog helpers.
`ComputerUse` is separated into action parsing, coordinate mapping and the
controller/Windows backend. Shared private headers stay next to them in
`src/agent/`.

See [`docs/architecture.md`](docs/architecture.md) for dependency direction,
the split-unit map and the rules for adding files.

Settings live in the registry under `HKCU\Software\noch\noch`.

## Tests

```sh
cmake -S . -B build-tests -G Ninja -DNOCH_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The tests are registered by `tests/CMakeLists.txt` through the helper in
`cmake/NochTest.cmake`. Shared source lists keep split units (for example
`AgentPolicy` and `ComputerUse`) in sync across every test that needs them.

`tst_agentpolicy` covers the permission policy, redaction and injection checks.
`tst_iconset` checks that every SVG icon parses into usable path data.
`tst_stream` checks the SSE parser (Anthropic events and OpenAI chunks, any
chunking, cut-off tool input, error events, completeness).
`tst_buddyagent` drives the real agent loop against a fake Anthropic/OpenAI
server: deny, approve, reject, plan/bypass modes, 529 retry, redaction,
compaction, loop guard, cancel during an approval, streamed text and tool
calls, cancel mid-stream, stream error and truncation retries, specialist
handoffs and their toolsets, session memory, and the per-run memory index.
`tst_computeruse` covers the computer-use actions, screenshot scaling, held keys
and the simulated backend. `tst_wakeword` (phrase matching, utterance detection,
consent, rate limit, pausing, back-off, and the local detector's whole flow with a
fake microphone and a fake Whisper server), `tst_wakedetector` (the openWakeWord
pipeline against the reference implementation's scores; needs ONNX Runtime, set
`NOCH_ORT_LIBRARY` to its path, skipped otherwise) and `tst_speechout` cover the
wake word and spoken reports. `tst_userquestions`, `tst_runjournal`,
`tst_savedrules`, `tst_projectcontext`, `tst_chatsessions` and `tst_voice` cover
the question, undo, saved-rule, instruction/slash-command, chat-session and voice
modules (the last one with a fake microphone and a fake speech server).
`tst_buddyagent` also drives the official computer toolset and its fallbacks,
batch trimming and cache markers, questions, undo, saved rules and session
summaries. `tst_qml` (needs `qmltestrunner`; skipped otherwise) runs the QML
component tests in `tests/qml` against the real `Theme.qml`.
`tst_buddysfx` checks the synthesized buddy cues (no NaN, headroom, silent
edges, no pops), resampling, pacing, the speak-run cap, mute and volume.
