#include "WakeDetector.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr size_t kMaxMelRows = 970;        // ten seconds
constexpr size_t kMaxEmbeddings = 120;
constexpr int kWarmupSteps = 26;           // two seconds of quiet, so the first real score is a fair one
}

WakeDetector::WakeDetector() = default;
WakeDetector::~WakeDetector() = default;

bool WakeDetector::load(const Models &models, QString *error)
{
    m_loaded = false;
    m_wakes.clear();
    m_phraseNames.clear();
    QString why;
    if (!m_mel.open(models.melspectrogram, &why)) {
        if (error)
            *error = QObject::tr("The audio front end (melspectrogram.onnx) did not load: %1").arg(why);
        return false;
    }
    if (!m_embed.open(models.embedding, &why)) {
        if (error)
            *error = QObject::tr("The speech embedding model (embedding_model.onnx) did not load: %1").arg(why);
        return false;
    }
    if (models.phrases.isEmpty()) {
        if (error)
            *error = QObject::tr("No wake word model was given.");
        return false;
    }
    for (const Models::Phrase &p : models.phrases) {
        auto session = std::make_unique<ortrt::Session>();
        if (!session->open(p.model, &why)) {
            if (error)
                *error = QObject::tr("The wake word model for “%1” did not load: %2").arg(p.name, why);
            return false;
        }
        m_wakes.push_back(std::move(session));
        m_phraseNames << p.name;
    }
    m_loaded = true;
    reset();
    // A model that does not behave (wrong shapes) is caught by the first scores.
    std::vector<std::vector<float>> perPhrase;
    feed(QByteArray(kChunkSamples * 2 * 2, '\0'), nullptr, &perPhrase);
    if (perPhrase.size() != m_wakes.size()) {
        m_loaded = false;
        if (error)
            *error = QObject::tr("The wake word models did not run.");
        return false;
    }
    for (size_t i = 0; i < perPhrase.size(); ++i) {
        if (perPhrase[i].empty() || !std::isfinite(perPhrase[i].back())) {
            m_loaded = false;
            if (error)
                *error = QObject::tr("The wake word model for “%1” is not an openWakeWord model (it did not accept a 16 x 96 input).")
                             .arg(m_phraseNames[int(i)]);
            return false;
        }
    }
    reset();
    return true;
}

void WakeDetector::reset()
{
    m_pending.clear();
    m_tail.assign(size_t(kMelContext), 0);
    m_melRows.assign(size_t(kEmbedMelFrames) * kMelBands, 1.0f);   // what openWakeWord starts from
    m_embeddings.clear();
    m_steps = 0;
    if (!m_loaded)
        return;
    // Warm up on digital silence so that the first real chunk already sees a
    // full, quiet history (the reference implementation starts from noise).
    const std::vector<int16_t> quiet(size_t(kChunkSamples), 0);
    for (int i = 0; i < kWarmupSteps; ++i)
        step(quiet.data(), false, nullptr);
    m_steps = 0;
}

bool WakeDetector::runModel(ortrt::Session &model, const float *in, const std::vector<int64_t> &shape,
                            std::vector<float> *out)
{
    std::vector<int64_t> outShape;
    QString why;
    return model.run(in, shape, out, &outShape, &why);
}

float WakeDetector::feed(const QByteArray &pcm, std::vector<float> *scores,
                         std::vector<std::vector<float>> *perPhrase)
{
    if (perPhrase)
        perPhrase->assign(m_wakes.size(), {});
    if (!m_loaded || pcm.size() < 2)
        return 0.0f;
    const auto *samples = reinterpret_cast<const int16_t *>(pcm.constData());
    m_pending.insert(m_pending.end(), samples, samples + pcm.size() / 2);

    float best = 0.0f;
    size_t used = 0;
    while (m_pending.size() - used >= size_t(kChunkSamples)) {
        std::vector<float> stepScores;
        if (step(m_pending.data() + used, true, &stepScores) && stepScores.size() == m_wakes.size()) {
            float stepBest = 0.0f;
            for (size_t i = 0; i < stepScores.size(); ++i) {
                stepBest = std::max(stepBest, stepScores[i]);
                if (perPhrase)
                    (*perPhrase)[i].push_back(stepScores[i]);
            }
            best = std::max(best, stepBest);
            if (scores)
                scores->push_back(stepBest);
        }
        used += size_t(kChunkSamples);
    }
    m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(used));
    return best;
}

bool WakeDetector::step(const int16_t *chunk, bool wantScore, std::vector<float> *out)
{
    // 1. mel frames for the new audio (with a little of the audio before it).
    const size_t window = size_t(kMelContext + kChunkSamples);
    m_scratch.resize(window);
    for (size_t i = 0; i < size_t(kMelContext); ++i)
        m_scratch[i] = float(m_tail[i]);
    for (size_t i = 0; i < size_t(kChunkSamples); ++i)
        m_scratch[size_t(kMelContext) + i] = float(chunk[i]);
    // The next window starts with the last kMelContext samples heard so far.
    {
        std::vector<int16_t> next;
        next.resize(size_t(kMelContext));
        for (size_t i = 0; i < size_t(kMelContext); ++i)
            next[i] = chunk[size_t(kChunkSamples) - size_t(kMelContext) + i];
        m_tail.swap(next);
    }

    std::vector<float> mel;
    std::vector<int64_t> melShape;
    QString why;
    if (!m_mel.run(m_scratch.data(), { 1, int64_t(window) }, &mel, &melShape, &why) || mel.empty()
        || mel.size() % kMelBands != 0)
        return false;
    for (float &v : mel)
        v = v / 10.0f + 2.0f;               // openWakeWord's own scaling
    m_melRows.insert(m_melRows.end(), mel.begin(), mel.end());
    const size_t maxFloats = kMaxMelRows * kMelBands;
    if (m_melRows.size() > maxFloats)
        m_melRows.erase(m_melRows.begin(), m_melRows.begin() + std::ptrdiff_t(m_melRows.size() - maxFloats));

    // 2. one embedding from the newest 76 mel frames.
    const float *latest = m_melRows.data() + (m_melRows.size() - size_t(kEmbedMelFrames) * kMelBands);
    std::vector<float> embedding;
    if (!runModel(m_embed, latest, { 1, kEmbedMelFrames, kMelBands, 1 }, &embedding)
        || embedding.size() != size_t(kEmbedSize))
        return false;
    m_embeddings.insert(m_embeddings.end(), embedding.begin(), embedding.end());
    const size_t maxEmb = kMaxEmbeddings * kEmbedSize;
    if (m_embeddings.size() > maxEmb)
        m_embeddings.erase(m_embeddings.begin(), m_embeddings.begin() + std::ptrdiff_t(m_embeddings.size() - maxEmb));
    ++m_steps;

    if (!wantScore)
        return true;

    // 3. the wake word models, each on the newest 16 embeddings. The features
    // are computed once; every phrase reads the same window.
    if (m_embeddings.size() < size_t(kWakeEmbeddings) * kEmbedSize)
        return false;
    const float *recent = m_embeddings.data() + (m_embeddings.size() - size_t(kWakeEmbeddings) * kEmbedSize);
    if (out)
        out->assign(m_wakes.size(), 0.0f);
    for (size_t i = 0; i < m_wakes.size(); ++i) {
        std::vector<float> score;
        // One broken classifier must not silence the others: it scores NaN
        // for this step, which load() reports against that phrase by name
        // and which never wins a "best score" comparison at run time.
        if (!runModel(*m_wakes[i], recent, { 1, kWakeEmbeddings, kEmbedSize }, &score) || score.empty()) {
            if (out)
                (*out)[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        if (out)
            (*out)[i] = std::isfinite(score[0]) ? std::clamp(score[0], 0.0f, 1.0f) : 0.0f;
    }
    return true;
}
