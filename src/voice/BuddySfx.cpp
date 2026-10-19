#include "BuddySfx.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QIODevice>
#include <QMediaDevices>
#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>


namespace {

constexpr int kSampleRate = 44100;
constexpr double kTwoPi = 6.28318530717958647692;

// Playback pacing: the same cue is not stacked on itself in quick succession
// (see BuddySfx::admit; capped by kMaxCueGapMs below).
constexpr qint64 kCueTailGraceMs = 25;
constexpr qint64 kMinCueGapMs = 110;

// The soft syllables that ride along as an answer types stop themselves after
// a short run, so a long answer settles back to quiet instead of blipping all
// the way through. A real pause ends the run and re-arms it.
constexpr qint64 kSpeakRunMaxMs = 2400;
constexpr qint64 kSpeakRunGapMs = 400;

// One soft mallet strike. The voice is a rounded sine with a slightly detuned
// twin (slow beating reads as warmth rather than as a "beep"), plus a quiet
// octave partial that dies away much faster than the body - the way a felt
// mallet or a kalimba tine sounds, never a square-ish chirp.
struct Note {
    double freq;            // Hz, before the style's pitch scale
    double start;           // seconds from the cue's start
    double decay;           // seconds until the body has faded to about -40 dB
    double amp;             // relative level inside the cue
    double attack = 0.010;  // raised-cosine onset, never shorter than 4 ms
    double glideFrom = 1.0; // starting frequency ratio (e.g. 0.5 = an octave below)
    double glideTime = 0.0; // seconds to ease into the target pitch
    double bright = 0.22;   // level of the octave partial
    double vibratoHz = 0.0;
    double vibratoDepth = 0.0; // fraction of the frequency
};

void renderNote(QVector<double> &mix, const Note &n, double pitchScale)
{
    const int startSample = std::max(0, int(n.start * kSampleRate));
    const double tail = n.decay * 1.25 + n.attack;
    const int count = std::max(8, int(tail * kSampleRate));
    if (startSample + count > mix.size())
        mix.resize(startSample + count, 0.0);

    const double target = n.freq * pitchScale;
    const double attack = std::max(0.004, n.attack);
    const double tau = std::max(0.01, n.decay / 4.6);      // body
    const double tauHi = tau * 0.32;                         // octave partial
    const double fadeOut = std::min(0.025, tail * 0.2);

    double phA = 0.0, phB = 0.0, phH = 0.0;
    for (int i = 0; i < count; ++i) {
        const double sec = double(i) / kSampleRate;

        double freq = target;
        if (n.glideTime > 0.0 && sec < n.glideTime) {
            const double u = sec / n.glideTime;
            const double ease = 1.0 - std::pow(1.0 - u, 3.0);  // out-cubic
            freq = target * (n.glideFrom + (1.0 - n.glideFrom) * ease);
        }
        if (n.vibratoHz > 0.0)
            freq *= 1.0 + n.vibratoDepth * std::sin(kTwoPi * n.vibratoHz * sec)
                              * std::min(1.0, sec / 0.08);

        phA += kTwoPi * freq / kSampleRate;
        phB += kTwoPi * freq * 1.0023 / kSampleRate;        // ~ +4 cents
        phH += kTwoPi * freq * 2.0 / kSampleRate;

        double envA = sec < attack ? 0.5 * (1.0 - std::cos(M_PI * sec / attack)) : 1.0;
        const double body = std::exp(-std::max(0.0, sec - attack) / tau);
        const double hi = std::exp(-std::max(0.0, sec - attack) / tauHi);
        const double remain = tail - sec;
        if (remain < fadeOut)
            envA *= std::max(0.0, remain / fadeOut);

        const double wave = (0.72 * std::sin(phA) + 0.28 * std::sin(phB)) * body
                          + n.bright * std::sin(phH) * hi;
        mix[startSample + i] += n.amp * envA * wave;
    }
}

// Gentle one-pole low-pass: shaves off the brittle top so every cue sits
// warm and rounded, even through laptop speakers.
void warmFilter(QVector<double> &x, double cutoffHz)
{
    const double a = 1.0 - std::exp(-kTwoPi * cutoffHz / kSampleRate);
    double y = 0.0;
    for (double &s : x) {
        y += a * (s - y);
        s = y;
    }
}

// A tiny, soft "room": three damped combs into an all-pass per channel, with
// different delay sets left and right so the tail feels wide and airy. Kept
// short and low so the cues bloom a little instead of echoing.
QVector<double> roomTail(const QVector<double> &dry, const int (&delaysMs)[3], int allpassMs)
{
    const int extra = int(0.28 * kSampleRate);
    QVector<double> out(dry.size() + extra, 0.0);

    for (int d : delaysMs) {
        const int len = std::max(1, d * kSampleRate / 1000);
        QVector<double> buf(len, 0.0);
        int idx = 0;
        double lp = 0.0;
        for (int i = 0; i < out.size(); ++i) {
            const double in = i < dry.size() ? dry[i] : 0.0;
            const double delayed = buf[idx];
            lp += 0.35 * (delayed - lp);                       // damp the highs
            buf[idx] = in + lp * 0.62;
            idx = (idx + 1) % len;
            out[i] += delayed / 3.0;
        }
    }

    const int apLen = std::max(1, allpassMs * kSampleRate / 1000);
    QVector<double> ap(apLen, 0.0);
    int idx = 0;
    for (double &s : out) {
        const double b = ap[idx];
        const double v = s + b * 0.5;
        ap[idx] = v;
        s = b - v * 0.5;
        idx = (idx + 1) % apLen;
    }
    return out;
}

// Pitches used by the cues (a warm, pentatonic-friendly middle register).
constexpr double C4 = 261.63, E4 = 329.63, G4 = 392.00, A4 = 440.00;
constexpr double C5 = 523.25, D5 = 587.33, E5 = 659.26, G5 = 783.99, A5 = 880.00;
constexpr double Cs5 = 554.37, C6 = 1046.50;


// Upper bound for the "wait out the previous instance" pacing. The mixer
// overlaps voices without clicks, so a long cue (the reply arpeggio) must not
// swallow a legitimate repeat a second later; this only stops spam.
constexpr qint64 kMaxCueGapMs = 350;
constexpr int kMaxVoices = 8;

// Gentle limiter for when several voices overlap: transparent below 0.8,
// then eases towards 1.0 instead of hard clipping.
inline float softClip(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.8f)
        return x;
    const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}

} // namespace

// Pull-mode source for the QAudioSink: sums the active voices and writes them
// in whatever format the device asked for. readData() may run on the audio
// thread, so the voice list is guarded by a mutex (held for microseconds).
class SfxMixer final : public QIODevice
{
public:
    struct Voice {
        QSharedPointer<const QVector<float>> data; // interleaved stereo
        qsizetype frame = 0;
        float gain = 1.0f;
        int fadeLeft = -1;   // frames left of a forced fade-out, -1 = none
        int fadeLen = 1;
    };

    SfxMixer(const QAudioFormat &format, QObject *parent)
        : QIODevice(parent), m_format(format) {}

    void add(const QSharedPointer<const QVector<float>> &data, float gain)
    {
        if (!data || data->isEmpty())
            return;
        QMutexLocker lock(&m_mutex);
        if (m_voices.size() >= kMaxVoices) {
            // Fade the oldest out quickly instead of cutting it.
            Voice &old = m_voices.first();
            const int f = std::max(1, m_format.sampleRate() / 200);
            if (old.fadeLeft < 0 || old.fadeLeft > f) { old.fadeLeft = f; old.fadeLen = f; }
        }
        m_voices.append({ data, 0, gain, -1, 1 });
    }

    // Fade every voice to silence over ~ms (used for mute / volume 0).
    void fadeAll(int ms)
    {
        QMutexLocker lock(&m_mutex);
        const int f = std::max(1, m_format.sampleRate() * ms / 1000);
        for (Voice &v : m_voices)
            if (v.fadeLeft < 0 || v.fadeLeft > f) { v.fadeLeft = f; v.fadeLen = f; }
    }

    bool idle() const
    {
        QMutexLocker lock(&m_mutex);
        return m_voices.isEmpty();
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int ch = std::max(1, m_format.channelCount());
        const int bps = std::max(1, m_format.bytesPerSample());
        const qint64 frames = maxlen / (ch * bps);
        if (frames <= 0)
            return 0;
        m_scratch.assign(size_t(frames) * 2, 0.0f);
        {
            QMutexLocker lock(&m_mutex);
            for (auto it = m_voices.begin(); it != m_voices.end();) {
                Voice &v = *it;
                const qsizetype total = v.data->size() / 2;
                const float *src = v.data->constData();
                qint64 i = 0;
                for (; i < frames && v.frame < total; ++i, ++v.frame) {
                    float g = v.gain;
                    if (v.fadeLeft >= 0) {
                        if (v.fadeLeft == 0) { v.frame = total; break; }
                        g *= float(v.fadeLeft) / float(v.fadeLen);
                        --v.fadeLeft;
                    }
                    m_scratch[size_t(2 * i)] += src[2 * v.frame] * g;
                    m_scratch[size_t(2 * i + 1)] += src[2 * v.frame + 1] * g;
                }
                if (v.frame >= total)
                    it = m_voices.erase(it);
                else
                    ++it;
            }
        }
        for (qint64 i = 0; i < frames; ++i) {
            const float l = softClip(m_scratch[size_t(2 * i)]);
            const float r = softClip(m_scratch[size_t(2 * i + 1)]);
            for (int c = 0; c < ch; ++c) {
                const float s = ch == 1 ? 0.5f * (l + r) : (c == 0 ? l : (c == 1 ? r : 0.0f));
                char *out = data + (i * ch + c) * bps;
                switch (m_format.sampleFormat()) {
                case QAudioFormat::Float: { const float v = s; std::memcpy(out, &v, 4); break; }
                case QAudioFormat::Int32: { const qint32 v = qint32(std::clamp(s, -1.0f, 1.0f) * 2147483520.0f); std::memcpy(out, &v, 4); break; }
                case QAudioFormat::UInt8: { *reinterpret_cast<quint8 *>(out) = quint8(std::lround(128.0f + std::clamp(s, -1.0f, 1.0f) * 127.0f)); break; }
                default: { const qint16 v = qint16(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f)); std::memcpy(out, &v, 2); break; }
                }
            }
        }
        return frames * ch * bps;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QAudioFormat m_format;
    mutable QMutex m_mutex;
    QList<Voice> m_voices;
    std::vector<float> m_scratch;
};

BuddySfx::BuddySfx(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    // Never make noise from the offscreen screenshot harness or a CI run.
    m_deviceDisabled = qEnvironmentVariableIntValue("NOCH_NO_AUDIO") != 0
                       || QGuiApplication::platformName() == QLatin1String("offscreen");
    if (qEnvironmentVariableIntValue("NOCH_FORCE_AUDIO") != 0)   // diagnostics
        m_deviceDisabled = false;
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(25000);   // reopening stalls a frame, so hold the device a while
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (m_keepOpen || (m_mixer && !m_mixer->idle()))
            m_idleTimer.start();
        else
            dropSink();     // release the device while the buddy is quiet
    });
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, [this] {
        // Default device switched, headphones plugged, etc.: reopen lazily on
        // the next cue, in that device's own format.
        dropSink();
        m_formatReady = false;
        m_device = QAudioDevice();
        m_failures = 0;
        m_retryAfterMs = 0;
        const bool avail = !QMediaDevices::defaultAudioOutput().isNull();
        if (avail != m_outputAvailable) {
            m_outputAvailable = avail;
            emit outputChanged();
        }
    });
    // Cues used to be written to %TEMP%\noch-buddy-sfx as WAV files for
    // QSoundEffect; they are rendered in memory now, so tidy that folder up.
    QDir(QDir::temp().filePath(QStringLiteral("noch-buddy-sfx"))).removeRecursively();
    rebuildCues();
}

BuddySfx::~BuddySfx()
{
    dropSink();
}

QStringList BuddySfx::cueNames()
{
    return { QStringLiteral("wake"), QStringLiteral("pop"), QStringLiteral("dismiss"),
             QStringLiteral("think"), QStringLiteral("reply"), QStringLiteral("happy"),
             QStringLiteral("drop"), QStringLiteral("speak0"), QStringLiteral("speak1"),
             QStringLiteral("speak2"), QStringLiteral("error") };
}

void BuddySfx::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (!on && m_mixer)
        m_mixer->fadeAll(12);   // a short fade, never a click
    emit enabledChanged();
}

void BuddySfx::setKeepOpen(bool on)
{
    if (m_keepOpen == on)
        return;
    m_keepOpen = on;
    if (on)
        prewarm();
    emit keepOpenChanged();
}

void BuddySfx::setVolume(double v)
{
    const double clamped = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
    if (qFuzzyCompare(1.0 + m_volume, 1.0 + clamped))
        return;
    m_volume = clamped;
    if (clamped <= 0.001 && m_mixer)
        m_mixer->fadeAll(12);
    emit volumeChanged();
}

// The slider is perceptual: loudness grows roughly with the square of the
// amplitude, so a plain linear mapping crams all the useful range into the
// bottom quarter of the slider.
double BuddySfx::outputGain() const
{
    return std::pow(m_volume, 1.7);
}

void BuddySfx::setStyle(const QString &s)
{
    const QString norm = (s == QLatin1String("cute") || s == QLatin1String("mellow"))
                         ? s
                         : QStringLiteral("warm");
    if (m_style == norm)
        return;
    m_style = norm;
    rebuildCues();
    emit styleChanged();
}

double BuddySfx::pitchMultiplier() const
{
    if (m_style == QLatin1String("cute"))
        return 1.189;   // +3 semitones
    if (m_style == QLatin1String("mellow"))
        return 0.841;   // -3 semitones
    return 1.0;
}

void BuddySfx::rebuildCues()
{
    const double pitch = pitchMultiplier();
    m_cues.clear();
    m_deviceCues.clear();
    for (const QString &cue : cueNames()) {
        auto data = QSharedPointer<const QVector<float>>::create(renderCue(cue, pitch));
        m_cueMs.insert(cue, int(qint64(data->size() / 2) * 1000 / kSynthRate));
        m_cues.insert(cue, data);
        if (m_deviceCueRate > 0)
            m_deviceCues.insert(cue, QSharedPointer<const QVector<float>>::create(
                                         resampleStereo(*data, kSynthRate, m_deviceCueRate)));
    }
}

bool BuddySfx::admit(const QString &key, qint64 nowMs)
{
    // Don't stack the same cue on itself in quick succession (it would read
    // as spam / a flam), but never wait longer than kMaxCueGapMs.
    const qint64 gap = std::clamp(qint64(m_cueMs.value(key)) - kCueTailGraceMs,
                                  kMinCueGapMs, kMaxCueGapMs);
    const auto last = m_lastPlayMs.constFind(key);
    if (last != m_lastPlayMs.constEnd() && nowMs - *last < gap)
        return false;
    m_lastPlayMs.insert(key, nowMs);
    return true;
}

bool BuddySfx::play(const QString &cue)
{
    if (!m_enabled || m_volume <= 0.001)
        return false;

    QString key = (cue == QLatin1String("open"))    ? QStringLiteral("wake")
                : (cue == QLatin1String("success")) ? QStringLiteral("reply")
                : (cue == QLatin1String("love"))    ? QStringLiteral("happy")
                : cue;

    const qint64 now = m_clock.elapsed();

    if (key == QLatin1String("speak")) {
        // Keep the run of syllables short: after a couple of seconds of
        // continuous chatter, stay quiet for the rest of the answer. Only a
        // real pause (the reveal is over) re-arms the run. Every attempt
        // counts as activity, so a long reveal can't creep the run back on.
        if (m_lastSpeakMs < 0 || now - m_lastSpeakMs > kSpeakRunGapMs)
            m_speakRunStartMs = now;
        m_lastSpeakMs = now;
        if (now - m_speakRunStartMs > kSpeakRunMaxMs)
            return false;
        // All three syllables share one pacing slot.
        if (!admit(QStringLiteral("speak0"), now))
            return false;
        int pick = int(QRandomGenerator::global()->bounded(2));
        if (pick >= m_lastSpeakVariant)
            ++pick;
        m_lastSpeakVariant = pick;
        key = QStringLiteral("speak%1").arg(pick);
    } else {
        if (!m_cues.contains(key))
            return false;
        if (!admit(key, now))
            return false;
    }

    ++m_voicesStarted;
    if (!ensureSink())
        return true;        // let through, but there is no device to play on
    m_mixer->add(m_deviceCues.value(key), float(outputGain()));
    m_idleTimer.start();
    return true;
}

// Finds the default output and the format it mixes in natively, and converts
// the cues to that rate. Both are remembered until the device list changes:
// asking the platform for them again on every reopen is most of what made the
// first cue after a quiet spell stall the UI.
bool BuddySfx::resolveFormat()
{
    if (m_formatReady && !m_device.isNull())
        return true;

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        if (m_outputAvailable) {
            m_outputAvailable = false;
            emit outputChanged();
        }
        return false;
    }

    // Ask for what the device mixes in natively (WASAPI shared mode only
    // accepts its own mix format), preferring float, then 16-bit.
    QAudioFormat fmt = dev.preferredFormat();
    if (fmt.sampleRate() <= 0)
        fmt.setSampleRate(48000);
    if (fmt.channelCount() <= 0)
        fmt.setChannelCount(2);
    QAudioFormat f = fmt;
    f.setSampleFormat(QAudioFormat::Float);
    if (dev.isFormatSupported(f)) {
        fmt = f;
    } else {
        f.setSampleFormat(QAudioFormat::Int16);
        if (dev.isFormatSupported(f) || fmt.sampleFormat() == QAudioFormat::Unknown)
            fmt = f;
    }
    m_format = fmt;
    m_device = dev;

    if (m_deviceCueRate != fmt.sampleRate()) {
        m_deviceCueRate = fmt.sampleRate();
        m_deviceCues.clear();
        for (auto it = m_cues.cbegin(); it != m_cues.cend(); ++it)
            m_deviceCues.insert(it.key(), QSharedPointer<const QVector<float>>::create(
                                              resampleStereo(*it.value(), kSynthRate, m_deviceCueRate)));
    }
    m_formatReady = true;
    return true;
}

void BuddySfx::prewarm()
{
    if (!m_enabled || m_volume <= 0.001 || m_deviceDisabled)
        return;
    if (ensureSink())
        m_idleTimer.start();
}

bool BuddySfx::ensureSink()
{
    if (m_sink && m_mixer)
        return true;
    if (m_deviceDisabled)
        return false;
    const qint64 now = m_clock.elapsed();
    if (now < m_retryAfterMs)
        return false;

    if (!resolveFormat())
        return false;
    const QAudioDevice dev = m_device;
    const QAudioFormat fmt = m_format;

    auto fail = [this, now](const char *why) {
        qWarning() << "BuddySfx: cannot open sound output:" << why << m_format;
        dropSink();
        m_formatReady = false;      // negotiate again on the retry
        ++m_failures;
        m_retryAfterMs = now + std::min<qint64>(30000, 500LL << std::min(m_failures, 6));
        return false;
    };

    m_mixer = new SfxMixer(fmt, this);
    m_mixer->open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(dev, fmt, this);
    m_sink->setBufferSize(fmt.bytesForDuration(80000));   // ~80 ms: low latency, no underruns
    QAudioSink *sink = m_sink;
    connect(m_sink, &QAudioSink::stateChanged, this, [this, sink](QAudio::State st) {
        if (sink != m_sink)
            return;
        const QAudio::Error err = sink->error();
        if (st == QAudio::StoppedState && err != QAudio::NoError && err != QAudio::UnderrunError) {
            qWarning() << "BuddySfx: sound output stopped with error" << err;
            m_formatReady = false;
            ++m_failures;
            m_retryAfterMs = m_clock.elapsed() + std::min<qint64>(30000, 500LL << std::min(m_failures, 6));
            QMetaObject::invokeMethod(this, [this, sink] { if (sink == m_sink) dropSink(); },
                                      Qt::QueuedConnection);
        }
    });
    m_sink->start(m_mixer);
    if (m_sink->error() != QAudio::NoError && m_sink->error() != QAudio::UnderrunError)
        return fail("start failed");
    if (m_sink->state() == QAudio::StoppedState)
        return fail("device refused the format");
    m_failures = 0;
    if (!m_outputAvailable) {
        m_outputAvailable = true;
        emit outputChanged();
    }
    return true;
}

void BuddySfx::dropSink()
{
    m_idleTimer.stop();
    if (m_sink) {
        QAudioSink *s = m_sink;
        m_sink = nullptr;
        s->stop();
        delete s;
    }
    if (m_mixer) {
        m_mixer->close();
        delete m_mixer;
        m_mixer = nullptr;
    }
}

QVector<float> BuddySfx::resampleStereo(const QVector<float> &in, int fromRate, int toRate)
{
    if (fromRate <= 0 || toRate <= 0 || fromRate == toRate || in.size() < 4)
        return in;
    const qsizetype inFrames = in.size() / 2;
    const qsizetype outFrames = std::max<qsizetype>(1, qsizetype(double(inFrames) * toRate / fromRate));
    QVector<float> out(outFrames * 2);
    const double step = double(fromRate) / toRate;
    for (qsizetype i = 0; i < outFrames; ++i) {
        const double pos = i * step;
        const qsizetype a = std::min<qsizetype>(qsizetype(pos), inFrames - 1);
        const qsizetype b = std::min<qsizetype>(a + 1, inFrames - 1);
        const float t = float(pos - double(a));
        out[2 * i] = in[2 * a] + (in[2 * b] - in[2 * a]) * t;
        out[2 * i + 1] = in[2 * a + 1] + (in[2 * b + 1] - in[2 * a + 1]) * t;
    }
    return out;
}

QVector<float> BuddySfx::renderCue(const QString &cue, double pitchScale)
{
    QVector<double> mix;
    double level = 0.42;      // peak after normalising (≈ -7.5 dBFS)
    double wet = 0.16;        // reverb send
    double cutoff = 3800.0;   // warmth filter

    if (cue == QLatin1String("wake")) {
        // A soft bubble "bloop" that blossoms into a gentle rising third.
        renderNote(mix, { G4, 0.000, 0.16, 0.85, 0.006, 0.62, 0.050, 0.10 }, pitchScale);
        renderNote(mix, { C5, 0.055, 0.42, 0.55, 0.012, 1.0, 0.0, 0.20 }, pitchScale);
        renderNote(mix, { E5, 0.115, 0.55, 0.50, 0.014, 1.0, 0.0, 0.18 }, pitchScale);
        level = 0.40;
    } else if (cue == QLatin1String("pop")) {
        // Tiny water-drop pop for the trailing thought dots.
        renderNote(mix, { A4, 0.000, 0.11, 1.0, 0.005, 0.55, 0.045, 0.08 }, pitchScale);
        level = 0.24;
        wet = 0.10;
    } else if (cue == QLatin1String("dismiss")) {
        // The same bubble folding back in: a soft falling pair.
        renderNote(mix, { E5, 0.000, 0.30, 0.55, 0.012, 1.0, 0.0, 0.14 }, pitchScale);
        renderNote(mix, { G4, 0.070, 0.36, 0.60, 0.014, 1.06, 0.060, 0.10 }, pitchScale);
        level = 0.30;
    } else if (cue == QLatin1String("think")) {
        // A hushed, breathing "mm-hm": two notes swell in slowly together.
        renderNote(mix, { C5, 0.000, 0.80, 0.60, 0.070, 1.0, 0.0, 0.06, 4.8, 0.004 }, pitchScale);
        renderNote(mix, { G4, 0.040, 0.85, 0.45, 0.090, 1.0, 0.0, 0.05, 4.2, 0.004 }, pitchScale);
        renderNote(mix, { E5, 0.160, 0.70, 0.30, 0.080, 1.0, 0.0, 0.05 }, pitchScale);
        level = 0.26;
        wet = 0.22;
        cutoff = 3000.0;
    } else if (cue == QLatin1String("reply")) {
        // "Here you go": a warm, unhurried major arpeggio over a soft low body.
        renderNote(mix, { C4, 0.000, 0.70, 0.35, 0.020, 1.0, 0.0, 0.10 }, pitchScale);
        renderNote(mix, { E5, 0.010, 0.55, 0.55, 0.010, 1.0, 0.0, 0.22 }, pitchScale);
        renderNote(mix, { G5, 0.085, 0.60, 0.48, 0.010, 1.0, 0.0, 0.20 }, pitchScale);
        renderNote(mix, { C6, 0.170, 0.80, 0.40, 0.012, 1.0, 0.0, 0.16 }, pitchScale);
        level = 0.38;
        wet = 0.20;
    } else if (cue == QLatin1String("happy")) {
        // A little pentatonic giggle that rises and softens as it goes.
        const double run[] = { C5, D5, E5, G5, A5 };
        for (int i = 0; i < 5; ++i)
            renderNote(mix, { run[i], 0.052 * i, 0.30 + 0.06 * i, 0.70 - 0.08 * i,
                              0.008, 0.97, 0.020, 0.20 }, pitchScale);
        level = 0.36;
        wet = 0.18;
    } else if (cue == QLatin1String("drop")) {
        // Cushioned "thup" when set down after a drag.
        renderNote(mix, { E4, 0.000, 0.16, 1.0, 0.006, 1.25, 0.035, 0.06 }, pitchScale);
        renderNote(mix, { G4, 0.040, 0.22, 0.35, 0.010, 1.0, 0.0, 0.10 }, pitchScale);
        level = 0.32;
        wet = 0.10;
        cutoff = 2600.0;
    } else if (cue.startsWith(QLatin1String("speak"))) {
        // Soft syllable blips used while an answer is revealed.
        const double tones[] = { E5, G5, A5 };
        const int v = std::clamp(cue.mid(5).toInt(), 0, 2);
        renderNote(mix, { tones[v], 0.000, 0.085, 1.0, 0.006, 0.94, 0.018, 0.08 }, pitchScale);
        level = 0.11;
        wet = 0.08;
        cutoff = 3200.0;
    } else if (cue == QLatin1String("error")) {
        // A sympathetic "aww": a gently sagging minor third, never a buzzer.
        renderNote(mix, { E5, 0.000, 0.42, 0.55, 0.016, 1.0, 0.0, 0.12, 5.0, 0.006 }, pitchScale);
        renderNote(mix, { Cs5, 0.120, 0.60, 0.60, 0.018, 1.0, 0.0, 0.10, 4.5, 0.008 }, pitchScale);
        renderNote(mix, { A4, 0.135, 0.65, 0.30, 0.030, 1.0, 0.0, 0.05 }, pitchScale);
        level = 0.30;
        wet = 0.20;
        cutoff = 3000.0;
    }


    if (mix.isEmpty())
        mix.resize(64, 0.0);

    warmFilter(mix, cutoff);

    static const int delaysL[3] = { 29, 37, 43 };
    static const int delaysR[3] = { 31, 41, 47 };
    const QVector<double> tailL = roomTail(mix, delaysL, 7);
    const QVector<double> tailR = roomTail(mix, delaysR, 9);

    const int frames = tailL.size();
    QVector<double> left(frames, 0.0), right(frames, 0.0);
    double peak = 1e-9;
    for (int i = 0; i < frames; ++i) {
        const double dry = i < mix.size() ? mix[i] : 0.0;
        double l = dry + wet * tailL[i];
        double r = dry + wet * (i < tailR.size() ? tailR[i] : 0.0);
        if (!std::isfinite(l)) l = 0.0;
        if (!std::isfinite(r)) r = 0.0;
        left[i] = l;
        right[i] = r;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }

    // Trim the inaudible end of the tail so cues stay small and snappy.
    int last = frames - 1;
    const double floor = peak * 0.0012;
    while (last > 0 && std::abs(left[last]) < floor && std::abs(right[last]) < floor)
        --last;
    const int keep = std::min(frames, last + int(0.005 * kSampleRate));
    const int fade = std::max(1, std::min(keep, int(0.020 * kSampleRate)));
    const int fadeIn = std::max(1, std::min(keep / 4, int(0.002 * kSampleRate)));

    // Normalise to the cue's level, with a 2 ms safety fade-in and a 20 ms
    // fade-out so the buffer starts and ends exactly at silence (no clicks).
    const double gain = level / peak;
    QVector<float> out(keep * 2);
    for (int i = 0; i < keep; ++i) {
        double g = gain;
        if (i < fadeIn)
            g *= double(i) / double(fadeIn);
        if (i >= keep - fade)
            g *= double(keep - 1 - i) / double(fade);
        out[2 * i]     = float(std::clamp(left[i] * g, -level, level));
        out[2 * i + 1] = float(std::clamp(right[i] * g, -level, level));
    }
    return out;
}
