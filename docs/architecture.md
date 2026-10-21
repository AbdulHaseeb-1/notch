# Architecture

Noch is a Qt 6/QML desktop overlay. The source tree is organised by domain so
that a feature's C++ services, QML views and tests sit in predictable places.
The application still builds as one executable with one QML module; the folders
are for people, not for runtime discovery.

## Layers

```text
qml/                         views and reusable QML types
  Main.qml                   window shell
  theme/, components/        shared tokens and controls
  panels/, flyouts/          feature UI
  buddy/, agent/             feature-specific UI
        │
        ▼
src/app/QmlSingletons.h      typed QML singletons (Media, BuddyAgent, Dock, …)
        │
        ▼
src/agent, chat, desktop,    domain services
media, voice
        │
        ▼
src/platform, common         Win32 integration and shared helpers
```

Dependencies point downward. A platform helper does not know about QML; a
domain service does not reach into a panel; QML talks to services through the
registered singletons and their signals.

## C++ domains

| Folder | Responsibility |
| --- | --- |
| `src/agent/` | Buddy provider, agent loop, permission policy, computer use, questions, run journal, saved rules, project context, stream parsing |
| `src/app/` | `QmlSingletons.h`, the typed singleton registration used by the generated QML module |
| `src/chat/` | Saved chats, turn storage, session formatting and resume/branch support |
| `src/common/` | Header-only utilities shared by more than one domain (`ShapeRegion.h`, `ImageAlpha.h`) |
| `src/desktop/` | Dock controller, app discovery/search, icon extraction, clipboard history, running windows |
| `src/media/` | Local music player, Windows media sessions, audio devices, radios |
| `src/platform/` | Win32/Windows integration: overlay input regions, glass backdrop, global hotkeys, fullscreen watcher, night light, power actions, appearance |
| `src/voice/` | Microphone capture, voice input, wake word, speech output, synthesised buddy cues, and Deepgram (live voice-agent conversation, speech to text, voice: see [deepgram.md](deepgram.md)) |

`src/main.cpp` stays at the root because it is the process entry point.

### Split implementation units

Large classes are split by responsibility while keeping one public header:

- `AgentPolicy`
  - `AgentPolicy.cpp` — path normalisation, mode conversion, URL and private-path queries
  - `AgentPolicyShell.cpp` — shell command classification and dangerous-command rules
  - `AgentPolicyRules.cpp` — saved-rule matching and workspace containment
  - `AgentPolicyEvaluate.cpp` — the decision pipeline
  - `AgentPolicySafety.cpp` — secret redaction, prompt-injection detection, display sanitising
  - `AgentPolicyInternal.h` — private helpers shared by the units
- `BuddyProvider`
  - `BuddyProvider.cpp` — construction and public configuration API
  - `BuddyProviderConfig.cpp` — provider settings and endpoint selection
  - `BuddyProviderPrompts.cpp` — system prompt and tool catalogue
  - `BuddyProviderProtocol.cpp` — request/response JSON extraction
  - `BuddyProviderRun.cpp` — request dispatch, streaming, retries, gate and context compaction
  - `BuddyProviderStorage.cpp` — summaries, traces, approvals and questions
  - `BuddyProviderTools.cpp` — local tool implementations
  - `BuddyProviderWeb.cpp` — web search/fetch, tool search and advisor helpers
  - `BuddyProviderInternal.h` — private shared types and constants
- `ComputerUse`
  - `ComputerUse.cpp` — the controller and screenshot/state handling
  - `ComputerUseActions.cpp` — action parsing and validation
  - `ComputerUseCoordinates.cpp` — model-to-screen coordinate mapping
  - `ComputerUseWin.cpp` — the Windows `SendInput` backend

The `noch::buddyprovider` and `noch::agentpolicy` namespaces are implementation
details. Public headers remain the contract used by the rest of the app and the
tests.

## QML layout

| Folder | Responsibility |
| --- | --- |
| `qml/Main.qml` | Window shell: notch, reveal/auto-hide, resize, tooltip, context menu and click-through mask |
| `qml/theme/` | `Theme.qml`, the dark-stone singleton with all design tokens |
| `qml/components/` | Shared primitives: glass, flyout, glyph, dock item, toggles, sliders, previews |
| `qml/panels/` | Notch pages: settings, hub, timer, clipboard, music, palette; `panels/settings/` holds extracted settings controls |
| `qml/flyouts/` | Spring-animated wrappers that host panels under the notch |
| `qml/buddy/` | Buddy character, speech bubble, expanded panel, result cards and bubble controls |
| `qml/agent/` | Agent HUD, cursor/glow, tool trace, approval card and tool status |
| `qml/icons/` | SVG icon assets loaded through `IconSet` |

### Flat resource namespace

QML files are added to `NOCH_QML_FILES` in the root `CMakeLists.txt` and
compiled into the executable. For every entry, CMake sets
`QT_RESOURCE_ALIAS` to the file's basename:

```cmake
foreach(qml_file IN LISTS NOCH_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
```

As a result, `qml/buddy/BuddyBubble.qml` is available as `BuddyBubble` at the
module root, exactly as it was when the tree was flat. Keep basenames unique and
do not add folder-based imports for internal QML files.

`qml/theme/Theme.qml` is also marked as a QML singleton. It is the only source
of colours, type sizes, spacing, radii and panel widths; components should use
its tokens rather than embedding hex values or magic numbers.

Some feature files (`Buddy.qml`, `BuddyBubble.qml`, `BuddyPanel.qml`,
`SettingsPanel.qml`) stay large on purpose: their geometry, state machine and
animation timing share one coordinate system, and splitting them further would
mean passing most of their state across a component boundary. The reusable
pieces they expose were extracted instead:
`qml/buddy/components/*`, `qml/panels/settings/*` and `qml/buddy/BuddyNeonRing.qml`. Prefer adding another focused component there
over growing those files with more inline controls.

## Tests

Unit tests live in `tests/` and are registered in `tests/CMakeLists.txt` with
`noch_add_unit_test()` from `cmake/NochTest.cmake`. The helper:

- resolves source paths relative to the repository root;
- links `Qt6::Test` and the `noch_includes` interface target;
- registers the test with CTest and sets `NOCH_NO_AUDIO=1` (plus the offscreen
  platform on non-Windows hosts).

Shared source lists (`NOCH_AGENT_POLICY_SOURCES`,
`NOCH_COMPUTER_USE_SOURCES`) keep split units in sync across test targets. When
a split unit gains a new translation unit, add it to the shared list rather than
to individual tests.

QML component tests live in `tests/qml/`. They use `qmltestrunner` when it is
available and are skipped otherwise; `tests/qml/components.txt` uses paths
relative to `qml/` (for example `buddy/BuddyQuestionCard`).

Run the suite with:

```sh
cmake -S . -B build-tests -G Ninja -DNOCH_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## Adding code

1. Put C++ code in the domain folder that owns the behaviour, not at the root.
2. Add every new source and header to `NOCH_SOURCES`; the list is explicit so
   stale files do not compile silently.
3. Keep private helpers in a `*Internal.h` header next to the implementation
   and out of the public header.
4. Put QML in the closest feature folder under `qml/`, add it to
   `NOCH_QML_FILES`, and keep the basename unique because of the flat alias.
5. Add tests under `tests/` and register them through `noch_add_unit_test()`.
   Extend the shared split-unit source lists when needed.
6. Prefer `Theme` tokens in QML and the existing domain services over new
   ad-hoc globals.
7. Build and run the tests before committing; QML is compiled into the
   executable, so a successful build catches syntax errors but a quick app
   launch is still the right smoke test for runtime type resolution.

## Keeping it smooth

The notch window is as large as the screen, so Qt Quick repaints all of it
whenever anything inside it changes. The rule that follows: nothing may keep
animating, or keep writing a property that something paints, while it is idle.

- A long-lived state (a resting mood, music playing, a running timer) must not
  hold a per-frame animation open. Drive it with a slow `Timer` and let the
  result step, or let the value follow its source directly. The Buddy frame loop
  runs only while `springing` is set, the focus timer publishes once a second,
  and the music pill's level bars step about nine times a second.
- Never bind a gradient stop colour to a value that animates: Qt builds and
  caches a new gradient texture for every distinct colour. Keep the stops fixed
  and animate `opacity` instead (see `qml/agent/AgentGlow.qml`).
- Pure transforms and fades (`rotation`, `scale`, `opacity`) belong in
  `RotationAnimator`, `ScaleAnimator` and `OpacityAnimator`, which run on the
  render thread.
- A C++ property that changes several times a second needs its own `NOTIFY`
  signal. Sharing a catch-all signal re-evaluates every binding on that object
  (`MediaHub::position` has `positionChanged`, separate from `changed`).
- Run no more than one background snapshot at a time behind a timer.
- Qt Quick's threaded render loop draws a frame on **every vsync while any
  animation is running** (a `FrameAnimation`, a `NumberAnimation`, an animator),
  even when nothing changed. So a looping animation on an item that is hidden or off
  screen still costs a full-window frame sixty times a second. Bind `running` to
  what is really on screen (an item's `visible` stays true inside a hidden window;
  see `AgentHud.qml`), and drive quiet motion from a `Timer`, which only costs a
  frame when a property actually changed (see `calmSpring` in `Buddy.qml`).
- Aiming the eyes is cheap only if it stays quiet: `updateGaze()` ignores changes
  under half a degree, and the cursor poll slows to 10 Hz once the cursor is still.
