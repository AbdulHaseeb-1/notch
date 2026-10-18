#include "BuddySfx.h"

#include <QAudioDevice>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QRandomGenerator>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kSampleRate = 44100;
constexpr double kTwoPi = 6.28318530717958647692;

// Playback pacing. A cue is never retriggered while its previous instance
// could still be sounding: restarting a playing QSoundEffect chops the sample
// off with a click. Every cue ends with a short fade to silence, so waiting
// until just before its end is enough.
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

QByteArray wrapWav(const QVector<qint16> &interleaved, int sampleRate, int channels)
{
    const quint32 dataSize = quint32(interleaved.size() * sizeof(qint16));
    const quint32 riffSize = 36 + dataSize;
    const quint16 audioFormat = 1; // PCM
    const quint16 numChannels = quint16(channels);
    const quint32 sr = quint32(sampleRate);
    const quint32 byteRate = quint32(sampleRate * channels * sizeof(qint16));
    const quint16 blockAlign = quint16(channels * sizeof(qint16));
    const quint16 bitsPerSample = 16;
    const quint32 fmtChunkSize = 16;

    QByteArray out;
    out.resize(44 + int(dataSize));
    char *p = out.data();

    std::memcpy(p + 0,  "RIFF", 4);
    std::memcpy(p + 4,  &riffSize, 4);
    std::memcpy(p + 8,  "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    std::memcpy(p + 16, &fmtChunkSize, 4);
    std::memcpy(p + 20, &audioFormat, 2);
    std::memcpy(p + 22, &numChannels, 2);
    std::memcpy(p + 24, &sr, 4);
    std::memcpy(p + 28, &byteRate, 4);
    std::memcpy(p + 32, &blockAlign, 2);
    std::memcpy(p + 34, &bitsPerSample, 2);
    std::memcpy(p + 36, "data", 4);
    std::memcpy(p + 40, &dataSize, 4);
    if (dataSize > 0)
        std::memcpy(p + 44, interleaved.constData(), dataSize);
    return out;
}

// Pitches used by the cues (a warm, pentatonic-friendly middle register).
constexpr double C4 = 261.63, E4 = 329.63, G4 = 392.00, A4 = 440.00;
constexpr double C5 = 523.25, D5 = 587.33, E5 = 659.26, G5 = 783.99, A5 = 880.00;
constexpr double Cs5 = 554.37, C6 = 1046.50;

} // namespace

BuddySfx::BuddySfx(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_recoveryTimer.setSingleShot(true);
    m_recoveryTimer.setInterval(500);
    connect(&m_recoveryTimer, &QTimer::timeout, this, &BuddySfx::recoverEffects);
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, [this] {
        m_recoveryAttempts = 0;
        scheduleRecovery();
    });
    rebuildEffects();
}

void BuddySfx::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (!on)
        stopEffects();
    emit enabledChanged();
}

void BuddySfx::setVolume(double v)
{
    const double clamped = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(m_volume, clamped))
        return;
    m_volume = clamped;
    if (clamped <= 0.001) {
        stopEffects();
    } else {
        for (QSoundEffect *fx : std::as_const(m_effects)) {
            if (fx)
                fx->setVolume(outputGain());
        }
    }
    emit volumeChanged();
}

void BuddySfx::stopEffects()
{
    for (QSoundEffect *fx : std::as_const(m_effects)) {
        if (fx)
            fx->stop();
    }
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
    rebuildEffects();
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

void BuddySfx::play(const QString &cue)
{
    if (!m_enabled || m_volume <= 0.001)
        return;

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
        if (now - m_lastSpeakMs > kSpeakRunGapMs)
            m_speakRunStartMs = now;
        m_lastSpeakMs = now;
        if (now - m_speakRunStartMs > kSpeakRunMaxMs)
            return;

        // Three soft syllables, never the same one twice in a row, so the
        // babble while an answer reveals sounds musical instead of mechanical.
        int pick = int(QRandomGenerator::global()->bounded(2));
        if (pick >= m_lastSpeakVariant)
            ++pick;
        m_lastSpeakVariant = pick;
        key = QStringLiteral("speak%1").arg(pick);
    }

    // Don't retrigger a cue while its previous instance could still be
    // sounding - a restart cuts the tail and reads as a click. Each cue waits
    // out its own length (its final stretch is already fading to silence).
    const qint64 gap = std::max(kMinCueGapMs, qint64(m_cueMs.value(key)) - kCueTailGraceMs);
    if (now - m_lastPlayMs.value(key, 0) < gap)
        return;
    m_lastPlayMs.insert(key, now);

    QSoundEffect *fx = m_effects.value(key, nullptr);
    if (!fx)
        fx = m_effects.value(QStringLiteral("wake"), nullptr);
    if (fx && fx->status() == QSoundEffect::Error) {
        qWarning() << "BuddySfx: effect is in error state for cue" << key
                   << "source" << fx->source();
        scheduleRecovery();
        return;
    }
    if (fx && fx->status() == QSoundEffect::Ready) {
        fx->setVolume(outputGain());
        fx->play();
    } else if (fx) {
        qWarning() << "BuddySfx: effect not ready for cue" << key
                   << "status" << fx->status() << "source" << fx->source();
    }
}

void BuddySfx::scheduleRecovery()
{
    if (m_recoveryAttempts >= 3)
        return;
    if (!m_recoveryTimer.isActive())
        m_recoveryTimer.start();
}

void BuddySfx::recoverEffects()
{
    ++m_recoveryAttempts;
    qInfo() << "BuddySfx: reinitializing effects after audio backend change/failure";
    stopEffects();
    for (QSoundEffect *fx : std::as_const(m_effects)) {
        if (fx)
            fx->deleteLater();
    }
    m_effects.clear();
    rebuildEffects();
}

void BuddySfx::rebuildEffects()
{
    const QString dirPath = QDir::temp().filePath(QStringLiteral("noch-buddy-sfx"));
    QDir().mkpath(dirPath);

    // Files from the previous synth ("buddy_<cue>_<style>.wav") are never
    // used again; don't leave them behind in the temp folder.
    QDir dir(dirPath);
    const QStringList legacy = dir.entryList({ QStringLiteral("buddy_*.wav") }, QDir::Files);
    for (const QString &name : legacy)
        dir.remove(name);

    const QStringList cues = {
        QStringLiteral("wake"),
        QStringLiteral("pop"),
        QStringLiteral("dismiss"),
        QStringLiteral("think"),
        QStringLiteral("reply"),
        QStringLiteral("happy"),
        QStringLiteral("drop"),
        QStringLiteral("speak0"),
        QStringLiteral("speak1"),
        QStringLiteral("speak2"),
        QStringLiteral("error")
    };

    const double pitch = pitchMultiplier();

    for (const QString &cue : cues) {
        // v2 in the name: never let a stale file from the old synth be reused.
        const QString filePath = QDir(dirPath).filePath(
            QStringLiteral("buddy2_%1_%2.wav").arg(cue, m_style));
        const QByteArray wav = synthesizeCue(cue, pitch);
        // Remember how long the cue sounds (44-byte WAV header, stereo
        // 16-bit frames) so play() can keep a retrigger from chopping off
        // the tail with a click.
        const int frames = int(std::max<qsizetype>(0, (wav.size() - 44) / (2 * int(sizeof(qint16)))));
        m_cueMs.insert(cue, frames * 1000 / kSampleRate);
        QFile f(filePath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(wav);
            f.close();
        }

        QSoundEffect *fx = new QSoundEffect(this);
        fx->setLoopCount(1);
        connect(fx, &QSoundEffect::statusChanged, this, [this, fx, cue] {
            if (fx->status() == QSoundEffect::Error) {
                qWarning() << "BuddySfx: failed to load cue" << cue
                           << "from" << fx->source();
                scheduleRecovery();
            }
        });
        m_effects.insert(cue, fx);
        fx->setSource(QUrl::fromLocalFile(filePath));
        fx->setVolume(outputGain());
    }
}

QByteArray BuddySfx::synthesizeCue(const QString &cue, double pitchScale)
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
        left[i] = dry + wet * tailL[i];
        right[i] = dry + wet * (i < tailR.size() ? tailR[i] : 0.0);
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    }

    // Trim the inaudible end of the tail so cues stay small and snappy.
    int last = frames - 1;
    const double floor = peak * 0.0012;
    while (last > 0 && std::abs(left[last]) < floor && std::abs(right[last]) < floor)
        --last;
    const int keep = std::min(frames, last + int(0.005 * kSampleRate));
    const int fade = std::min(keep, int(0.020 * kSampleRate));

    const double gain = level / peak;
    QVector<qint16> pcm(keep * 2);
    for (int i = 0; i < keep; ++i) {
        double g = gain;
        if (i >= keep - fade)
            g *= double(keep - i) / double(fade);
        pcm[2 * i]     = qint16(std::clamp(left[i] * g, -0.98, 0.98) * 32767.0);
        pcm[2 * i + 1] = qint16(std::clamp(right[i] * g, -0.98, 0.98) * 32767.0);
    }
    return wrapWav(pcm, kSampleRate, 2);
}
