# Performance and animation research

Research notes for making Noch's Buddy faster and smoother. Dated 2026-10-22. Every claim carries a
confidence tag: **[doc]** from official Qt/Microsoft/Anthropic documentation or Qt source I read,
**[src]** from Qt source code, **[vendor]** from a vendor or blog post (treat as an estimate),
**[mine]** my inference from reading this repository. Nothing here has been profiled on a real run
yet: the first step of the plan is to measure.

A word on "10x". No source supports a blanket 10x. Frame cost is dominated by a few things (how
many pixels repaint per frame, how many windows exist, what runs on the GUI thread), and fixing the
top one or two can cut idle and active GPU/CPU cost by large factors, while the rest give single
digit percentages. The plan below is ordered by expected payoff, and each step has a number to
check against so "better" is measured rather than felt.

## 1. What the repo already does well

Read from `docs/architecture.md` and the code, so it is not re-proposed:

- Idle means no animation: the gaze loop is a `FrameAnimation` only while `springing`, otherwise a
  33 ms `Timer` (`Buddy.qml`). Settings previews move only on hover.
- The buddy has its own window, sized once for the largest bubble, moved natively while dragged
  (`Overlay.setWindowPosition`), with a click-through mask.
- Token streaming is coalesced (`m_streamEmitTimer`, 40 ms, about 25 per second) before it reaches QML.
- Prompt caching is already wired (`cache_control` in `BuddyProviderInternal.h`).
- Reduced motion is honoured (`SPI_GETCLIENTAREAANIMATION` feeds `mo`).
- A GPU ring shader (`shaders/buddyring.frag`) already replaces stacks of `Shape` layers.

So the remaining wins are structural, not tidying.

## 2. Findings

### F1. Two exposed windows switch Qt's animation clock to a plain timer  (highest payoff) [src]

In `QSGThreadedRenderLoop::startOrStopAnimationTimer()` Qt only drives animations from vsync when
`exposedWindows == 1 && unthrottledWindows == 0 && badVSync == 0`. With more than one exposed
window, animations advance from a system timer instead, so they are no longer locked to the display
refresh. The Qt docs say the same: the threaded loop "falls back to timers ... when more than one
QQuickWindow is visible" [doc]. Qt's own comment explains why: `polishAndSync()` may run for several
windows and cannot reliably drive the animation driver.

Noch always has at least two: the screen-sized notch window and `buddyWin` (plus `AgentGlow` and the
control overlay while a computer-use run is active) [mine]. Expected symptoms: judder on 60 Hz,
worse on 120/144 Hz, and `NumberAnimation`/`SpringAnimation` timing that wanders. Animators
(`OpacityAnimator`, `ScaleAnimator`, ...) keep a separate render-thread driver [doc] and are less
affected, but `FrameAnimation`, `NumberAnimation`, `SpringAnimation` and the bubble's width/height
springs are not.

Options, cheapest first:
1. A/B test `QSG_USE_SIMPLE_ANIMATION_DRIVER=1` (Qt 6.5+, elapsed-time driver) [doc]. One env var.
2. Keep only one window exposed while Buddy is active: the notch window is hidden or unmapped when
   nothing in it is showing (it is screen-sized, so this also removes F2 below).
3. Move the bubble and panel's long motions to Animators or to `FrameAnimation.frameTime`-based
   code (Noch's `springStep` already uses `frameTime`, which is the right shape).

### F2. The notch window is as large as the screen and repaints whole  [mine, matches architecture.md]

`Main.qml` sizes the root `Window` to `Screen.width x Screen.height` and it is transparent. Qt Quick
repaints the whole window when anything in it changes, and the compositor then blends a full-screen
alpha surface over the desktop every frame. Qt's D3D11 backend uses a flip-model swapchain and, for
semi-transparent windows, DirectComposition [src: `qrhid3d11.cpp`, commit titles "default swap
effect FLIP_DISCARD", "Switch to DirectComposition for semi-transparent windows"], which is the
efficient path, but the cost still scales with window area. Microsoft's guidance for transparent
GPU-rendered overlays is a DirectComposition swapchain with `WS_EX_NOREDIRECTIONBITMAP` and not
`WS_EX_LAYERED` [vendor: MSDN Magazine, VLC commit], which Qt already approximates; I could not
confirm what Noch's window ends up with, so check it (see plan step 1).

Direction: apply the buddy-window pattern to the notch. Size the window to the notch plus whatever
flyout is open, grow in chunks, shrink when quiet. Idle then costs a small texture, not a screen.

### F3. Animating `width`/`height` re-lays out everything inside  [doc]

The Qt performance page: animating a property re-evaluates every binding that depends on it; avoid
script per frame; clipping and opacity limit batching. `BuddyBubble` morphs `w`/`h` with
`SpringAnimation` (spring 3.4, damping 0.38 on pop-in) and the bubble content binds to those sizes
[mine], so every frame re-lays out text, rows and the approval card. The window mask is also
re-derived while it morphs (`refreshMask`, `maskSettle`).

Direction: lay the content out once at its target size, then animate a clip/reveal rectangle and a
`scale` transform on the surface only (transforms are what Qt batches cheaply: "each Item creates a
transform node"; changing transforms are applied to a retained batch [doc]). Keep text out of the
animated subtree.

### F4. Clip and per-item antialiasing cost batching  [doc]

- "Clipping is a visual effect, not an optimization"; `clip: true` limits batching to that subtree
  [doc]. `BuddyBubble` clips the whole `stage` plus several inner wells [mine].
- Qt 6.10 docs: `antialiasing` on rounded `Rectangle`s costs rendering performance; unset it for
  rectangles in motion and set it when stationary [doc]. Noch sets `antialiasing: true` widely.
- Overlapping translucent items break batches; backgrounds under text force extra draw calls [doc].
  Flat, borderless surfaces (the redesign just done) help here.

Check with `QSG_VISUALIZE=batches`: few colours is good, hatched patches are unmerged batches.

### F5. Glow, blur and shadows: use the cheap primitives  [doc, vendor]

- `MultiEffect` cost scales with pixel count; do not change `blurEnabled`/`blurMax` while animating
  (shader regeneration); prefer `blurMultiplier`; blur a down-sized `ShaderEffectSource` [doc].
- Qt 6.9 added `RectangularShadow`, an SDF shadow for (rounded) rectangles; the Qt blog's own
  numbers are 30 animated shadows at 60 fps on a low-end tablet [vendor: Qt blog].
- `Shape`: `GeometryRenderer` needs MSAA layers for smooth edges; `CurveRenderer` has built-in
  antialiasing without multisampling cost but redraws a bit slower [doc]. Fixed-size ring art
  should be a shader (already started) or a pre-rendered texture, not live Shapes.
- `ShaderEffectSource` renders to an FBO first: "use sparingly" [doc].

Direction: BuddyGlow/BuddyRing (the biggest consumers of `Shape`, gradients and layers) get one
fragment shader each, sized to the ring plus glow margin only (the current `BuddyShaderRing` already
does this at `root.width + 2*margin`). Never bind a gradient stop to an animating colour (already in
architecture.md; the Qt batch rules agree).

### F6. Streaming text  [doc, vendor]

- Plain text with an explicit `textFormat: Text.PlainText` is the cheapest; avoid `RichText`;
  `AutoText` adds a rich-text detection pass [doc].
- `font.preferShaping: false` and `font.kerning: false` "improve performance in most cases" when
  text changes often [doc]. Worth testing on the live well, the reasoning card and the word
  cascade.
- Re-laying out a growing `Text` per update is the same O(n) cost each time. Industry guidance is
  to batch to the frame and parse incrementally, and to treat >50 KB responses specially [vendor].
  Noch already coalesces to about 25 per second; moving that to one flush per rendered frame
  (`frameSwapped`) is the remaining step, and the word cascade (`revealTimer`, 46 ms) should only
  create word items near the viewport. [mine]
- `Text.CurveRendering` (Qt 6.7+) is an option if distance-field text looks soft or uses too much
  GPU memory; QtRendering remains the faster default [doc].

### F7. Frame pacing and what to measure  [doc, vendor]

- Frame budget: 16 ms at 60 fps, 6.9 ms at 144 Hz; keep blocking GUI work to a couple of
  milliseconds [doc]. Qt schedules updates with `requestUpdate()` on a ~5 ms GUI timer [doc].
- "Too fast" detection: if frames return in under half the expected interval the loop flips to a
  timer and logs it [src]. Run with `QSG_INFO=1` and watch for that message, and for the active
  graphics backend.
- Smoothness should be reported as distribution, not average: p50/p90/p95/p99 frame times and
  count of missed vsyncs are the common reporting fields; a janky frame is one that misses its
  intended present time [vendor: Android/Perfetto docs, Chromium scroll-jank doc].
- Tools: `QSG_VISUALIZE=changes|overdraw|batches|clip`, `QSG_RENDERER_DEBUG=render` (batch
  stats), `QSG_RENDER_TIMING=1`, `qt.scenegraph.time.*` logging [doc]; QML Profiler scene-graph
  events, with the caveat that it does not profile C++ [doc]. `changes` flashes every node that
  repaints, which is the fastest way to find something that keeps drawing while idle.

### F8. Power  [doc]

Windows lets a process opt into EcoQoS (`SetProcessInformation` with
`PROCESS_POWER_THROTTLING_EXECUTION_SPEED`), which lowers clocks / uses efficiency cores. Microsoft
says to use it for work that is not part of the foreground experience [doc]. The wake-word, file
watchers and snapshots are candidates (apply on threads with `SetThreadInformation`); the render
and voice threads are not.

### F9. Perceived latency of the agent (the other half of "fast")  [doc, vendor]

- Jakob Nielsen's limits: 0.1 s feels instant, 1 s keeps flow, anything over 1 s needs a visible
  sign of work, 10 s needs progress and a way to interrupt [NN/g, doc]. The minimal thinking dots
  now cover the 1-10 s band; the bubble's tool timeline covers the long band.
- Anthropic's own latency guidance: pick the right model (Haiku 5.5 at low effort for speed
  critical paths), shorter prompt and output, stream [doc].
- Prompt caching: reads cost about 0.1x and skip reprocessing; the tool list must stay byte-stable
  and the breakpoint goes on the last identical block, not on a timestamp or the new user
  message; 5-minute TTL by default, 1-hour available; `max_tokens: 0` can pre-warm [doc]. Noch
  caches already; audit that nothing volatile (clock, memory recall) sits before the breakpoint
  and that tool JSON key order is deterministic, since a silent miss costs more than it saves.
- Voice: vendor budgets cluster around 700-800 ms to first audio, achieved only if every stage
  streams and overlaps; naive 500 ms silence endpointing alone eats most of it, semantic
  endpointing gets 150-250 ms; TTS first chunk 75-200 ms [vendor, estimates]. Verify with your own
  p50/p95; the 337-509 ms STT figure from independent measurement vs 150 ms claimed shows how far
  vendor numbers can drift.

## 3. Animation craft: what "better" means

Rules gathered, with the weaker sources flagged.

1. **Springs for anything the user can interrupt; durations for scripted entrances.** A spring can
   take a new target mid-flight and keep its velocity; a fixed-duration tween restarts from zero
   [vendor: several write-ups of Apple's WWDC18 "Designing Fluid Interfaces"; I could not retrieve
   the session itself, so the exact Apple numbers are unverified].
2. **Parameterise by response and damping ratio, not mass/stiffness.** Damping 1.0 is critically
   damped (fastest, no overshoot); below 1 overshoots [Juckett, doc-quality]. Commonly cited
   defaults: damping 1.0 for ordinary UI, 0.8 only when momentum is carried in, bounce 0.1-0.3
   when used at all [vendor].
3. **Use the closed-form damped-spring update, not Euler.** Juckett's analytic solution takes
   `(ω, ζ, Δt)` and returns exact position and velocity, so it is stable at any frame time and
   frame-rate independent: `x' = x0*posPosCoef + v0*posVelCoef`, `v' = x0*velPosCoef +
   v0*velVelCoef` [source: ryanjuckett.com]. `Buddy.qml`'s `springStep(dt)` clamps `dt` to 0.05
   and integrates; a closed form removes the clamp's slow-motion on a hitch.
4. **Separate "spatial" from "effects" motion.** Material's rule: position/size use springs that
   may overshoot slightly; opacity and colour use damping 1 (never overshoot, a background alpha
   must not go above 100 percent) [vendor: Material/Android docs, token values differ by
   version]. In Noch: spring the bubble's reveal; fade text with an eased `Behavior`.
5. **Durations:** about 225 ms in, about 195 ms out, nothing over about 400 ms for a transition;
   decelerate on entry, accelerate on exit [vendor: Material motion guidance]. The bubble's pop
   (220-420 ms plus delays up to 210 ms) and the 1.1 s settle timer for the mask are at or past the
   edge.
6. **Don't scale from 0.** Start near 0.9 with opacity so it reads as appearing, not popping out of
   nowhere [vendor]. The bubble currently scales from 0.3.
7. **Ambient motion must justify itself.** Decorative motion is for non-functional elements; keep it
   slow and low-contrast, and stop it when nothing is on screen [vendor]. This is what the
   dots-only thinking indicator does, and why the pulses and glow were removed.
8. **Respect reduced motion** at the system level (already done) and give the user an in-app
   "calm" switch (not done: the setting is read once from the OS).

## 4. Plan, in order

Each step has a number to hit. Do 1 first; the rest are ordered by expected payoff.

| # | Step | Expected effect | How to check |
|---|------|-----------------|--------------|
| 1 | Add a debug frame HUD and baseline run: `frameSwapped` frame-time ring buffer (p50/p95/p99, count over 1.5x vsync), `QSG_INFO=1`, `QSG_VISUALIZE=changes`, Task Manager GPU engine for `noch.exe` and `dwm.exe` while idle, thinking, streaming, dragging. Save as `docs/perf-baseline.md`. | Everything else is judged against it | Numbers exist |
| 2 | Idle: with nothing open, `QSG_VISUALIZE=changes` must show no flashing; GPU about 0 percent. Fix any loop found. | Removes constant cost | Idle GPU near 0 percent for `noch.exe` |
| 3 | F1: try `QSG_USE_SIMPLE_ANIMATION_DRIVER=1`; then hide the notch window when it has nothing to show; compare p95 frame time while the bubble animates. | Smooth, vsync-true timing | p95 under 1.2x vsync interval |
| 4 | F2: size the notch window to content (the buddy-window pattern), grow in chunks, shrink after quiet. | Repainted pixels drop by an order of magnitude when idle or small | `changes` overlay area; GPU engine time |
| 5 | F3: stop animating bubble `w`/`h` through layout; fixed-size content plus clip reveal plus surface scale. | Removes per-frame text relayout | Profiler: no layout in animation frames |
| 6 | F4/F5: audit `clip`, `antialiasing`, translucent overlaps with `QSG_VISUALIZE=batches`; shader-ise the remaining glow Shapes; `RectangularShadow` for any shadows. | Fewer batches (guideline: under 10, 3-4 opaque) | Batch count with `QSG_RENDERER_DEBUG=render` |
| 7 | F6: flush streaming once per frame; `preferShaping: false` on live text; virtualise the word cascade. | Smoother long answers | Long-answer p99 frame time |
| 8 | Motion: swap Euler for the closed-form spring, shorten pop to about 225/195 ms, start from 0.9 not 0.3, add an in-app calm switch. | Feel | Side-by-side recording |
| 9 | F9: audit the cache prefix for volatile bytes; measure voice time-to-first-audio p50/p95. | Faster first word | TTFT and first-audio logs |
| 10 | F8: EcoQoS for background threads. | Battery | Power trace |

Risks: step 4 touches the window mask and click-through logic that earlier work stabilised; the
buddy window's note that "resizing or shifting a window shows the old picture at the new place for a
frame or two" applies to the notch too, so grow only in chunks and never track animation. Step 3's
env var is global, so test it against the voice and music pill paths, not just Buddy.

## 5. Gaps in this research

- I did not find an authoritative source for the Windows cost of a full-screen alpha window on
  current DWM, nor measured numbers; the Qt forum thread I found (high GPU use while animating,
  Direct3D 12 lowering app GPU use but doubling `dwm.exe`) is anecdotal and unresolved.
- I could not read the swapchain creation part of `qrhid3d11.cpp` (the file is 250 KB and the fetch
  truncated), so I have not confirmed how Qt 6.12 sets alpha mode for Noch's window.
- Apple's WWDC18 session and the official Material 3 spec were not retrievable; the spring values
  above are secondary sources and are marked as such.
- The 10x figure is a goal, not a finding.

## Sources

- Qt: [Qt Quick performance considerations](https://doc.qt.io/qt-6/qtquick-performance.html),
  [Scene graph and render loops](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html),
  [Default renderer rules](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html),
  [Graphics adaptations](https://doc.qt.io/qt-6/qtquick-visualcanvas-adaptations.html),
  [Text](https://doc.qt.io/qt-6/qml-qtquick-text.html),
  [MultiEffect](https://doc.qt.io/qt-6.9/qml-qtquick-effects-multieffect.html),
  [Shape](https://doc.qt.io/qt/qml-qtquick-shapes-shape.html),
  [Rectangle](https://doc.qt.io/qt-6.10/qml-qtquick-rectangle.html),
  [Threaded animation example](https://doc.qt.io/qt/qtquick-scenegraph-threadedanimation-example.html),
  [Profiling QML applications](https://doc.qt.io/qtcreator/creator-qml-performance-monitor.html),
  [Qt 6.12 what's new](https://doc.qt.io/qt-6/whatsnew612.html),
  [RectangularShadow (Qt blog)](https://www.qt.io/blog/rectangularshadow-fast-rectangle-shadows-in-qt-6.9)
- Qt source: [qsgthreadedrenderloop.cpp](https://code.qt.io/cgit/qt/qtdeclarative.git/plain/src/quick/scenegraph/qsgthreadedrenderloop.cpp),
  [qrhid3d11.cpp](https://code.qt.io/cgit/qt/qtbase.git/plain/src/gui/rhi/qrhid3d11.cpp),
  [QRhiSwapChain](https://doc.qt.io/qt/qrhiswapchain.html)
- Qt forum / lists: [High GPU usage when animating](https://forum.qt.io/topic/121179/high-gpu-usage-when-animating),
  [Adding 2nd window causes animation stuttering](https://forum.qt.io/post/654252),
  [Render loops and timers in QtQuick](https://www.mimec.org/node/94)
- Windows: [SetProcessInformation / EcoQoS](https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation),
  [Introducing EcoQoS](https://devblogs.microsoft.com/sustainable-software/introducing-ecoqos/),
  [MSDN Magazine on DirectComposition](https://msdn.microsoft.com/en-us/magazine/dn745861.aspx)
- Animation: [Juckett, Damped springs](https://www.ryanjuckett.com/damped-springs/),
  [Material motion duration and easing](https://m1.material.io/motion/duration-easing.html),
  [Material motion (Android)](https://github.com/material-components/material-components-android/blob/master/docs/theming/Motion.md),
  [Building fluid interfaces](https://medium.com/@nathangitter/building-fluid-interfaces-ios-swift-9732bb934bf5),
  [Dynamic Island animations dissected](https://uxdesign.cc/apples-dynamic-island-animations-dissected-into-4-principles-b3bfdf546d9a)
- Latency: [NN/g, Response times](https://www.nngroup.com/articles/response-times-3-important-limits/),
  [Anthropic: reducing latency](https://platform.claude.com/docs/en/test-and-evaluate/strengthen-guardrails/reduce-latency),
  [Anthropic: prompt caching](https://platform.claude.com/docs/en/build-with-claude/prompt-caching),
  [Voice latency budget (800 ms)](https://www.twig.so/blog/voice-ai-agents-latency-budget-800ms),
  [Voice turn-taking](https://tianpan.co/blog/2026-05-10-voice-agent-turn-taking-250ms-threshold-reshapes-architecture),
  [Streaming UI, LangChain](https://docs.langchain.com/oss/javascript/langchain/frontend/markdown-messages)
- Measurement: [Chromium scroll jank metric](https://chromium.googlesource.com/chromium/src/+/798e59c5275d4131797e8ea2e96e6c6000256b58/docs/graphics/scrolling/scroll_jank_metric.md),
  [Android JankData](https://developer.android.com/reference/android/view/SurfaceControl.JankData)
