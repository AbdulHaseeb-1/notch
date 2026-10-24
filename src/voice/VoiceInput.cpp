#include "VoiceInput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>
#include <QRegularExpression>

#include "VoiceAudio.h"

// ---- the real microphone -------------------------------------------------------------

namespace {

class MicrophoneCapture final : public AudioCapture
{
public:
    ~MicrophoneCapture() override { stop(); }

    bool start(QString *error) override
    {
        const QAudioDevice device = QMediaDevices::defaultAudioInput();
        if (device.isNull()) {
            *error = QObject::tr("No microphone was found.");
            return false;
        }
        QAudioFormat want;
        want.setSampleRate(voice::kSampleRate);
        want.setChannelCount(1);
        want.setSampleFormat(QAudioFormat::Int16);
        // Most microphones can record this directly; if not, take what the device
        // prefers and convert as the audio arrives.
        m_format = device.isFormatSupported(want) ? want : device.preferredFormat();
        m_frameBytes = m_format.bytesPerFrame();
        if (m_frameBytes <= 0) {
            *error = QObject::tr("The microphone's format is not supported.");
            return false;
        }
        m_source = new QAudioSource(device, m_format, this);
        m_io = m_source->start();
        if (!m_io || m_source->error() != QAudio::NoError) {
            *error = QObject::tr("The microphone could not be opened. Check Windows Settings > Privacy > Microphone.");
            stop();
            return false;
        }
        connect(m_io, &QIODevice::readyRead, this, [this] { drain(); });
        return true;
    }

    void stop() override
    {
        drain();   // the last few milliseconds before the key came up are part of the last word
        if (m_source) {
            m_source->stop();
            m_source->deleteLater();
            m_source = nullptr;
        }
        m_io = nullptr;
        m_pending.clear();
    }

private:
    void drain()
    {
        if (!m_io || m_frameBytes <= 0)
            return;
        m_pending += m_io->readAll();
        // Only whole frames; a frame split across two reads waits for its other half.
        const qsizetype whole = (m_pending.size() / m_frameBytes) * m_frameBytes;
        if (whole <= 0)
            return;
        const QByteArray chunk = voice::toMono16k(m_pending.left(whole), m_format);
        m_pending.remove(0, whole);
        if (!chunk.isEmpty())
            emit audio(chunk);
    }

    QAudioSource *m_source = nullptr;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    int m_frameBytes = 0;
    QByteArray m_pending;
};

} // namespace

std::unique_ptr<AudioCapture> makeMicrophoneCapture()
{
    return std::make_unique<MicrophoneCapture>();
}

// ---- the speech-to-text thread ---------------------------------------------------------------

// A synchronous engine's prepare() and transcribe() can take a while, so they happen
// here and not on the thread that draws the UI. (Deepgram is asynchronous and never
// uses this thread; synchronous engines are the scripted ones tests stand in.)
// It only ever runs one request at a time, in order.
class TranscribeWorker : public QObject
{
    Q_OBJECT
public:
    explicit TranscribeWorker(Transcriber *engine) : m_engine(engine) {}

public slots:
    void prepare() { emit prepared(m_engine->prepare()); }
    void run(quint64 ticket, const QByteArray &pcm)
    {
        QString error;
        const QString text = m_engine->transcribe(pcm, &error);
        emit finished(ticket, text, error);
    }

signals:
    void prepared(const QString &error);
    void finished(quint64 ticket, const QString &text, const QString &error);

private:
    Transcriber *m_engine;
};

// ---- VoiceInput -------------------------------------------------------------------------

VoiceInput::VoiceInput(QObject *parent)
    : QObject(parent)
{
    // The fallback settings object is the live one until a shared one is
    // attached, so edits to it rebuild the engine just the same.
    connect(&m_fallbackSettings, &DeepgramSettings::configChanged, this, [this] { applyEngine(); });
    applyEngine();   // the one engine: Deepgram, from the settings as they are now
    m_maxTimer.setSingleShot(true);
    connect(&m_maxTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Listening)
            endListening();   // the maximum: stop and use what was said
    });
}

VoiceInput::~VoiceInput()
{
    if (m_capture)
        m_capture->stop();
    if (m_worker) {
        m_thread.quit();
        m_thread.wait();
    }
}

QString VoiceInput::engine() const
{
    return m_transcriber ? m_transcriber->name() : QString();
}

// Rebuilds the engine from the shared Deepgram settings. A request in flight
// is abandoned (its answer is dropped), which is what changing the settings means.
void VoiceInput::applyEngine()
{
    DeepgramListenSettings s = deepgramSettings()->listenSettings();
    s.apiBase = m_apiBase;
    setTranscriber(makeDeepgramTranscriber(s));
}

void VoiceInput::setDeepgramSettings(DeepgramSettings *settings)
{
    // Drop both possible live connections (the old effective object and the
    // fallback the constructor wired) so the new one is connected exactly once.
    disconnect(deepgramSettings(), &DeepgramSettings::configChanged, this, nullptr);
    m_fallbackSettings.disconnect(this);
    m_settings = settings;
    connect(deepgramSettings(), &DeepgramSettings::configChanged, this, [this] { applyEngine(); });
    applyEngine();
}

void VoiceInput::setDeepgramApiBase(const QString &base)
{
    if (base == m_apiBase)
        return;
    m_apiBase = base;
    applyEngine();
}

QString VoiceInput::problem() const
{
    if (!m_transcriber)
        return tr("There is no speech-to-text engine.");
    if (!m_prepareError.isEmpty())
        return m_prepareError;
    return m_transcriber->problem();
}

bool VoiceInput::configured() const
{
    return problem().isEmpty();
}

void VoiceInput::setTranscriber(std::unique_ptr<Transcriber> transcriber)
{
    if (m_worker) {
        m_thread.quit();
        m_thread.wait();
        m_worker = nullptr;     // deleted with the thread
    }
    m_pending.clear();
    m_clipTicket = 0;
    m_ready = false;
    m_prepareError.clear();
    m_transcriber = std::move(transcriber);
    // A network engine has nothing to load: it is ready as soon as it is configured.
    m_ready = m_transcriber && m_transcriber->isAsync() && m_transcriber->problem().isEmpty();
    emit readyChanged();
    emit configChanged();
    if (m_enabled)
        ensureWorker();
}

void VoiceInput::ensureWorker()
{
    if (m_worker || !m_transcriber || m_transcriber->isAsync())
        return;
    m_worker = new TranscribeWorker(m_transcriber.get());
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &TranscribeWorker::prepared, this, &VoiceInput::onPrepared);
    connect(m_worker, &TranscribeWorker::finished, this, &VoiceInput::onFinished);
    m_thread.start();
    // Load the model now, in the background, so the first request does not wait for it.
    if (m_transcriber->problem().isEmpty())
        QMetaObject::invokeMethod(m_worker, &TranscribeWorker::prepare, Qt::QueuedConnection);
}

void VoiceInput::onPrepared(const QString &error)
{
    m_ready = error.isEmpty();
    m_prepareError = error;
    emit readyChanged();
    emit configChanged();
}

void VoiceInput::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (!on)
        cancel();   // turning it off closes the microphone at once
    else
        ensureWorker();
    emit configChanged();
}

QString VoiceInput::state() const
{
    switch (m_state) {
    case State::Listening: return QStringLiteral("listening");
    case State::Transcribing: return QStringLiteral("transcribing");
    case State::Idle: break;
    }
    return QStringLiteral("idle");
}

double VoiceInput::seconds() const
{
    return voice::seconds(m_pcm);
}

void VoiceInput::setCapture(std::unique_ptr<AudioCapture> capture)
{
    if (m_capture)
        m_capture->stop();
    m_capture = std::move(capture);
}

void VoiceInput::setState(State s)
{
    if (s == m_state)
        return;
    m_state = s;
    emit stateChanged();
}

void VoiceInput::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    emit failed(message);
}

void VoiceInput::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

bool VoiceInput::beginListening()
{
    if (m_state != State::Idle)
        return false;
    clearError();
    if (!m_enabled) {
        fail(tr("Voice input is off. Turn it on in Settings > Buddy."));
        return false;
    }
    if (!configured()) {
        fail(problem());
        return false;
    }
    ensureWorker();
    if (!m_capture)
        m_capture = makeMicrophoneCapture();
    QString why;
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &AudioCapture::audio, this, &VoiceInput::onAudio);
    connect(m_capture.get(), &AudioCapture::failed, this, [this](const QString &message) {
        if (m_state != State::Listening)
            return;
        m_capture->stop();
        m_maxTimer.stop();
        m_pcm.clear();
        m_level = 0;
        emit levelChanged();
        setState(State::Idle);
        fail(message);
    });
    m_pcm.clear();
    m_level = 0;
    if (!m_capture->start(&why)) {
        fail(why.isEmpty() ? tr("The microphone could not be opened.") : why);
        return false;
    }
    setState(State::Listening);
    emit levelChanged();
    m_maxTimer.start(m_maxSeconds * 1000);
    return true;
}

void VoiceInput::onAudio(const QByteArray &pcm)
{
    if (m_state != State::Listening)
        return;
    m_pcm += pcm;
    // Rise fast, fall slowly: a meter that follows the voice without flickering.
    m_level = qMax(voice::level(pcm), m_level * 0.82);
    emit levelChanged();
}

void VoiceInput::endListening()
{
    if (m_state != State::Listening)
        return;
    m_maxTimer.stop();
    if (m_capture)
        m_capture->stop();
    m_level = 0;
    emit levelChanged();
    const QByteArray clip = m_pcm;
    m_pcm.clear();
    if (voice::seconds(clip) < kMinSeconds) {
        setState(State::Idle);
        fail(tr("That was too short. Hold the key while you speak."));
        return;
    }
    if (voice::isSilent(clip)) {
        setState(State::Idle);
        fail(tr("I did not hear anything. Check your microphone."));
        return;
    }
    setState(State::Transcribing);
    m_clipTicket = transcribe(voice::trimSilence(clip), this, [this](const Transcription &t) {
        m_clipTicket = 0;
        setState(State::Idle);
        if (!t.ok) {
            fail(t.error);
            return;
        }
        clearError();
        emit transcript(t.text);
    });
}

void VoiceInput::cancel()
{
    m_maxTimer.stop();
    if (m_capture && m_state == State::Listening)
        m_capture->stop();
    if (m_clipTicket) {
        abort(m_clipTicket);
        m_clipTicket = 0;
    }
    m_pcm.clear();
    if (m_level != 0) {
        m_level = 0;
        emit levelChanged();
    }
    setState(State::Idle);
}

quint64 VoiceInput::transcribe(const QByteArray &pcm16, QObject *context,
                               std::function<void(const Transcription &)> done)
{
    ensureWorker();
    const quint64 ticket = m_nextTicket++;
    m_pending.insert(ticket, Pending{ QPointer<QObject>(context), std::move(done) });
    if (m_transcriber && m_transcriber->isAsync()) {
        // A network engine: the request runs on this thread's event loop and reports back.
        const QPointer<VoiceInput> self(this);
        m_transcriber->transcribeAsync(pcm16, [self, ticket](const QString &text, const QString &error) {
            if (self)
                self->onFinished(ticket, text, error);
        });
        return ticket;
    }
    if (!m_worker) {
        Transcription t;
        t.error = problem();
        QMetaObject::invokeMethod(this, [this, ticket, t] {
            const Pending p = m_pending.take(ticket);
            if (p.done && p.context)
                p.done(t);
        }, Qt::QueuedConnection);
        return ticket;
    }
    QMetaObject::invokeMethod(m_worker, [w = m_worker, ticket, pcm16] { w->run(ticket, pcm16); }, Qt::QueuedConnection);
    return ticket;
}

void VoiceInput::abort(quint64 ticket)
{
    m_pending.remove(ticket);   // the engine may still finish it; the answer is dropped
}

void VoiceInput::onFinished(quint64 ticket, const QString &text, const QString &error)
{
    const auto it = m_pending.find(ticket);
    if (it == m_pending.end())
        return;                 // aborted
    const Pending p = it.value();
    m_pending.erase(it);
    if (!p.done || !p.context)
        return;
    Transcription t;
    // Repair the recognizer's predictable mistakes before this becomes a
    // command or a chat message: stutters, mangled technology names,
    // missing sentence shape.
    const QString words = voice::fixTranscript(text.simplified());
    if (!error.isEmpty()) {
        t.error = error.left(300);
    } else if (words.isEmpty()) {
        t.empty = true;
        t.error = tr("I could not make out any words.");
    } else {
        t.ok = true;
        t.text = words;
    }
    p.done(t);
}

#include "VoiceInput.moc"
