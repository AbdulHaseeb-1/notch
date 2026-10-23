# Noch

A music pill for the top edge of your Windows desktop, with a desktop buddy you
can talk to. The pill sits in the middle of the top edge on a clear desktop,
tucks itself away while an app window sits in its space, and drops back down when
you push the pointer to the top of the screen.

## Using it

- **Auto-hide** - the pill stays out while the top of the desktop is clear. When
  an app window (not a tool window or an overlay) covers its space, or a
  full-screen app is in front, it slides up and leaves a small dash at the top
  edge. Push the pointer onto that dash and it springs back down. Turn auto-hide
  off in Settings to keep it out all the time.
- **Palette** - Ctrl + Alt + Space (from anywhere) drops the notch with a search
  field: type to launch any installed app, switch to an open window, do quick
  maths (`1+2*3` is `7`; Enter copies the result), search the web, or run a
  built-in command - lock, sleep, mute the mic, Task Manager, Windows settings,
  Noch's own settings. Arrows move, Enter runs, Esc closes. If another app
  already owns Ctrl + Alt + Space, Noch quietly falls back (Alt + Shift + Space,
  then Ctrl + Alt + P) and Settings says which shortcut is live.
- **Resize** - drag the small handle under the notch, Ctrl + scroll on the
  notch, or use S / M / L / XL and the slider in Settings (70%-160%).
- **Hub** - the **Timer** and **Clipboard** pages in one card. Open it from the
  palette (type "focus timer" or "clipboard"), by clicking the timer pill while a
  session runs, or by asking Buddy.
  - *Timer*: a focus timer (5 / 15 / 25 / 45 / 60 min, `+` adds five). It keeps
    running with the panel closed; a live pill beside the music pill shows a
    progress ring and the time left, and pulses with a toast when the session
    ends. Click the pill to reopen the timer.
  - *Clipboard*: history of what you copied. Click an entry to copy it again,
    pin the ones you want to keep, `Clear` drops the rest. It is **off until you
    turn it on**, text only, kept in memory (nothing is written to disk), gone
    when Noch quits, and copies that password managers mark as private are
    skipped.
- **Music** - the pill is Noch's own player. Click it to open the transport bar
  (previous, play or pause, next, repeat, volume); the volume icon opens a
  slider. While music plays the pill shrinks to a small gradient level
  indicator. Scroll on the pill to change volume; right-click opens Settings.
  There is no separate player panel: drop songs or a folder onto the pill, or
  just ask Buddy ("play some music", "skip this", "turn it down", "play Blue
  Train"). Buddy's `music` tool drives whatever the pill shows: Noch's own queue,
  or the app Windows reports as playing (Spotify, a browser ...). It can queue
  files and folders from your Music folder on its own and asks before queueing
  anything outside it. The queue, the last track and the volume are remembered.
  A switch in Settings > Agent & tools turns the tool off.
- **Skills** - teach Buddy better ways to work with Agent Skills, the format of
  Claude Code and the Claude Agent SDK: a folder with a `SKILL.md` (a name, a
  description, then instructions). Buddy sees only the names and descriptions and
  loads a skill's instructions when a task matches, so many skills cost little.
  Noch ships six (desktop control, files and shell, research, clarifying
  questions, memory, and a skill-creator that helps you write your own); add yours
  in the skills folder (Settings > Agent & tools > Skills > *Open skills folder*)
  or in `.noch/skills` / `.claude/skills` of a workspace. `/skills` lists them,
  `/<skill-name>` runs one. A skill is advice only: it never grants a tool or a
  permission. See [docs/skills.md](docs/skills.md).
- **Games and full-screen apps** - while a full-screen app (a game, a video, a
  presentation) is in front, the pill stays tucked away even when the pointer
  touches the top edge, so it never drops over the action. Ctrl + Alt + Space
  still summons it. Turn **Keep quiet over games** off in Settings to ignore
  this.
- **Settings** - the `...` button at the right end (or Ctrl + , while Noch is
  focused). Esc or clicking elsewhere closes it.
  Settings has a sidebar of categories (General, Buddy, Look & glow, Voice, Deepgram,
  AI model, Agent & tools, Safety) with a search box that narrows them by what
  they contain (Ctrl + F), and Ctrl + Tab or Ctrl + 1..8 to move between them.
- **Look & glow** - Buddy's ring comes in eight styles (Neon, Aurora, Comet, Orbit,
  Beads, Pulse, Prism, Halo) with thickness, speed and colour, plus a coloured
  glow or a pool of light under it. It shows when it is listening (a ribbon of
  waves that follows your voice, rings of sound drawing in) and when it speaks (a
  crown of bars and rings going out). The edge glow shown while Buddy controls the
  computer has five styles. Everything is in Settings, Look & glow.
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

**Long-term memory.** Buddy remembers lasting facts about you across conversations
with [Mem0](https://mem0.ai): the default is a private Mem0 server in Docker on
your own PC (`docker/mem0`, started with `setup.ps1`); Mem0 Cloud or your own
server can be chosen in Settings, Agent & tools. See
[`docker/mem0/README.md`](docker/mem0/README.md).

**Connected apps.** Settings, Connectors signs Buddy in to your accounts once: Gmail
(read mail, draft and send, with Google OAuth and your own client ID) and any app with an
MCP server (GitHub, Notion, Slack ...). Then ask "read my unread emails" or "draft a reply
to Anna". Reads run on their own; anything that sends or changes something asks you every
time. Tokens are kept encrypted (Windows DPAPI). Setup steps for the Google client:
[`docs/connectors.md`](docs/connectors.md).

**Buddy agent safety.** Tool calls pass through a permission policy with
approval cards, guardrails, context compaction, retries and local traces. See
[`docs/agent-safety.md`](docs/agent-safety.md).

Motion is spring-driven. The notch drops in with a little overshoot and tucks
away firmly, flyouts spring out of the notch (`qml/components/Flyout.qml`), dock
icons swell toward the pointer and settle, and toggles, tabs and pills have
springy overshoots. Only the glass changes shape, so nothing is re-laid-out per
frame.

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
  media/              music player, Windows media sessions, audio
  platform/           Win32 integration: overlay, hotkeys, power, appearance
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
| Notch shell | `qml/Main.qml`, `qml/components/{Glass,Flyout}.qml`, `src/platform/FullscreenWatcher.*` (full-screen and "an app is in the pill's space") |
| Settings | `qml/panels/SettingsPanel.qml` (sidebar shell), `qml/panels/settings/*` (controls), `qml/panels/settings/pages/*` (one file per page), `qml/flyouts/SettingsFlyout.qml` |
| Buddy ring, glow, listening and speaking | `qml/buddy/fx/*` |
| Hub | `qml/panels/{HubPanel,FocusTimer,TimerView,TimerPill,ClipboardView}.qml` |
| Music | `qml/panels/NowPlaying.qml` (the pill), `src/media/{MusicPlayer,MediaHub,MusicAgentBackend}.*`, `src/agent/MusicBackend.h` (Buddy's `music` tool) |
| Palette | `qml/panels/PaletteCard.qml`, `qml/flyouts/PaletteFlyout.qml` |
| Buddy | `qml/buddy/*`, `src/agent/{BuddyProvider,AgentPolicy,ComputerUse}*` |
| Connected apps (Gmail, MCP) | `src/agent/{Connector,ConnectorRegistry,GmailConnector,McpConnector,OAuthClient,SecretStore}.*`, `qml/panels/settings/pages/ConnectorsPage.qml`, [docs/connectors.md](docs/connectors.md) |
| Voice (incl. Deepgram live conversation, see [docs/deepgram.md](docs/deepgram.md)) | `src/voice/*` |
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
`tst_secretstore` (DPAPI-encrypted secrets), `tst_oauth` (PKCE, the loopback redirect, code
exchange and refresh against a fake sign-in service), `tst_gmail` (the Gmail connector against a fake
Google: sign-in, search, read, drafts, send, token refresh, revocation) and `tst_mcp` (MCP over HTTP and
stdio) cover the connectors; their rules in the gate are in `tst_agentpolicy` and `tst_buddyagent`.
`tst_iconset` checks that every SVG icon parses into usable path data.
`tst_stream` checks the SSE parser (Anthropic events and OpenAI chunks, any
chunking, cut-off tool input, error events, completeness).
`tst_buddyagent` drives the real agent loop against a fake Anthropic/OpenAI
server: deny, approve, reject, plan/bypass modes, 529 retry, redaction,
compaction, loop guard, cancel during an approval, streamed text and tool
calls, cancel mid-stream, stream error and truncation retries, session memory, and the per-run memory index.
`tst_computeruse` covers the computer-use actions, screenshot scaling, held keys
and the simulated backend. `tst_wakeword` (phrase matching, utterance detection,
pausing, failure handling, and the local detector's whole flow with a fake
microphone and a fake speech engine), `tst_wakedetector` (the openWakeWord
pipeline against the reference implementation's scores; needs ONNX Runtime, set
`NOCH_ORT_LIBRARY` to its path, skipped otherwise) and `tst_moonshine` (the real Moonshine Tiny model on recorded speech; needs ONNX Runtime
and the model files, set `NOCH_ORT_LIBRARY` and `NOCH_MOONSHINE_DIR`, skipped otherwise)
and `tst_speechout` (system, Google AI Studio Gemini TTS and Kokoro voices against a fake server) cover the
wake word, transcription and spoken reports. `tst_userquestions`, `tst_runjournal`,
`tst_savedrules`, `tst_projectcontext`, `tst_skills`, `tst_chatsessions` and `tst_voice` cover
the question, undo, saved-rule, instruction/slash-command, agent-skill, chat-session and voice
modules (the last one with a fake microphone and a fake speech engine).
`tst_buddyagent` also drives the official computer toolset and its fallbacks,
batch trimming and cache markers, questions, undo, saved rules and session
summaries. `tst_qml` (needs `qmltestrunner`; skipped otherwise) runs the QML
component tests in `tests/qml` against the real `Theme.qml`.
`tst_buddysfx` checks the synthesized buddy cues (no NaN, headroom, silent
edges, no pops), resampling, pacing, the speak-run cap, mute and volume.
