# Buddy agent safety layer

Noch's Buddy can run a multi-turn tool loop (shell, file editor, computer use,
web, memory, …). Every tool call the model makes passes through a small safety
layer before anything runs. Code: `src/AgentPolicy.{h,cpp}` (pure, unit-tested
in `tests/tst_agentpolicy.cpp`) and the gate in `src/BuddyProvider.cpp`
(`gateAndExecute`, `resolveApproval`, `guardToolOutput`, `compactContext`,
`scheduleRetry`, `finishTrace`).

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
   or `tee`), editor `view`, observing computer actions (screenshot, cursor
   position, wait), memory, search, todo and advisor run without asking.
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

- **Context editing:** before each model call, the newest 4 tool results stay
  verbatim. Older results over 600 characters are replaced with a short
  placeholder, and older screenshots are dropped. This follows Anthropic's
  "clear tool uses" strategy.
- **Prompt caching (Anthropic):** the system prompt and the tool block carry
  `cache_control: {type: "ephemeral"}`. Cache hits are counted from
  `cache_read_input_tokens` (Anthropic) or `prompt_tokens_details.cached_tokens`
  (OpenAI) and reported in the response meta.
- **Max turns:** after 12 tool rounds the run stops and says so (as with
  `max_turns` in the OpenAI Agents SDK).

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

## Session memory

Earlier turns of the conversation are sent within a 24 000-character budget.
Older turns are folded into one short "[Session memory] Earlier in this
session I asked about: …" note instead of being dropped silently. When the
memory tool is on, the system prompt lists up to 16 saved `/memories` files
(the Claude Agent SDK memory-tool pattern). This list is taken once at the
start of a run, so the system prompt stays byte-identical across turns and the
prompt cache survives memory writes.

## Error recovery

The provider retries HTTP 408/409/425/429/500/502/503/504/529 and dropped or
timed-out connections up to 3 times. Backoff is 1 s, 2 s, then 4 s plus
jitter, and a `retry-after` header is honoured (capped at 20 s). A failed tool
returns `is_error: true` to the model, so the model can recover.

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
