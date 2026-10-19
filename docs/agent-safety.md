# Buddy agent safety layer

Noch's Buddy can run a multi-turn tool loop (shell, file editor, computer use,
web, memory, …). Every tool call the model makes passes through a small safety
layer before anything runs. Code: `src/agent/AgentPolicy.{h,cpp}` with the
implementation split across `AgentPolicyShell.cpp`, `AgentPolicyRules.cpp`,
`AgentPolicyEvaluate.cpp` and `AgentPolicySafety.cpp` (pure, unit-tested in
`tests/tst_agentpolicy.cpp`). The gate lives in the `src/agent/BuddyProvider*`
units: `gateAndExecute`, `guardToolOutput`, `compactContext` and
`scheduleRetry` in `BuddyProviderRun.cpp`, and `resolveApproval` and
`finishTrace` in `BuddyProviderStorage.cpp`.

## Evaluation order

Modelled on the Claude Agent SDK permission flow
(hooks → deny rules → ask rules → permission mode → allow rules → canUseTool):

1. **Loop guard.** If the same tool runs with identical input more than twice
   in one task, the call is skipped and the model is told to change approach.
2. **Input guardrail.** Arguments that contain the user's API key are blocked.
3. **Hard deny rules.** These apply in every mode, including *Trusted*:
   destructive shell patterns (`rm -rf /`, `rm --recursive --force /`,
   `find / -delete`, `format`, `diskpart`, `mkfs`, fork bombs, `curl … | sh`,
   registry or boot edits, disabling Defender, …), writes to OS folders,
   credential stores (`~/.ssh`, `~/.aws`, …), Noch's own settings/data, and
   `.git/hooks`. Symlinks are followed before the path check.
4. **Read-only fast path.** Read-only shell commands (`ls`, `dir`, `cat`,
   `git status/log/diff`, `Get-ChildItem`, … with no redirects, substitution
   or `tee`), editor `view`, observing computer actions (screenshot, zoom,
   cursor position, wait), memory, search, todo and advisor run without asking.
   Opaque commands (`bash -c`, `python -c`, encoded PowerShell, a variable as
   the program) always ask, even in Trusted mode, because the gate cannot see
   what they will run.
5. **Permission mode** (Settings › Buddy › Safety & permissions):

   | Mode | Shell with side effects | File edits | Computer control |
   |---|---|---|---|
   | Ask (`default`) | ask | ask | ask |
   | Auto-edit (`acceptEdits`) | ask | allowed inside the workspace folder | ask |
   | Plan (`plan`) | denied | denied | denied |
   | Trusted (`bypass`) | allowed | allowed | allowed |

   Hard-deny rules still apply in Trusted mode. Editing secrets files (`.env`,
   `*.pem`, `id_rsa`, …) always asks. Only http(s) URLs can be fetched;
   private, loopback and metadata hosts always ask (even in Trusted).
   `file://` and other schemes are denied. After a prompt-injection flag in
   the same run, Trusted mode and session grants stop auto-approving side
   effects until the next message.
6. **Session grants.** *Always allow* on an approval card trusts that tool for
   the rest of the app session. Changing the mode clears every grant.
7. **Unknown tools are denied.**

Commands the user types directly (for example `run: …`) count as already
approved, so they skip *ask*. Hard-deny rules still apply to them.

## Human in the loop

An *ask* verdict pauses the run. The call shows as **waiting** in the trace,
and an approval card offers **Deny**, **Always allow** and **Allow once**
(Enter allows once, Esc denies). An approval left unanswered for 120 s is
declined. Like a hook timeout, it is treated as a block.

Declined and denied calls are not errors. The model receives a `tool_result`
that explains why the call did not run and tells it not to retry the same
action. This follows the OpenAI Agents SDK rejection message and the Claude
Agent SDK deny reason.

## Output guardrails

- **Secret redaction**, on by default: API keys, bearer tokens, private-key
  blocks, AWS keys, GitHub/Slack/OpenAI/Anthropic tokens and the configured
  API key are masked in tool output, the final answer and saved traces.
- **Untrusted content fencing:** `web_fetch` and `web_search` results are
  wrapped in `<untrusted_content>` after invisible characters are stripped
  and any attempt to close the fence early is neutralised. The system prompt
  tells the model that everything inside is data, never instructions.
- **Prompt-injection flag:** text that targets agents ("ignore previous
  instructions", "you are now", "exfiltrate", …), including smuggled
  zero-width forms, gets a guardrail banner, taints the run, and the step
  shows *Possible prompt injection* in the trace. The approval card shows
  bidi/zero-width characters as visible markers so nothing is hidden.

## Context and cost

- **Context editing, in batches:** the newest 4 tool results always stay
  verbatim. Older bulky results (over 600 characters) and older screenshots are
  replaced with a short placeholder, but only once 6 results or 4 screenshots
  have piled up, and then all at once. Changing an earlier message invalidates
  the prompt cache from that point on, so doing it every turn (as before) paid
  for the whole conversation again each time. In a batch, the cached prefix
  holds for several turns in a row. This follows Anthropic's "clear tool uses"
  strategy.
- **Prompt caching (Anthropic):** the system prompt and the tool block carry
  `cache_control: {type: "ephemeral"}`, and so does the end of the conversation
  (the newest message, and the previous request's end, so the next turn reads
  what this one wrote; 4 breakpoints at most). Cache hits are counted from
  `cache_read_input_tokens` (Anthropic) or `prompt_tokens_details.cached_tokens`
  (OpenAI) and reported in the response meta.
- **Max steps:** a run stops after *Steps per task* tool rounds (Settings,
  Buddy; 40 by default, 4 to 100) and says so (as with `max_turns` in the
  OpenAI Agents SDK). Repeating the same call back to back is stopped sooner by
  the loop guard; for computer use only consecutive repeats count, plus a cap of
  8 identical input actions, so taking several screenshots in a row is fine.

## Streaming

With *Stream replies* on (Settings, Buddy, Agent behaviour), Anthropic and
OpenAI-compatible requests set `stream: true`. OpenAI also gets
`stream_options.include_usage`. Ollama stays buffered. `StreamAccumulator`
parses the SSE events and rebuilds the same JSON a buffered reply would have
had, so the tool loop, guardrails and traces are unchanged. The live text is
redacted before it reaches the UI and is coalesced to one update every 40 ms.
Several cases fall back or fail safe:
- A non-2xx reply, or a server that ignores `stream`, falls back to the
  buffered path.
- A stream `error` event is retried like a 5xx/529.
- A stream that closes before the model finished (no `message_stop` or
  `[DONE]`, and no stop reason) is retried and never used as the answer.
- Cancel aborts the request and clears the live text.

## Handoffs and specialists

Following the OpenAI Agents SDK, handoffs are tools: Buddy is offered
`transfer_to_research` and `transfer_to_code`.

| Agent | Tools |
| --- | --- |
| Buddy | everything the user enabled |
| Research specialist | `web_search`, `web_fetch`, `memory`, `todo_write`, `advisor` |
| Code specialist | `glob_grep`, `str_replace_editor`, `bash`, `todo_write`, `memory` |

A specialist can only hand back with `transfer_to_buddy`. Its toolset is
enforced twice: in the request, where other tools are not offered, and in the
gate, where an out-of-set call is denied with rule `agent:toolset`. The
permission policy still applies to everything a specialist runs. Each handoff
is a span in the trace. Every run, and every cancel, starts back at Buddy. The
whole feature can be turned off with *Specialist handoffs*.

## Chats and session memory

Every finished question and answer is saved to a chat on disk
(`<AppData>/sessions/`, up to 100 chats, 400 turns each), so a chat can be
picked up after Noch was closed: `/resume` lists them, `/resume 2` opens one,
`/branch` (or `/branch 2`) starts a new chat from a copy of it and leaves the
original as it was.

What goes along with the next question is the chat's **summary** (when it has
one) followed by the newest turns word for word. Once a chat passes 10 turns
or about 12 000 characters, a separate model call with no tools folds the older
turns into a rolling summary (also on demand with `/compact`). That call is
redacted like any other, queued behind a running request, never touches it, and
a failure is not retried until two more turns exist. The summary stays within
4 000 characters; the 4 newest turns are never replaced by it.

When the memory tool is on, the system prompt lists up to 16 saved `/memories`
files (the Claude Agent SDK memory-tool pattern). This list is taken once at the
start of a run, so the system prompt stays byte-identical across turns and the
prompt cache survives memory writes.

## Questions

Buddy has an `ask_user_question` tool for the moments it would otherwise
guess: 1 to 4 questions with 2 to 4 options each, shown as chips to tap, plus
"Other" to type an answer. It needs no approval (it only talks to you), but
everything is treated as untrusted model output: text is stripped of hidden and
bidirectional characters and length-capped, answers are validated against the
options, and the answer reaches the model as an ordinary tool result. An
unanswered question is skipped after 5 minutes (the model is told to go with its
best assumption), and cancelling the run closes it. Turn it off with *Ask me
questions*.

## Always-allow rules

On an approval card, **Always allow** can save a *rule* instead of a one-off
grant. A rule is deliberately narrow:

- an exact tool and a pattern: for the editor, a folder (a file inside it, resolved
  through symlinks; never a drive root, your home folder or above); for the shell,
  the exact command you approved, or, for a short list of ordinary read/inspect
  commands such as `git status`, that command with any arguments, in both cases
  bound to the working folder it was approved in;
- never for commands with shell syntax (pipes, redirects, `&&`, substitutions),
  and never for computer control or any other tool: for those the button saves
  nothing and falls back to a grant for the current session, which the card says
  before you click;
- it never overrides a hard stop (destructive commands, system folders, credential
  stores) or the injection taint, and it never covers the instruction files below.

Rules live in `<AppData>/permission-rules.json` (up to 200) and are listed under
Settings, Buddy, *Always-allow rules*, with Remove and Remove all. Changing the
permission mode clears session grants but keeps saved rules.

## Instruction files and slash commands

A `NOCH.md` in the workspace folder (or `.noch/NOCH.md`; `AGENTS.md` and
`CLAUDE.md` are read when there is no `NOCH.md`) and one in Noch's data folder
are added to the system prompt as standing instructions, 16 KB per file and 24 KB
in all. They are read once per run, so the prompt stays stable within it.

Because a file like that steers every later run, Buddy **always asks** before
creating or changing one (instruction files and anything under `.noch/`), even in
Trusted mode, with a session grant or with a saved rule. A repository's
instruction file is still text written by someone else; it cannot widen what
Buddy may do, only what it is asked to do.

Slash commands: `/help`, `/new`, `/resume`, `/branch`, `/compact`, `/undo`,
`/mode`, `/init` and `/instructions` are built in and answer locally (nothing is
sent to the model). Your own commands are Markdown files in
`<workspace>/.noch/commands/` or `<AppData>/commands/`, run as `/name arguments`
(`$ARGUMENTS`, `$1`..`$9`); they cannot replace a built-in.

## Undo for a run

While a run works, each file it is about to change is first copied into
`<AppData>/undo/<run>/` (up to 25 MB per file, 200 MB per run, the last 10 runs
kept). *Undo N files* on the run summary, or `/undo`, puts everything back: old
contents restored, files the run created removed, files it deleted or moved
returned. A file you changed since the run is **left alone** and reported;
`/undo force` or *Undo anyway* overrides that.

What it covers: the editor tool (always one named file) and the files a shell
command names (redirects, `cp`, `mv`, `rm`, `Set-Content`, wildcards, folders).
What it cannot: whatever a program the command launches does (a build, an
installer, a script), the memory tool's files, computer control, and files over
25 MB. The run's summary lists these as untracked instead of promising more than
it can do.

## Voice input

Hold the microphone button (or Ctrl+Alt+V; Ctrl+Alt+Shift+V if that is taken) and
speak. Off by default, and it needs a speech-to-text service you provide: any
OpenAI-style `/audio/transcriptions` endpoint, over https or on this computer
only. Audio is recorded only while you hold, kept in memory (60 s at most, never
written to disk), sent to that service and then discarded; the key you enter for
it is used for nothing else and your chat key is never sent there. What comes
back is text: it goes into the input field to check, or is sent straight away
with *Send right away*. Dictation does not bypass any approval.

## Error recovery

The provider retries HTTP 408/409/425/429/500/502/503/504/529 and dropped or
timed-out connections up to 3 times. Backoff is 1 s, 2 s, then 4 s plus
jitter, and a `retry-after` header is honoured (capped at 20 s). A failed tool
returns `is_error: true` to the model, so the model can recover.

## Computer use

By default Buddy uses Claude's own computer-use tools, the ones Claude was trained
on (`computer_toolset_20260801`, with the 17 actions `screenshot`, `zoom`,
`mouse_move`, `left_click`, `right_click`, `middle_click`, `double_click`,
`triple_click`, `left_click_drag`, `left_mouse_down`, `left_mouse_up`, `scroll`,
`type`, `key`, `hold_key`, `wait` and `cursor_position`). Two things follow:

- **Several actions per step.** The model may send a batch of actions in one reply.
  Every action still goes through the gate on its own: an approval for each
  click or keystroke, as before. If one is declined, denied or fails, the rest of
  the batch is not run and shows as *skipped*.
- **Cost.** There is no separate fee for these tools: they are billed as ordinary
  tokens. The tool definitions add about 4,500 input tokens to a request, which are
  cached after the first, and only while computer use is switched on. The
  *Screenshot detail* setting trades tokens for sharpness: *Sharp* sends up to
  1920×1080 (about 4,800 tokens per screenshot), *Standard* up to 1280×800.

If a model does not know the toolset, the request is retried once with the
previous computer tool (`computer_20251124`, then `computer_20250124`), and finally
with Buddy's own look-alike set, and that stays so until the model or the setting
changes (only while the chat holds no computer call yet, since those blocks would not
match the older definitions). Switch
*Use Claude's own computer tools* off to use the look-alike set from the start.
The official toolset drives the primary display only. The bash and text-editor
tools stay Buddy's own (with its policy gate): Anthropic's bash tool assumes one
persistent shell, which Buddy's gated, per-command execution is not.

Screenshots are scaled to fit within the limits above with the aspect ratio kept,
and coordinates are mapped back to the real display, so DPI scaling and multiple
monitors work. Modifier keys can be held while clicking, dragging or scrolling
(`text`), and a key or button held down is always released when a run ends or
pauses. On Windows the input goes through `SendInput`. Other platforms get a
simulated backend that only logs actions.

Guardrails:

- Clicks, drags, scrolling, typing, keys and held keys or buttons ask in Ask and Auto-edit mode and
  are denied in Plan mode. Only screenshot, zoom, cursor position and wait run
  without asking.
- Every screenshot marks the run as having seen untrusted screen content.
  After that, risky key combos (Win+R, Ctrl+Alt+Del, Alt+F4, …) and text
  that looks like a shell command ask even in Trusted mode or with a session
  grant.
- Unknown actions, out-of-range coordinates and text over 2000 characters
  are rejected. Input actions are rate limited to a burst of 40 per minute,
  at least 80 ms apart.
- Buddy never types into a password field. On Windows it checks the focused
  control for `ES_PASSWORD` / `EM_GETPASSWORDCHAR` and checks for the Windows
  Security credential prompt. A UI Automation `IsPassword` check for browser
  and WinUI fields is a planned follow-up.
- While Buddy is in control, a glow runs around every screen and a HUD under
  the notch shows the current action and a Stop button. Ctrl+Alt+Esc, or
  holding Esc, stops the run. Any real mouse movement or key press pauses it
  until you press Resume. The overlays never take focus, the glow and cursor
  are click-through, and all of them are excluded from screen capture
  (`WDA_EXCLUDEFROMCAPTURE`), so the model never sees them.
- Each action shows in the trace with a small thumbnail of the screen around
  the target.

## Wake word

*Wake word* (Settings, Buddy, Voice input; off by default, and it needs Voice
input set up first) lets you say "hey buddy" (or a phrase you choose) instead of
clicking. Say it alone and Buddy listens for your request; or add the request in
the same breath: "hey buddy, open notepad". Buddy then does the work like any
typed request and reports back: the answer pops up as usual, and with *Read
reports aloud* a short summary is also spoken when the run finishes, needs your
approval, asks a question or fails. (Spoken reports are Windows' built-in voice,
or espeak-ng elsewhere; never code, links or Markdown, at most about 280
characters.)

This is the one feature with an open microphone, so it is built to hold less than
it could:

- **Why it needs consent.** There is no local keyword model; each utterance is
  transcribed by your speech-to-text service to look for the phrase. With a
  service on the internet that means *everything said near the microphone* is
  sent there, so Noch refuses to run until you tick *Allow sending everything it
  hears*. A speech service on this computer needs no consent: nothing leaves it.
  Taking the consent back closes the microphone at once.
- **What leaves the computer.** Only utterances: a small local detector cuts the
  audio into sentences and learns the room's noise level, so silence, fans and
  clicks are never sent. At most 12 utterances a minute are checked, one at a
  time; the rest are dropped, not queued. A service that fails closes the
  microphone and backs off (10 s, doubling to 5 min); a refused key stops it
  until the key is changed.
- **What is kept.** Nothing. A transcript is compared with the phrase and
  dropped, and is not stored, logged or exposed to the UI unless the phrase
  started it, and then only the request after the phrase is used. Audio stays in
  memory.
- **False wake-ups.** The phrase only counts at the very start of an utterance
  (after at most one "hey", "ok", ...), so the word inside ordinary conversation
  does nothing. Phrases under four letters are refused. A second wake within 1.5 s
  is ignored. Say it and nothing more, and Buddy gives up after 7 s.
- **Always visible.** A small microphone badge sits on the orb whenever the
  microphone is open for this (it turns red once the phrase was heard), and the OS
  shows its own microphone indicator.
- **It pauses** while push-to-talk is in use and while Buddy is speaking (so it
  cannot wake itself), and when Buddy is busy a new request is not started.
- **No new powers.** What it hears becomes an ordinary request: every tool still
  goes through the permission gate and its approvals, and "Always allow" rules,
  hard stops and the instruction-file protection apply unchanged. Because sound
  from speakers or a video could say the phrase too, keep the permission mode on
  *Ask* if you leave the wake word on in a room with other audio.

## Traces

When *Keep run traces* is on, each run writes
`<AppData>/traces/trace-<id>.json`. A trace holds model calls, tool calls,
permission decisions, approvals, guardrail hits, retries and compactions,
along with token totals. The newest 40 traces are kept. Secrets are redacted
before a trace is written.

## Sources

- Claude Agent SDK: permissions, hooks, sessions and the secure deployment
  guide (docs.claude.com/en/docs/agent-sdk/…)
- Anthropic: context editing, prompt caching, API errors (429 / 529 overloaded)
- OpenAI Agents SDK: guardrails, human-in-the-loop (`needsApproval`,
  interruptions), tracing, running agents (`max_turns`)
