#pragma once

#include <QByteArray>
#include <QString>

#include <string>
#include <vector>

#include "OrtRuntime.h"

// Speech to text on this computer with Moonshine Tiny (Useful Sensors, MIT licence):
// a small encoder-decoder transformer built for short, live speech, about thirty
// times faster than real time on an ordinary CPU, English only. Nothing is sent
// anywhere and no audio is kept.
//
//   audio (16 kHz, raw) -> encoder.onnx -> hidden states
//   greedy decoder (decoder.onnx, with a key/value cache) -> token ids -> text
//
// The ONNX files are the quantized `onnx-community/moonshine-tiny-ONNX` export
// (encoder_model_quantized / decoder_model_merged_quantized); vocab.json is its
// tokenizer's id -> piece table (tools/extract_moonshine_vocab.py).
//
// Not thread-safe: use it from one thread.
class Moonshine
{
public:
    static constexpr int kSampleRate = 16000;
    static constexpr double kMinSeconds = 0.1;     // shorter clips are not speech
    static constexpr double kMaxSeconds = 64.0;    // what the model was trained for
    static constexpr int kTokensPerSecond = 6;     // the model's own bound on speaking rate

    // Opens the models (ONNX Runtime must already be loaded).
    bool load(const QByteArray &encoder, const QByteArray &decoder, const QByteArray &vocabJson, QString *error);
    bool loaded() const { return m_loaded; }

    // 16 kHz mono 16-bit PCM in, text out ("" when nothing was recognised).
    // `error` is set (and the text empty) only when the models failed.
    QString transcribe(const QByteArray &pcm16, QString *error = nullptr);

    // Pieces to text: SentencePiece spacing and byte tokens, specials dropped.
    QString detokenize(const std::vector<int> &ids) const;

private:
    ortrt::Session m_encoder;
    ortrt::Session m_decoder;
    std::vector<std::string> m_pieces;
    bool m_loaded = false;
};
