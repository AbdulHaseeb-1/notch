#include "VoiceAudio.h"

#include <QtEndian>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace voice {

namespace {

constexpr double kSilentPeak = 0.012;       // below this nothing was said (about -38 dBFS)
constexpr double kSpeechFrame = 0.02;       // a 20 ms frame counts as sound above this level
constexpr int kKeepMs = 150;

// One sample as -1..1, whatever the sample format.
float readSample(const char *p, QAudioFormat::SampleFormat f)
{
    switch (f) {
    case QAudioFormat::UInt8:
        return (float(std::uint8_t(*p)) - 128.0f) / 128.0f;
    case QAudioFormat::Int16: {
        std::int16_t v;
        std::memcpy(&v, p, 2);
        return float(v) / 32768.0f;
    }
    case QAudioFormat::Int32: {
        std::int32_t v;
        std::memcpy(&v, p, 4);
        return float(double(v) / 2147483648.0);
    }
    case QAudioFormat::Float: {
        float v;
        std::memcpy(&v, p, 4);
        return std::isfinite(v) ? qBound(-1.0f, v, 1.0f) : 0.0f;
    }
    default:
        return 0.0f;
    }
}

int sampleBytes(QAudioFormat::SampleFormat f)
{
    switch (f) {
    case QAudioFormat::UInt8: return 1;
    case QAudioFormat::Int16: return 2;
    case QAudioFormat::Int32: return 4;
    case QAudioFormat::Float: return 4;
    default: return 0;
    }
}

} // namespace

QByteArray toMono16k(const QByteArray &raw, const QAudioFormat &format)
{
    const int bytes = sampleBytes(format.sampleFormat());
    const int channels = format.channelCount();
    const int rate = format.sampleRate();
    if (bytes == 0 || channels <= 0 || rate <= 0 || raw.isEmpty())
        return QByteArray();
    const int frameBytes = bytes * channels;
    const qsizetype frames = raw.size() / frameBytes;
    if (frames == 0)
        return QByteArray();

    // Mono at the source rate.
    QVector<float> mono(frames);
    const char *p = raw.constData();
    for (qsizetype i = 0; i < frames; ++i) {
        float sum = 0;
        for (int c = 0; c < channels; ++c)
            sum += readSample(p + i * frameBytes + c * bytes, format.sampleFormat());
        mono[i] = sum / channels;
    }

    // To 16 kHz. Cutting the rate down needs a low-pass first or high
    // frequencies fold back as noise: a box average over the source samples
    // that fall into each output sample is enough for speech.
    QVector<float> out;
    if (rate == kSampleRate) {
        out = mono;
    } else {
        const double step = double(rate) / kSampleRate;
        const qsizetype n = qsizetype(std::floor(double(frames) / step));
        out.resize(n);
        for (qsizetype i = 0; i < n; ++i) {
            const double pos = i * step;
            if (step > 1.0) {
                const qsizetype a = qsizetype(std::floor(pos));
                const qsizetype b = qMin<qsizetype>(frames, qsizetype(std::ceil(pos + step)));
                float acc = 0;
                for (qsizetype k = a; k < b; ++k)
                    acc += mono[k];
                out[i] = b > a ? acc / float(b - a) : 0.0f;
            } else {
                const qsizetype a = qsizetype(std::floor(pos));
                const qsizetype b = qMin<qsizetype>(frames - 1, a + 1);
                const float t = float(pos - double(a));
                out[i] = mono[a] * (1.0f - t) + mono[b] * t;
            }
        }
    }

    QByteArray pcm(out.size() * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<std::int16_t *>(pcm.data());
    for (qsizetype i = 0; i < out.size(); ++i)
        dst[i] = std::int16_t(qBound(-32768.0f, std::round(out[i] * 32767.0f), 32767.0f));
    return pcm;
}

QByteArray encodeWav(const QByteArray &pcm16, int sampleRate)
{
    QByteArray wav;
    wav.reserve(44 + pcm16.size());
    auto u32 = [&wav](quint32 v) { char b[4]; qToLittleEndian(v, b); wav.append(b, 4); };
    auto u16 = [&wav](quint16 v) { char b[2]; qToLittleEndian(v, b); wav.append(b, 2); };
    const quint32 dataBytes = quint32(pcm16.size());
    wav.append("RIFF", 4);
    u32(36 + dataBytes);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    u32(16);                       // PCM header size
    u16(1);                        // PCM
    u16(1);                        // mono
    u32(quint32(sampleRate));
    u32(quint32(sampleRate) * 2);  // byte rate
    u16(2);                        // block align
    u16(16);                       // bits per sample
    wav.append("data", 4);
    u32(dataBytes);
    wav.append(pcm16);
    return wav;
}

double level(const QByteArray &pcm16)
{
    const qsizetype n = pcm16.size() / 2;
    if (n == 0)
        return 0.0;
    const auto *s = reinterpret_cast<const std::int16_t *>(pcm16.constData());
    double acc = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const double v = double(s[i]) / 32768.0;
        acc += v * v;
    }
    // Ordinary speech has an RMS around 0.05-0.15: scale so it shows on a meter.
    return qBound(0.0, std::sqrt(acc / double(n)) * 4.0, 1.0);
}

double peak(const QByteArray &pcm16)
{
    const qsizetype n = pcm16.size() / 2;
    const auto *s = reinterpret_cast<const std::int16_t *>(pcm16.constData());
    int m = 0;
    for (qsizetype i = 0; i < n; ++i)
        m = qMax(m, qAbs(int(s[i])));
    return double(m) / 32768.0;
}

double seconds(const QByteArray &pcm16)
{
    return double(pcm16.size()) / double(kBytesPerSecond);
}

bool isSilent(const QByteArray &pcm16)
{
    return pcm16.isEmpty() || peak(pcm16) < kSilentPeak;
}

QByteArray trimSilence(const QByteArray &pcm16)
{
    const int frame = kSampleRate / 50;   // 20 ms
    const qsizetype frames = pcm16.size() / 2 / frame;
    if (frames < 2)
        return pcm16;
    qsizetype first = -1, last = -1;
    for (qsizetype f = 0; f < frames; ++f) {
        const double l = level(pcm16.mid(f * frame * 2, frame * 2)) / 4.0;   // back to plain RMS
        if (l >= kSpeechFrame) {
            if (first < 0)
                first = f;
            last = f;
        }
    }
    if (first < 0)
        return pcm16;   // all silence: nothing to trim towards
    const qsizetype keep = (kKeepMs * kSampleRate / 1000) / frame + 1;
    const qsizetype from = qMax<qsizetype>(0, first - keep);
    const qsizetype to = qMin<qsizetype>(frames, last + 1 + keep);
    return pcm16.mid(from * frame * 2, (to - from) * frame * 2);
}

} // namespace voice
