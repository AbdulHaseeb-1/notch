#include "Moonshine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kLayers = 6;
constexpr int kHeads = 8;
constexpr int kHeadDim = 36;
constexpr int kHidden = 288;
constexpr int kBos = 1;
constexpr int kEos = 2;
constexpr int kFirstSpecial = 32000;   // <<ST_n>> markers

// Each layer has self-attention (decoder) and cross-attention (encoder) keys and
// values, in the order the model lists them.
std::string kvName(const char *prefix, int layer, const char *kind, const char *which)
{
    return std::string(prefix) + "." + std::to_string(layer) + "." + kind + "." + which;
}

struct KvCache {
    // [layer][0 = key, 1 = value] -> floats of shape [1, kHeads, length, kHeadDim]
    std::vector<float> data[kLayers][2];
    int64_t length[kLayers][2] = {};
};

} // namespace

bool Moonshine::load(const QByteArray &encoder, const QByteArray &decoder, const QByteArray &vocabJson, QString *error)
{
    m_loaded = false;
    const QJsonDocument vocab = QJsonDocument::fromJson(vocabJson);
    if (!vocab.isArray() || vocab.array().size() < kFirstSpecial) {
        if (error)
            *error = QObject::tr("The Moonshine vocabulary (vocab.json) is not valid.");
        return false;
    }
    m_pieces.clear();
    for (const QJsonValue &v : vocab.array())
        m_pieces.push_back(v.toString().toStdString());

    QString why;
    if (!m_encoder.open(encoder, &why)) {
        if (error)
            *error = QObject::tr("The Moonshine encoder did not load: %1").arg(why);
        return false;
    }
    if (!m_decoder.open(decoder, &why)) {
        if (error)
            *error = QObject::tr("The Moonshine decoder did not load: %1").arg(why);
        return false;
    }
    m_loaded = true;
    // A second of quiet proves the whole chain runs (and warms the runtime up).
    // What it hears does not matter, only that nothing fails.
    QString probeError;
    transcribe(QByteArray(Moonshine::kSampleRate * 2, '\0'), &probeError);
    if (!probeError.isEmpty()) {
        m_loaded = false;
        if (error)
            *error = QObject::tr("The Moonshine models do not work together: %1").arg(probeError);
        return false;
    }
    return true;
}

QString Moonshine::detokenize(const std::vector<int> &ids) const
{
    std::string bytes;
    for (int id : ids) {
        if (id <= kEos || id >= int(m_pieces.size()) || id >= kFirstSpecial)
            continue;
        const std::string &piece = m_pieces[size_t(id)];
        // <0xNN>: a raw byte (how characters outside the vocabulary are spelled).
        if (piece.size() == 6 && piece[0] == '<' && piece[1] == '0' && piece[2] == 'x' && piece[5] == '>') {
            bool ok = true;
            unsigned value = 0;
            for (int i = 3; i < 5; ++i) {
                const char c = piece[size_t(i)];
                const int digit = c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1));
                if (digit < 0) {
                    ok = false;
                    break;
                }
                value = value * 16 + unsigned(digit);
            }
            if (ok) {
                bytes.push_back(char(value));
                continue;
            }
        }
        bytes += piece;
    }
    // U+2581 (the SentencePiece space) -> " ".
    QString text = QString::fromUtf8(bytes.data(), qsizetype(bytes.size()));
    text.replace(QChar(0x2581), QLatin1Char(' '));
    if (text.startsWith(QLatin1Char(' ')))
        text.remove(0, 1);
    return text.trimmed();
}

QString Moonshine::transcribe(const QByteArray &pcm16, QString *error)
{
    if (!m_loaded && !m_decoder.isOpen())
        return QString();
    const qsizetype total = pcm16.size() / 2;
    if (total < qsizetype(kMinSeconds * kSampleRate))
        return QString();
    const qsizetype samples = std::min<qsizetype>(total, qsizetype(kMaxSeconds * kSampleRate));
    const auto *pcm = reinterpret_cast<const int16_t *>(pcm16.constData());
    std::vector<float> audio;
    audio.resize(size_t(samples));
    for (qsizetype i = 0; i < samples; ++i)
        audio[size_t(i)] = float(pcm[i]) / 32768.0f;

    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QString();
    };

    // 1. the encoder: the whole clip at once.
    std::vector<ortrt::TensorOut> encoded;
    QString why;
    if (!m_encoder.runNamed({ { "input_values", ortrt::DType::Float32, { 1, int64_t(samples) }, audio.data() } },
                            { "last_hidden_state" }, &encoded, &why)
        || encoded.empty() || encoded[0].shape.size() != 3 || encoded[0].shape[2] != kHidden)
        return fail(QObject::tr("Moonshine could not read the audio: %1").arg(why));
    const ortrt::TensorOut &hidden = encoded[0];

    // 2. the decoder: one token at a time, with the self-attention cache growing
    // and the cross-attention cache (from the encoder) fixed after the first step.
    KvCache cache;
    for (int l = 0; l < kLayers; ++l)
        for (int kv = 0; kv < 2; ++kv)
            for (int w = 0; w < 2; ++w)
                cache.length[l][kv] = 0;
    KvCache cross;
    std::vector<int> ids;
    int64_t current = kBos;
    const int maxTokens = std::max(8, int(double(samples) / kSampleRate * kTokensPerSecond) + 1);

    for (int step = 0; step < maxTokens; ++step) {
        const bool cached = step > 0;
        uint8_t useCache = cached ? 1 : 0;
        std::vector<ortrt::TensorIn> in;
        in.push_back({ "input_ids", ortrt::DType::Int64, { 1, 1 }, &current });
        in.push_back({ "encoder_hidden_states", ortrt::DType::Float32, hidden.shape, hidden.data.data() });
        for (int l = 0; l < kLayers; ++l) {
            for (int kv = 0; kv < 2; ++kv) {
                const char *which = kv == 0 ? "key" : "value";
                in.push_back({ kvName("past_key_values", l, "decoder", which), ortrt::DType::Float32,
                               { 1, kHeads, cache.length[l][kv], kHeadDim }, cache.data[l][kv].data() });
                in.push_back({ kvName("past_key_values", l, "encoder", which), ortrt::DType::Float32,
                               { 1, kHeads, cross.length[l][kv], kHeadDim }, cross.data[l][kv].data() });
            }
        }
        in.push_back({ "use_cache_branch", ortrt::DType::Bool, { 1 }, &useCache });

        std::vector<std::string> want{ "logits" };
        for (int l = 0; l < kLayers; ++l)
            for (const char *which : { "key", "value" })
                want.push_back(kvName("present", l, "decoder", which));
        if (!cached)
            for (int l = 0; l < kLayers; ++l)
                for (const char *which : { "key", "value" })
                    want.push_back(kvName("present", l, "encoder", which));

        std::vector<ortrt::TensorOut> out;
        if (!m_decoder.runNamed(in, want, &out, &why) || out.empty())
            return fail(QObject::tr("Moonshine stopped while writing: %1").arg(why));

        // The next token: the best-scoring one at the last position.
        const ortrt::TensorOut &logits = out[0];
        if (logits.shape.size() != 3 || logits.shape[2] <= 0)
            return fail(QObject::tr("Moonshine gave an unexpected answer."));
        const size_t vocab = size_t(logits.shape[2]);
        const float *last = logits.data.data() + logits.data.size() - vocab;
        const int next = int(std::max_element(last, last + vocab) - last);

        size_t k = 1;
        for (int l = 0; l < kLayers; ++l) {
            for (int kv = 0; kv < 2; ++kv, ++k) {
                cache.data[l][kv] = std::move(out[k].data);
                cache.length[l][kv] = out[k].shape.size() == 4 ? out[k].shape[2] : 0;
            }
        }
        if (!cached) {
            for (int l = 0; l < kLayers; ++l) {
                for (int kv = 0; kv < 2; ++kv, ++k) {
                    cross.data[l][kv] = std::move(out[k].data);
                    cross.length[l][kv] = out[k].shape.size() == 4 ? out[k].shape[2] : 0;
                }
            }
        }
        if (next == kEos)
            break;
        ids.push_back(next);
        current = next;
    }
    return detokenize(ids);
}
