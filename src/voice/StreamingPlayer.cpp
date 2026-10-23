#include "StreamingPlayer.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QGuiApplication>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kPumpMs = 10;
constexpr int kDeviceBufferMs = 120;
constexpr qint64 kLevelEmitMs = 60;
constexpr qint64 kDryHoldMs = 90;

double chunkLevel(const QByteArray &pcm16)
{
    const qsizetype n = pcm16.size() / 2;
    if (n <= 0)
        return 0.0;
    const auto *s = reinterpret_cast<const qint16 *>(pcm16.constData());
    double sum = 0.0;
    for (qsizetype i = 0; i < n; ++i)
        sum += double(s[i]) * s[i];
    // Scaled like VoiceAudio's level(): ordinary speech sits well above 0.1.
    return std::min(1.0, std::sqrt(sum / double(n)) / 32768.0 * 4.0);
}

} // namespace

StreamingPlayer::StreamingPlayer(QObject *parent)
    : QObject(parent)
{
    m_deviceDisabled = qEnvironmentVariableIntValue("NOCH_NO_AUDIO") != 0
                       || QGuiApplication::platformName() == QLatin1String("offscreen");
    m_timer.setInterval(kPumpMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &StreamingPlayer::pump);
    m_clock.start();
}

StreamingPlayer::~StreamingPlayer()
{
    closeDevice();
}

void StreamingPlayer::setInputRate(int hz)
{
    if (hz < 8000 || hz > 192000 || hz == m_inRate)
        return;
    m_inRate = hz;
    m_phase = 0.0;
}

void StreamingPlayer::setVolume(double v)
{
    m_volume = std::clamp(v, 0.0, 1.5);
}

void StreamingPlayer::setDeviceDisabled(bool off)
{
    if (off == m_deviceDisabled)
        return;
    flush();
    m_deviceDisabled = off;
}

QByteArray StreamingPlayer::resample(const QByteArray &pcm16, int inRate, const QAudioFormat &to, double volume,
                                     double *phase, qint16 *last)
{
    const qsizetype inFrames = pcm16.size() / 2;
    const int outRate = std::max(8000, to.sampleRate());
    const int outCh = std::max(1, to.channelCount());
    const int bps = std::max(1, to.bytesPerSample());
    if (inFrames <= 0 || inRate <= 0)
        return QByteArray();
    const auto *in = reinterpret_cast<const qint16 *>(pcm16.constData());
    const double step = double(inRate) / outRate;
    double p = phase ? *phase : 0.0;
    const qint16 prev = last ? *last : 0;
    const float gain = float(volume);

    QByteArray out;
    out.reserve(qsizetype(double(inFrames) / step + 2) * outCh * bps);
    auto sample = [&](qsizetype idx) -> float {
        return float(idx < 0 ? prev : in[idx]) / 32768.0f;
    };
    // An output frame needs input frames a and a+1; the chunk's last input frame
    // waits for the next chunk (it becomes that chunk's `last`).
    while (p < double(inFrames - 1)) {
        const qsizetype a = qsizetype(std::floor(p));
        const float t = float(p - double(a));
        const float v = std::clamp((sample(a) + (sample(a + 1) - sample(a)) * t) * gain, -1.0f, 1.0f);
        char frame[32];
        for (int c = 0; c < outCh; ++c) {
            char *at = frame + c * bps;
            switch (to.sampleFormat()) {
            case QAudioFormat::Float: std::memcpy(at, &v, 4); break;
            case QAudioFormat::Int32: { const qint32 s = qint32(v * 2147483520.0f); std::memcpy(at, &s, 4); break; }
            case QAudioFormat::UInt8: *reinterpret_cast<quint8 *>(at) = quint8(std::lround(128.0f + v * 127.0f)); break;
            default: { const qint16 s = qint16(std::lround(v * 32767.0f)); std::memcpy(at, &s, 2); break; }
            }
        }
        out.append(frame, outCh * bps);
        p += step;
    }
    if (phase)
        *phase = p - double(inFrames);
    if (last)
        *last = in[inFrames - 1];
    return out;
}

QByteArray StreamingPlayer::toReference16k(const char *data, qint64 bytes, const QAudioFormat &format,
                                           ReferenceState *state)
{
    const int bps = format.bytesPerSample();
    const int ch = format.channelCount();
    const int rate = format.sampleRate();
    if (!data || !state || bps <= 0 || ch <= 0 || rate <= 0)
        return QByteArray();
    const qint64 frames = bytes / (qint64(bps) * ch);
    const double step = 16000.0 / rate;
    QByteArray out;
    out.reserve(qsizetype(double(frames) * step * 2.0) + 4);
    for (qint64 f = 0; f < frames; ++f) {
        float v = 0.0f;
        for (int c = 0; c < ch; ++c) {
            const char *at = data + (f * ch + c) * bps;
            switch (format.sampleFormat()) {
            case QAudioFormat::Float: { float x; std::memcpy(&x, at, 4); v += x; break; }
            case QAudioFormat::Int32: { qint32 x; std::memcpy(&x, at, 4); v += float(double(x) / 2147483648.0); break; }
            case QAudioFormat::UInt8: v += (float(*reinterpret_cast<const quint8 *>(at)) - 128.0f) / 128.0f; break;
            default: { qint16 x; std::memcpy(&x, at, 2); v += float(x) / 32768.0f; break; }
            }
        }
        state->acc += v / float(ch);
        ++state->count;
        state->phase += step;
        if (state->phase >= 1.0) {
            state->phase -= 1.0;
            const float o = std::clamp(state->acc / float(state->count), -1.0f, 1.0f);
            const qint16 s = qint16(std::lround(o * 32767.0f));
            out.append(reinterpret_cast<const char *>(&s), 2);
            state->acc = 0.0f;
            state->count = 0;
        }
    }
    return out;
}

bool StreamingPlayer::openDevice()
{
    if (m_sink)
        return true;
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull())
        return false;
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
    m_sink = new QAudioSink(dev, fmt, this);
    m_deviceBufferBytes = int(fmt.bytesForDuration(qint64(kDeviceBufferMs) * 1000));
    m_sink->setBufferSize(m_deviceBufferBytes);
    m_io = m_sink->start();
    if (!m_io || m_sink->error() != QAudio::NoError) {
        closeDevice();
        return false;
    }
    return true;
}

void StreamingPlayer::closeDevice()
{
    m_io = nullptr;
    if (m_sink) {
        QAudioSink *s = m_sink;
        m_sink = nullptr;
        s->stop();
        delete s;
    }
}

void StreamingPlayer::setActive(bool on)
{
    if (on == m_active)
        return;
    m_active = on;
    if (on)
        m_timer.start();
    else
        m_timer.stop();
}

void StreamingPlayer::write(const QByteArray &pcm16)
{
    if (pcm16.size() < 2)
        return;
    const qint64 frames = pcm16.size() / 2;
    m_totalFrames += frames;
    m_level = std::max(m_level, chunkLevel(pcm16));
    const qint64 chunkMs = qint64(double(frames) * 1000.0 / m_inRate);
    if (!m_active) {
        // Audio after a silence: hold it back until there is enough to play without gaps.
        m_started = false;
        m_bufferedMs = 0;
        m_prebufferDeadline = m_clock.elapsed() + kPrebufferMaxWaitMs;
    }
    m_bufferedMs += chunkMs;
    if (m_deviceDisabled) {
        // The clock of a pretend device starts when playback does.
        m_simPendingMs += chunkMs;
        if (m_started)
            m_simEndMs = std::max(m_simEndMs, m_clock.elapsed()) + chunkMs;
        setActive(true);
        if (!m_started && m_bufferedMs >= m_targetMs)
            beginPlayback();
        return;
    }
    if (!m_sink && !openDevice()) {
        emit failed(tr("The speakers could not be opened."));
        return;
    }
    const QByteArray converted = resample(pcm16, m_inRate, m_format, m_volume, &m_phase, &m_last);
    m_queue += converted;
    m_queuedFrames = m_queue.size() / std::max(1, m_format.bytesPerFrame());
    setActive(true);
    if (!m_started && m_bufferedMs >= m_targetMs)
        beginPlayback();
    pump();
}

// The prebuffer is full (or the wait ran out): audio may flow to the device.
void StreamingPlayer::beginPlayback()
{
    if (m_started)
        return;
    m_started = true;
    if (m_deviceDisabled) {
        m_simEndMs = m_clock.elapsed() + m_simPendingMs;
        m_simPendingMs = 0;
    }
    emit playbackStarted();
}

void StreamingPlayer::flush()
{
    m_streaming = false;
    m_ref = ReferenceState();
    emit referenceReset();
    m_started = false;
    m_bufferedMs = 0;
    m_simPendingMs = 0;
    m_queue.clear();
    m_queuedFrames = 0;
    m_phase = 0.0;
    m_last = 0;
    m_simEndMs = 0;
    closeDevice();
    m_dryStart = -1;
    setActive(false);
    if (m_level != 0.0) {
        m_level = 0.0;
        emit levelChanged();
    }
}

void StreamingPlayer::setStreaming(bool on)
{
    m_streaming = on;
    // The answer is complete: audio still waiting for a jitter buffer that will never fill plays now.
    if (!on && m_active && !m_started)
        beginPlayback();
}

// The stream ran dry while the answer was still coming: play silence and build a fresh
// jitter buffer, instead of ending the answer or stuttering through the gap.
void StreamingPlayer::rebuffer(qint64 now)
{
    m_started = false;
    m_bufferedMs = 0;
    m_simPendingMs = 0;
    m_simEndMs = 0;
    m_dryStart = -1;
    m_prebufferDeadline = now + kRebufferMaxWaitMs;
    // A real underrun: keep a bigger cushion until answers go through clean again.
    m_targetMs = std::min(kMaxPrebufferMs, m_targetMs + kPrebufferStepMs);
    m_underran = true;
    ++m_underrunCount;
    emit underrun(m_targetMs);
    if (m_level != 0.0) {
        m_level = 0.0;
        emit levelChanged();
    }
}

double StreamingPlayer::bufferedSeconds() const
{
    if (m_deviceDisabled)
        return (std::max<qint64>(0, m_simEndMs - m_clock.elapsed()) + m_simPendingMs) / 1000.0;
    if (!m_started)
        return double(m_bufferedMs) / 1000.0;
    const int bpf = std::max(1, m_format.bytesPerFrame());
    const int rate = std::max(1, m_format.sampleRate());
    double seconds = double(m_queue.size()) / bpf / rate;
    if (m_sink)
        seconds += double(std::max<qint64>(0, qint64(m_deviceBufferBytes) - m_sink->bytesFree())) / bpf / rate;
    return seconds;
}

void StreamingPlayer::pump()
{
    const qint64 now = m_clock.elapsed();
    bool finished = false;
    if (!m_started) {
        // Still prebuffering: start once the wait has run out, even with little audio. A stream
        // that is still going but has sent nothing yet is waited for, not played as silence.
        if (now >= m_prebufferDeadline) {
            if (m_bufferedMs > 0 || !m_streaming)
                beginPlayback();
            else
                m_prebufferDeadline = now + kRebufferMaxWaitMs;
        }
        if (!m_started)
            return;
    }
    if (m_deviceDisabled) {
        const bool dry = now >= m_simEndMs;
        if (dry && m_streaming) {
            rebuffer(now);
            return;
        }
        if (!dry)
            m_dryStart = -1;
        else if (m_dryStart < 0)
            m_dryStart = now;
        finished = dry && now - m_dryStart >= kDryHoldMs;
    } else if (m_io && m_sink) {
        if (!m_queue.isEmpty()) {
            const int bpf = std::max(1, m_format.bytesPerFrame());
            qint64 room = m_sink->bytesFree();
            room -= room % bpf;
            if (room > 0) {
                const qint64 n = std::min<qint64>(room, m_queue.size());
                const qint64 wrote = m_io->write(m_queue.constData(), n);
                if (wrote > 0) {
                    const QByteArray ref = toReference16k(m_queue.constData(), wrote, m_format, &m_ref);
                    if (!ref.isEmpty())
                        emit reference(ref);
                    m_queue.remove(0, qsizetype(wrote));
                }
                m_queuedFrames = m_queue.size() / bpf;
            }
        }
        // Dry when nothing is waiting and the device has consumed what it was given.
        // A stream that is merely a little late (network jitter) must not close the
        // device and reopen it with a click, so the dry spell has to last.
        const bool dry = m_queue.isEmpty() && m_sink->bytesFree() >= m_deviceBufferBytes - m_format.bytesPerFrame();
        if (dry && m_streaming) {
            rebuffer(now);
        } else {
            if (!dry)
                m_dryStart = -1;
            else if (m_dryStart < 0)
                m_dryStart = now;
            finished = dry && now - m_dryStart >= kDryHoldMs;
        }
        if (m_sink->error() != QAudio::NoError) {
            const QString why = tr("The speakers stopped working.");
            flush();
            emit failed(why);
            return;
        }
    }
    // The meter falls away between chunks.
    const double before = m_level;
    m_level *= finished ? 0.0 : 0.94;
    if (m_level < 0.01)
        m_level = 0.0;
    if (before != m_level && (now - m_lastLevelEmit >= kLevelEmitMs || m_level == 0.0)) {
        m_lastLevelEmit = now;
        emit levelChanged();
    }
    if (finished) {
        closeDevice();
        m_phase = 0.0;
        m_last = 0;
        m_started = false;
        m_bufferedMs = 0;
        m_dryStart = -1;
        if (!m_underran)
            m_targetMs = std::max(kPrebufferMs, m_targetMs - kPrebufferStepMs);
        m_underran = false;
        setActive(false);
        emit drained();
    }
}
