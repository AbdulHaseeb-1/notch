#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

#include "OrtRuntime.h"

// Local wake-word detection with openWakeWord's models.
//
// openWakeWord runs three small neural networks in a row on the microphone
// stream, all on this computer:
//   audio (1280 new samples = 80 ms) -> melspectrogram.onnx -> mel frames
//   the last 76 mel frames          -> embedding_model.onnx -> 96 numbers
//   the last 16 embeddings (1.3 s)  -> <the wake word>.onnx -> a score 0..1
// The first two are shared by every wake word; the third one is the phrase
// ("Hey Buddy"). Nothing here touches the network and no audio is stored.
//
// Not thread-safe: use it from one thread (WakeWord runs it on a worker).
class WakeDetector
{
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kChunkSamples = 1280;     // one step: 80 ms
    static constexpr int kMelContext = 160 * 3;    // samples kept ahead of a chunk for the mel window
    static constexpr int kMelBands = 32;
    static constexpr int kEmbedMelFrames = 76;
    static constexpr int kEmbedSize = 96;
    static constexpr int kWakeEmbeddings = 16;

    struct Models {
        QByteArray melspectrogram;
        QByteArray embedding;
        QByteArray wakeword;
    };

    WakeDetector();
    ~WakeDetector();

    // Opens the three models (ONNX Runtime must already be loaded). On failure
    // `error` says which one and why.
    bool load(const Models &models, QString *error);
    bool loaded() const { return m_loaded; }

    // Forget everything heard so far (starts again from a quiet room).
    void reset();

    // Feeds 16 kHz mono 16-bit PCM in chunks of any size. One score per
    // completed 80 ms step is appended to `scores` (when given); the return
    // value is the highest of them (0 if no step completed).
    float feed(const QByteArray &pcm, std::vector<float> *scores = nullptr);

    int stepsProcessed() const { return m_steps; }

private:
    bool step(const int16_t *chunk, bool score, float *out);
    bool runModel(ortrt::Session &model, const float *in, const std::vector<int64_t> &shape,
                  std::vector<float> *out);

    ortrt::Session m_mel;
    ortrt::Session m_embed;
    ortrt::Session m_wake;
    bool m_loaded = false;

    std::vector<int16_t> m_pending;     // not yet a whole chunk
    std::vector<int16_t> m_tail;        // the audio just before the next chunk (kMelContext samples)
    std::vector<float> m_melRows;       // mel frames, kMelBands floats each, oldest first
    std::vector<float> m_embeddings;    // embeddings, kEmbedSize floats each, oldest first
    std::vector<float> m_scratch;
    int m_steps = 0;
};
