#include "EchoCanceller.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kBlock = 160;                     // 10 ms: adaptation is decided per block
constexpr long kLearnSamples = 3 * EchoCanceller::kRate;   // adapt freely for the first 3 s of reference
constexpr double kMuLearn = 0.35;
constexpr double kMuTrack = 0.15;
constexpr double kEps = 0.01;
constexpr double kFarMin = 2e-6;                // block power of the reference below which it counts as silence
constexpr double kNearRatio = 3.0;              // the microphone this far above the expected echo = user voice
constexpr double kNearFloor = 2e-5;
constexpr int kNearRelearnBlocks = 300;         // 3 s of that: the room probably changed, learn again
constexpr size_t kMaxPending = 6400;            // 400 ms: more than the device buffers; older reference is stale

} // namespace

EchoCanceller::EchoCanceller()
    : m_w(kTaps, 0.0f)
    , m_x(2 * kTaps, 0.0f)
{
}

void EchoCanceller::reset()
{
    std::fill(m_w.begin(), m_w.end(), 0.0f);
    std::fill(m_x.begin(), m_x.end(), 0.0f);
    m_pos = 0;
    m_energy = 0.0;
    m_pending.clear();
    m_quiet = kTaps;
    m_farActive = 0;
    m_nearBlocks = 0;
    m_pdSm = m_peSm = 0.0;
    m_erleDb = 0.0;
    m_running = false;
}

void EchoCanceller::clearReference()
{
    m_pending.clear();
}

void EchoCanceller::pushReference(const QByteArray &pcm16)
{
    const qsizetype n = pcm16.size() / 2;
    const auto *s = reinterpret_cast<const qint16 *>(pcm16.constData());
    for (qsizetype i = 0; i < n; ++i)
        m_pending.push_back(float(s[i]) / 32768.0f);
    while (m_pending.size() > kMaxPending)
        m_pending.pop_front();
}

void EchoCanceller::pushSample(float x)
{
    m_pos = (m_pos - 1 + kTaps) % kTaps;
    const float old = m_x[m_pos];                 // the slot being reused held the oldest sample
    m_x[m_pos] = x;
    m_x[m_pos + kTaps] = x;
    m_energy += double(x) * x - double(old) * old;
    if (m_energy < 0.0)
        m_energy = 0.0;
}

float EchoCanceller::dot() const
{
    const float *w = m_w.data();
    const float *x = m_x.data() + m_pos;
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    for (int k = 0; k < kTaps; k += 8) {
        a0 += w[k] * x[k];
        a1 += w[k + 1] * x[k + 1];
        a2 += w[k + 2] * x[k + 2];
        a3 += w[k + 3] * x[k + 3];
        a4 += w[k + 4] * x[k + 4];
        a5 += w[k + 5] * x[k + 5];
        a6 += w[k + 6] * x[k + 6];
        a7 += w[k + 7] * x[k + 7];
    }
    return ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7));
}

QByteArray EchoCanceller::process(const QByteArray &mic16)
{
    const qsizetype n = mic16.size() / 2;
    // Nothing playing, nothing queued and the history all zero: the microphone passes untouched.
    if (m_pending.empty() && m_quiet >= kTaps) {
        m_running = false;
        return mic16;
    }
    m_running = true;

    QByteArray out(mic16.size(), Qt::Uninitialized);
    const auto *d = reinterpret_cast<const qint16 *>(mic16.constData());
    auto *o = reinterpret_cast<qint16 *>(out.data());

    float ref[kBlock];
    for (qsizetype b = 0; b < n; b += kBlock) {
        const int len = int(std::min<qsizetype>(kBlock, n - b));

        // The reference for this block, and how loud it and the microphone are.
        double pr = 0.0, pd = 0.0;
        for (int i = 0; i < len; ++i) {
            float r = 0.0f;
            if (!m_pending.empty()) {
                r = m_pending.front();
                m_pending.pop_front();
            }
            ref[i] = r;
            pr += double(r) * r;
            const double m = double(d[b + i]) / 32768.0;
            pd += m * m;
        }
        pr /= len;
        pd /= len;
        const bool farActive = pr > kFarMin;
        if (farActive) {
            m_quiet = 0;
            m_farActive += len;
        } else {
            m_quiet += len;
        }

        // Adapt while the speakers play, unless the microphone is far louder than the echo
        // the filter expects: that is the user, and learning it would teach the filter to
        // cancel their voice.
        const bool learning = m_farActive < kLearnSamples;
        bool adapt = farActive;
        if (farActive && !learning) {
            double wnorm = 0.0;
            for (int k = 0; k < kTaps; ++k)
                wnorm += double(m_w[k]) * m_w[k];
            const bool near = pd > kNearRatio * (wnorm * pr) + kNearFloor;
            if (near) {
                adapt = false;
                if (++m_nearBlocks >= kNearRelearnBlocks) {
                    m_nearBlocks = 0;
                    m_farActive = kLearnSamples - kRate;        // a second of free learning
                }
            } else {
                m_nearBlocks = 0;
            }
        }
        const double mu = learning ? kMuLearn : kMuTrack;

        double pe = 0.0;
        for (int i = 0; i < len; ++i) {
            pushSample(ref[i]);
            const float y = dot();
            const float dn = float(d[b + i]) / 32768.0f;
            const float e = dn - y;
            pe += double(e) * e;
            const float c = std::clamp(e, -1.0f, 1.0f);
            o[b + i] = qint16(std::lround(std::clamp(c, -1.0f, 1.0f) * 32767.0f));
            if (adapt) {
                const float g = float(mu) * e / float(m_energy + kEps);
                float *w = m_w.data();
                const float *x = m_x.data() + m_pos;
                for (int k = 0; k < kTaps; ++k)
                    w[k] += g * x[k];
            }
        }
        pe /= len;

        // How much of the microphone the filter explains, while only the speakers are playing.
        if (farActive && adapt) {
            m_pdSm = 0.9 * m_pdSm + 0.1 * pd;
            m_peSm = 0.9 * m_peSm + 0.1 * pe;
            if (m_pdSm > 1e-9) {
                const double erle = std::clamp(10.0 * std::log10(m_pdSm / (m_peSm + 1e-12)), 0.0, 40.0);
                m_erleDb = 0.97 * m_erleDb + 0.03 * erle;
            }
        }
    }
    return out;
}
