#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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
//   the last 16 embeddings (1.3 s)  -> <a wake word>.onnx -> a score 0..1
// The first two are shared by every wake word; the third one is a phrase
// ("Hey Buddy"). Several phrases can be loaded at once: they all read the
// same embeddings, so an extra phrase costs one tiny classifier run per step.
// Nothing here touches the network and no audio is stored.
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
        // One classifier per wake phrase, in the order scores come back.
        struct Phrase {
            QString name;        // what the user calls it ("Hey Buddy")
            QByteArray model;    // the .onnx file's bytes
        };
        QList<Phrase> phrases;
    };

    WakeDetector();
    ~WakeDetector();

    // Opens the models (ONNX Runtime must already be loaded). On failure
    // `error` says which one and why.
    bool load(const Models &models, QString *error);
    bool loaded() const { return m_loaded; }

    // The loaded phrases, in Models::phrases order (the order feed() reports).
    QStringList phraseNames() const { return m_phraseNames; }
    int phraseCount() const { return int(m_wakes.size()); }

    // Forget everything heard so far (starts again from a quiet room).
    void reset();

    // Feeds 16 kHz mono 16-bit PCM in chunks of any size. One score per
    // completed 80 ms step is appended to `scores` (when given) — the best
    // score across the phrases — and the return value is the highest of them
    // (0 if no step completed). When `perPhrase` is given it is resized to one
    // vector per phrase, each holding that phrase's score per step, so a
    // caller listening for several phrases can tell them apart.
    float feed(const QByteArray &pcm, std::vector<float> *scores = nullptr,
               std::vector<std::vector<float>> *perPhrase = nullptr);

    int stepsProcessed() const { return m_steps; }

private:
    bool step(const int16_t *chunk, bool score, std::vector<float> *out);
    bool runModel(ortrt::Session &model, const float *in, const std::vector<int64_t> &shape,
                  std::vector<float> *out);

    ortrt::Session m_mel;
    ortrt::Session m_embed;
    std::vector<std::unique_ptr<ortrt::Session>> m_wakes;
    QStringList m_phraseNames;
    bool m_loaded = false;

    std::vector<int16_t> m_pending;     // not yet a whole chunk
    std::vector<int16_t> m_tail;        // the audio just before the next chunk (kMelContext samples)
    std::vector<float> m_melRows;       // mel frames, kMelBands floats each, oldest first
    std::vector<float> m_embeddings;    // embeddings, kEmbedSize floats each, oldest first
    std::vector<float> m_scratch;
    int m_steps = 0;
};
