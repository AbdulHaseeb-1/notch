#include "SpeechPlayer.h"

#include <QAudioSink>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

SpeechPlayer::SpeechPlayer(QObject *parent)
    : QObject(parent)
{
    m_deviceDisabled = qEnvironmentVariableIntValue("NOCH_NO_AUDIO") != 0
                       || QGuiApplication::platformName() == QLatin1String("offscreen");
    m_guard.setSingleShot(true);
    connect(&m_guard, &QTimer::timeout, this, [this] { finish(); });
}

SpeechPlayer::~SpeechPlayer()
{
    dropSink();
}

QByteArray SpeechPlayer::wavToPcm(const QByteArray &wav, int *rate, int *channels, QString *error)
{
    auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QByteArray();
    };
    if (wav.size() < 44 || wav.left(4) != "RIFF" || wav.mid(8, 4) != "WAVE")
        return fail(tr("The voice service did not return a WAV file."));
    int format = 0, bits = 0, ch = 0, hz = 0;
    qsizetype at = 12;
    while (at + 8 <= wav.size()) {
        const QByteArray id = wav.mid(at, 4);
        quint32 size = qFromLittleEndian<quint32>(wav.constData() + at + 4);
        const qsizetype body = at + 8;
        if (id == "fmt " && body + 16 <= wav.size()) {
            format = qFromLittleEndian<quint16>(wav.constData() + body);
            ch = qFromLittleEndian<quint16>(wav.constData() + body + 2);
            hz = int(qFromLittleEndian<quint32>(wav.constData() + body + 4));
            bits = qFromLittleEndian<quint16>(wav.constData() + body + 14);
            // WAVE_FORMAT_EXTENSIBLE carries the real format in a sub-format; 16-bit PCM is all we take.
            if (format == 0xFFFE)
                format = 1;
        } else if (id == "data") {
            if (format != 1 || bits != 16 || (ch != 1 && ch != 2) || hz < 8000 || hz > 192000)
                return fail(tr("The voice service sent audio in a format Noch cannot play (needs 16-bit PCM)."));
            qsizetype len = (size == 0 || size == 0xFFFFFFFFu || body + qsizetype(size) > wav.size())
                                ? wav.size() - body : qsizetype(size);
            len -= len % (2 * ch);
            if (len <= 0)
                return fail(tr("The voice service sent no audio."));
            if (rate)
                *rate = hz;
            if (channels)
                *channels = ch;
            return wav.mid(body, len);
        }
        at = body + qsizetype(size) + (size & 1);
    }
    return fail(tr("The voice service did not return a WAV file."));
}

QByteArray SpeechPlayer::convert(const QByteArray &pcm16, int rate, int channels, const QAudioFormat &to)
{
    const int inCh = std::max(1, channels);
    const qsizetype inFrames = pcm16.size() / (2 * inCh);
    const int outRate = std::max(8000, to.sampleRate());
    const int outCh = std::max(1, to.channelCount());
    if (inFrames <= 0 || rate <= 0)
        return QByteArray();
    const auto *in = reinterpret_cast<const qint16 *>(pcm16.constData());
    const qsizetype outFrames = std::max<qsizetype>(1, qsizetype(double(inFrames) * outRate / rate));
    const int bps = std::max(1, to.bytesPerSample());
    QByteArray out(outFrames * outCh * bps, Qt::Uninitialized);
    char *dst = out.data();
    const double step = double(rate) / outRate;
    auto sampleAt = [&](qsizetype frame, int c) {
        return float(in[frame * inCh + std::min(c, inCh - 1)]) / 32768.0f;
    };
    for (qsizetype i = 0; i < outFrames; ++i) {
        const double pos = double(i) * step;
        const qsizetype a = std::min<qsizetype>(qsizetype(pos), inFrames - 1);
        const qsizetype b = std::min<qsizetype>(a + 1, inFrames - 1);
        const float t = float(pos - double(a));
        for (int c = 0; c < outCh; ++c) {
            // Mono speech to stereo: the same on both sides; more channels stay silent.
            const int src = (inCh == 1) ? 0 : std::min(c, 1);
            const float v = (c < 2 || inCh == 1) ? sampleAt(a, src) + (sampleAt(b, src) - sampleAt(a, src)) * t : 0.0f;
            char *at = dst + (i * outCh + c) * bps;
            switch (to.sampleFormat()) {
            case QAudioFormat::Float: { const float f = v; std::memcpy(at, &f, 4); break; }
            case QAudioFormat::Int32: { const qint32 s = qint32(std::clamp(v, -1.0f, 1.0f) * 2147483520.0f); std::memcpy(at, &s, 4); break; }
            case QAudioFormat::UInt8: { *reinterpret_cast<quint8 *>(at) = quint8(std::lround(128.0f + std::clamp(v, -1.0f, 1.0f) * 127.0f)); break; }
            default: { const qint16 s = qint16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); std::memcpy(at, &s, 2); break; }
            }
        }
    }
    return out;
}

bool SpeechPlayer::play(const QByteArray &pcm16, int rate, int channels)
{
    stop();
    const int ch = std::max(1, channels);
    if (pcm16.size() < 2 * ch || rate <= 0)
        return false;
    m_lastSeconds = double(pcm16.size() / (2 * ch)) / rate;
    ++m_played;
    m_playing = true;

    if (m_deviceDisabled) {
        m_guard.start(30);
        return true;
    }

    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) {
        m_playing = false;
        return false;
    }
    // What the device mixes in natively, float if it takes it, else 16-bit.
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
    m_data = convert(pcm16, rate, ch, fmt);
    if (m_data.isEmpty()) {
        m_playing = false;
        return false;
    }
    m_buffer.close();
    m_buffer.setData(m_data);
    m_buffer.open(QIODevice::ReadOnly);

    m_sink = new QAudioSink(dev, fmt, this);
    QAudioSink *sink = m_sink;
    connect(sink, &QAudioSink::stateChanged, this, [this, sink](QAudio::State st) {
        if (sink != m_sink)
            return;
        // Pull mode: the sink goes idle once the buffer has been read to its end.
        if (st == QAudio::IdleState && m_buffer.atEnd())
            finish();
        else if (st == QAudio::StoppedState && sink->error() != QAudio::NoError)
            finish();
    });
    m_sink->start(&m_buffer);
    if (m_sink->error() != QAudio::NoError) {
        dropSink();
        m_playing = false;
        return false;
    }
    // If the device never says it is done, end shortly after the audio must have been.
    m_guard.start(int(m_lastSeconds * 1000) + 1500);
    return true;
}

void SpeechPlayer::dropSink()
{
    m_guard.stop();
    if (m_sink) {
        QAudioSink *s = m_sink;
        m_sink = nullptr;
        s->stop();
        delete s;
    }
    m_buffer.close();
}

void SpeechPlayer::stop()
{
    dropSink();
    m_playing = false;
}

void SpeechPlayer::finish()
{
    if (!m_playing)
        return;
    dropSink();
    m_playing = false;
    emit finished();
}
