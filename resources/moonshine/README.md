# Moonshine Tiny speech-to-text

Noch turns speech into text on this computer with
[Moonshine Tiny](https://github.com/moonshine-ai/moonshine) (English, MIT licence),
run through ONNX Runtime.

| File | What it is | How it gets there |
| --- | --- | --- |
| `vocab.json` | the token list, compiled into Noch (made by `tools/extract_moonshine_vocab.py`) | in this folder |
| `encoder.onnx`, `decoder.onnx` | the quantized model, about 28 MB | downloaded once at configure time from `onnx-community/moonshine-tiny-ONNX` (hash-checked), copied next to the executable in `models/moonshine-tiny`; turn off with `-DNOCH_FETCH_MOONSHINE=OFF` |

To use your own copy, put the two files in `%APPDATA%\noch\models\moonshine-tiny`
(Settings > Buddy > Voice input > Open models folder) or set `NOCH_MOONSHINE_DIR`.
