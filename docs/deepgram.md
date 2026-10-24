# Deepgram: live conversation, speech to text and voice

Noch can use [Deepgram](https://deepgram.com) in two ways. Both share one API key
(Settings > Deepgram).

| | What Deepgram does | What does the thinking |
| --- | --- | --- |
| **Live conversation** (Voice Agent) | hears you, thinks, and speaks, in one continuous call | the model chosen in Settings > Deepgram (Claude Haiku 5.5 by default), run by Deepgram or at your own endpoint |
| **Speech to text / voice only** | turns a push-to-talk clip into words (`/v1/listen`), reads Buddy's replies (`/v1/speak`) | Buddy's own model and tools, as always |

## Live conversation

`src/voice/DeepgramAgent.*` opens `wss://agent.deepgram.com/v1/agent/converse`:

* the microphone goes up as 16 kHz 16-bit PCM in 40 ms frames; the voice comes back
  as 24 kHz PCM and is played as it arrives (`StreamingPlayer`, with a 160 ms
  jitter buffer and an instant flush for interruptions);
* the **Settings** message is built from the app's settings by
  `DeepgramProtocol.cpp` (`buildSettings`): listen = Nova 3 / Nova 2 / Flux, think =
  provider + model + prompt (+ your endpoint), speak = an Aura voice with speed;
* `ask_buddy`: the model has one function. When you ask for something to be *done* it
  calls `ask_buddy(request)`; `DeepgramAgent` emits `buddyRequested`, `Buddy.qml`
  runs the request through the normal agent (same tools, same approvals, same safety
  settings) and answers the call with the outcome, which the model says in its own
  words. Approval and question prompts are spoken through the call too;
* a spoken request is a request like any other: it runs under your permission mode, so
  with "approve everything" on, a voice (or a TV in the room) can make Buddy act without
  asking. Keep the default mode if that matters to you;
* the microphone is open, and audio goes to Deepgram, **only** between Start and End.
  The state is always visible (the caption under the orb, the listening/speaking
  effects). A quiet conversation ends itself after the configured time;
* **speakers vs headphones**: through speakers the microphone is muted (silence is
  sent, so the stream stays alive) while Buddy talks, so it cannot answer itself.
  With headphones you can talk over it. *Interrupt* works in both;
* a dropped connection is resumed up to three times with the conversation so far
  (`agent.context.messages`); prompt, voice and speed changes apply immediately
  (`UpdatePrompt` / `UpdateSpeak`), other changes from the next conversation.

### Things learnt from the live service

* Haiku 5.5 (and other new models) reject the `temperature` field outright
  (`"temperature is deprecated for this model"`, then `FAILED_TO_THINK`). It is only
  sent for models known to take it (`modelAcceptsTemperature`).
* Deepgram streams the voice at about real-time pace, so the player pre-buffers a
  little; without it every short gap in the stream was audible.
* Nova hears names it cannot know wrongly ("Notecad" for "Notepad"). *Words to listen
  for* sets keyterms; the default is `Buddy, Noch, Notepad`.
* Flux (`flux-general-en`) decides for itself when you are done speaking; Nova waits
  for a pause. The pre-recorded endpoint used for push-to-talk has no Flux, so clips
  use Nova 3 whatever the conversation uses.

### Settings > Deepgram > Test connection

Opens a short session with your settings (no microphone), applies them and asks the
model one question. A wrong model name, a rejected key or a field the model does not
accept shows up there, not in the middle of a conversation.

## Speech to text and voice, with Buddy's own model

* `VoiceInput`'s one speech-to-text engine is Deepgram (`DeepgramTranscriber`,
  asynchronous, no worker thread): push-to-talk clips and the request spoken
  after the wake word are both transcribed by it. Wake-word phrase detection
  itself stays on this computer (openWakeWord); only what is spoken after the
  phrase is sent.
* `SpeechOut` has a *deepgram* engine. Each sentence is requested as raw PCM and
  streamed into a `StreamingPlayer` as it arrives, so the first sound comes at the
  first bytes instead of after the whole sentence. A failure is shown in
  Settings (`error`); there is no fallback voice.

## Tests

| Test | What it covers | Needs |
| --- | --- | --- |
| `tst_wsclient` | the WebSocket client against a local server (handshake, framing, fragmentation, ping, close, every failure) | nothing |
| `tst_deepgram` | settings builder, event parser, model rules, player, and the whole session against a scripted local Deepgram (barge-in, `ask_buddy`, speaker mode, reconnect, idle end, live changes, connection test) | nothing |
| `tst_deepgramrest` | Deepgram speech to text and voice through `VoiceInput` / `SpeechOut`, with failures | nothing |
| `tst_deepgramlive` | the real service: model list, connection test, a spoken question, `ask_buddy`, talking over the agent, STT, streamed voice | `DEEPGRAM_API_KEY` in the environment (skipped without it) |

Debugging a call: `QT_LOGGING_RULES="noch.deepgram.debug=true"` prints message types
and state changes (never audio, settings or keys).

## Where the key lives

Like the other API keys it is stored in Noch's settings file (`%APPDATA%\noch\noch.ini`,
and the JSON backup next to it). It is only ever sent to Deepgram, in an
`Authorization` header, never in an address or a log.
