# Wake word models

Noch's "Hey Buddy" wake word runs on this computer with
[openWakeWord](https://github.com/dscripka/openWakeWord) and ONNX Runtime.

| File | What it is | Licence |
| --- | --- | --- |
| `melspectrogram.onnx` | openWakeWord's audio front end (shared by every wake word) | Apache-2.0 |
| `embedding_model.onnx` | openWakeWord's speech embedding, from Google's `speech_embedding` (shared) | Apache-2.0 |
| `hey_buddy.onnx` | the "Hey Buddy" classifier (a community model from openwakeword.com, model 1716, `Hey_Buddy_20260524_215927`) | see openwakeword.com's licence page |

All three are compiled into Noch (see `CMakeLists.txt`).

## Using a different wake word

Drop any openWakeWord `.onnx` classifier (16 x 96 input) into
`%APPDATA%\noch\wakeword\` (Settings > Buddy > Wake word > Open models folder).
It replaces `hey_buddy.onnx`. Files named `melspectrogram.onnx` and
`embedding_model.onnx` there replace the shared ones.
