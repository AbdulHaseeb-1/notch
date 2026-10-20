#include "WakeWord.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

#include "VoiceAudio.h"
#include "WakeDetector.h"

namespace wake {

namespace {

struct Token {
    QString orig;
    QString norm;
};

QList<Token> tokens(const QString &text)
{
    QList<Token> out;
    for (const QString &w : text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString n = normalizeWord(w);
        if (!n.isEmpty())
            out.append({ w, n });
    }
    return out;
}

int levenshtein(const QString &a, const QString &b)
{
    QList<int> prev(b.size() + 1), cur(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a.at(i - 1) == b.at(j - 1) ? 0 : 1) });
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Short words must be exact (one wrong letter in "hey" is another word); longer
// ones may be a letter off, the way a speech-to-text service often writes a name.
bool similar(const QString &a, const QString &b)
{
    if (a == b)
        return true;
    const int shortest = std::min(a.size(), b.size());
    if (shortest < 5)
        return false;
    const int d = levenshtein(a, b);
    return d <= 1 || (shortest >= 9 && d <= 2);
}

const QSet<QString> &fillers()
{
    static const QSet<QString> f = { QStringLiteral("hey"), QStringLiteral("hi"), QStringLiteral("ok"),
                                     QStringLiteral("okay"), QStringLiteral("oh"), QStringLiteral("yo"),
                                     QStringLiteral("hello"), QStringLiteral("um"), QStringLiteral("uh"),
                                     QStringLiteral("so") };
    return f;
}

QString restFrom(const QList<Token> &t, int from)
{
    QStringList words;
    for (int i = from; i < t.size(); ++i)
        words << t.at(i).orig;
    QString rest = words.join(QLatin1Char(' '));
    static const QRegularExpression lead(QStringLiteral("^[\\s,.;:!?\\-\\x{2013}\\x{2014}]+"));
    rest.remove(lead);
    return rest.trimmed();
}

} // namespace

QString normalizeWord(const QString &word)
{
    const QString d = word.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    for (const QChar c : d)
        if (c.isLetterOrNumber() && !c.isMark())
            out += c;
    return out;
}

QString phraseProblem(const QString &phrase)
{
    const QStringList words = phrase.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return QObject::tr("Type the word or phrase that wakes Buddy.");
    if (words.size() > 4)
        return QObject::tr("Use at most four words.");
    QString joined;
    for (const QString &w : words) {
        for (const QChar c : w)
            // U+2019 is the typographic apostrophe; spell it by code point so the
            // check can't become a broken multi-byte character constant.
            if (!c.isLetterOrNumber() && c != QLatin1Char('\'') && c != QLatin1Char('-') && c != QChar(0x2019))
                return QObject::tr("Use letters and numbers only.");
        joined += normalizeWord(w);
    }
    if (joined.size() < 4)
        return QObject::tr("That is too short and would wake Buddy by accident. Use at least four letters.");
    if (joined.size() > 40)
        return QObject::tr("That phrase is too long.");
    return QString();
}

Match match(const QString &transcript, const QString &phrase)
{
    Match m;
    QStringList p;
    for (const QString &w : phrase.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString n = normalizeWord(w);
        if (!n.isEmpty())
            p << n;
    }
    const QList<Token> t = tokens(transcript);
    if (p.isEmpty() || t.isEmpty())
        return m;

    QList<int> starts{ 0 };
    if (t.size() > 1 && fillers().contains(t.first().norm))
        starts << 1;
    for (int start : starts) {
        int i = start;
        bool ok = true;
        for (const QString &w : p) {
            if (i < t.size() && similar(t.at(i).norm, w))
                ++i;
            else {
                ok = false;
                break;
            }
        }
        // "heybuddy" written as one word.
        if (!ok && p.size() > 1 && start < t.size() && similar(t.at(start).norm, p.join(QString()))) {
            ok = true;
            i = start + 1;
        }
        if (ok) {
            m.found = true;
            m.rest = restFrom(t, i);
            return m;
        }
    }
    return m;
}

bool confirmSustained(int &streak, float score, float threshold, int needed)
{
    if (score < threshold) {
        streak = 0;
        return false;
    }
    if (++streak < qMax(1, needed))
        return false;
    streak = 0;   // consumed: the next wake needs a fresh streak
    return true;
}

QString spokenSummary(const QString &text, int maxChars)
{
    QString s = text;
    static const QRegularExpression fence(QStringLiteral("```.*?```"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]*)\\]\\([^)]*\\)"));
    static const QRegularExpression url(QStringLiteral("https?://\\S+"));
    static const QRegularExpression marks(QStringLiteral("[`*_#>~|]"));
    static const QRegularExpression bullet(QStringLiteral("(?m)^\\s*(?:[-+\\x{2022}]|\\d+[.)])\\s+"));
    s.remove(fence);
    s.replace(link, QStringLiteral("\\1"));
    s.remove(url);
    s.remove(bullet);
    s.remove(marks);
    s.replace(QRegularExpression(QStringLiteral("\\s*\\n+\\s*")), QStringLiteral(". "));
    s = s.simplified();
    s.replace(QRegularExpression(QStringLiteral("([.!?:;])(\\s*\\.)+")), QStringLiteral("\\1"));
    s.remove(QRegularExpression(QStringLiteral("^[.\\s]+")));
    if (s.size() <= maxChars)
        return s;
    // Whole sentences while they fit.
    QString out;
    static const QRegularExpression sentence(QStringLiteral("[^.!?]+[.!?]+\\s*"));
    auto it = sentence.globalMatch(s);
    while (it.hasNext()) {
        const QString part = it.next().captured(0);
        if (out.size() + part.size() > maxChars)
            break;
        out += part;
    }
    out = out.trimmed();
    if (!out.isEmpty())
        return out;
    // One long sentence: cut at a word boundary.
    QString cut = s.left(maxChars);
    const int sp = cut.lastIndexOf(QLatin1Char(' '));
    if (sp > maxChars / 2)
        cut = cut.left(sp);
    return cut.trimmed() + QStringLiteral("…");
}

// ---- Segmenter ------------------------------------------------------------------------

Segmenter::Segmenter(const SegmenterConfig &config)
    : m_config(config)
{
}

void Segmenter::setConfig(const SegmenterConfig &config)
{
    m_config = config;
}

void Segmenter::reset()
{
    m_pending.clear();
    m_preRoll.clear();
    m_segment.clear();
    m_in = false;
    m_speechRun = m_speechFrames = m_silentRun = m_frames = 0;
}

bool Segmenter::feed(const QByteArray &pcm, QList<QByteArray> *out)
{
    bool started = false;
    const int frameBytes = qMax(1, m_config.frameMs * voice::kSampleRate / 1000) * 2;
    m_pending += pcm;
    while (m_pending.size() >= frameBytes) {
        const QByteArray frame = m_pending.left(frameBytes);
        m_pending.remove(0, frameBytes);
        processFrame(frame, out, &started);
    }
    return started;
}

void Segmenter::processFrame(const QByteArray &frame, QList<QByteArray> *out, bool *started)
{
    const double lvl = voice::level(frame);
    const double threshold = std::max(m_config.minLevel, m_floor * m_config.floorFactor);
    const bool speech = lvl > threshold;
    const int preRollFrames = qMax(0, m_config.preRollMs / m_config.frameMs);
    const int startFrames = qMax(1, (m_config.startMs + m_config.frameMs - 1) / m_config.frameMs);
    const int endFrames = qMax(1, m_config.endSilenceMs / m_config.frameMs);
    const int maxFrames = qMax(1, m_config.maxMs / m_config.frameMs);

    if (!m_in) {
        if (!speech) {
            // The room's own noise level, tracked while nothing is being said.
            m_floor = 0.97 * m_floor + 0.03 * lvl;
            m_speechRun = 0;
        } else {
            ++m_speechRun;
        }
        m_preRoll.append(frame);
        while (m_preRoll.size() > preRollFrames + startFrames)
            m_preRoll.removeFirst();
        if (m_speechRun >= startFrames) {
            m_in = true;
            m_segment.clear();
            for (const QByteArray &f : std::as_const(m_preRoll))
                m_segment += f;
            m_frames = int(m_preRoll.size());
            m_speechFrames = m_speechRun;
            m_silentRun = 0;
            m_preRoll.clear();
            *started = true;
        }
        return;
    }

    m_segment += frame;
    ++m_frames;
    if (speech) {
        ++m_speechFrames;
        m_silentRun = 0;
    } else {
        ++m_silentRun;
    }
    if (m_silentRun >= endFrames || m_frames >= maxFrames)
        finish(out);
}

void Segmenter::finish(QList<QByteArray> *out)
{
    const int frameBytes = qMax(1, m_config.frameMs * voice::kSampleRate / 1000) * 2;
    const int minSpeechFrames = qMax(1, m_config.minSpeechMs / m_config.frameMs);
    if (m_speechFrames >= minSpeechFrames) {
        // Keep a short tail of quiet, not the whole wait for the end of the sentence.
        const int keepTail = 10;
        const int drop = qMax(0, m_silentRun - keepTail) * frameBytes;
        if (drop > 0 && drop < m_segment.size())
            m_segment.chop(drop);
        out->append(m_segment);
    }
    m_segment.clear();
    m_in = false;
    m_speechRun = m_speechFrames = m_silentRun = m_frames = 0;
}

} // namespace wake

// ---- the local detector's thread -----------------------------------------------------------

namespace {

const QString kBundledDir = QStringLiteral(":/qt/qml/noch/resources/wakeword");
const QString kMelName = QStringLiteral("melspectrogram.onnx");
const QString kEmbedName = QStringLiteral("embedding_model.onnx");
const QString kDefaultModel = QStringLiteral("hey_buddy.onnx");

// A model file dropped into the user's folder wins over the bundled one. For the
// phrase itself any .onnx there that is not one of the two shared feature
// models counts (the newest, if several).
QString userWakeModel(const QString &folder)
{
    const QFileInfoList files = QDir(folder).entryInfoList({ QStringLiteral("*.onnx") }, QDir::Files, QDir::Time);
    for (const QFileInfo &f : files)
        if (f.fileName() != kMelName && f.fileName() != kEmbedName)
            return f.absoluteFilePath();
    return QString();
}

QString featureModel(const QString &folder, const QString &name)
{
    const QString user = QDir(folder).filePath(name);
    if (QFileInfo::exists(user))
        return user;
    const QString bundled = kBundledDir + QLatin1Char('/') + name;
    return QFile::exists(bundled) ? bundled : QString();
}

QString wakeModelPath(const QString &folder)
{
    const QString user = userWakeModel(folder);
    if (!user.isEmpty())
        return user;
    const QString bundled = kBundledDir + QLatin1Char('/') + kDefaultModel;
    return QFile::exists(bundled) ? bundled : QString();
}

QByteArray slurp(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class WakeWorker : public QObject
{
    Q_OBJECT
public slots:
    // Loads ONNX Runtime and the three models. Slow (tens of milliseconds to a
    // second), so it runs here and not on the GUI thread.
    void init(const QString &folder)
    {
        QString why;
        const QString appDir = QCoreApplication::applicationDirPath();
        if (!ortrt::load({ folder, appDir, appDir + QStringLiteral("/wakeword") }, &why)) {
            emit ready(false, why);
            return;
        }
        WakeDetector::Models models;
        models.melspectrogram = slurp(featureModel(folder, kMelName));
        models.embedding = slurp(featureModel(folder, kEmbedName));
        models.wakeword = slurp(wakeModelPath(folder));
        if (models.melspectrogram.isEmpty() || models.embedding.isEmpty() || models.wakeword.isEmpty()) {
            emit ready(false, tr("The wake word models could not be read."));
            return;
        }
        if (!m_detector.load(models, &why)) {
            emit ready(false, why);
            return;
        }
        emit ready(true, QString());
    }

    void feed(const QByteArray &pcm, quint64 generation)
    {
        emit scored(m_detector.loaded() ? m_detector.feed(pcm) : 0.0f, generation);
    }

    void reset()
    {
        if (m_detector.loaded())
            m_detector.reset();
    }

signals:
    void ready(bool ok, const QString &error);
    void scored(float best, quint64 generation);

private:
    WakeDetector m_detector;
};

// ---- WakeWord ---------------------------------------------------------------------------

WakeWord::WakeWord(VoiceInput *voice, QObject *parent)
    : QObject(parent)
    , m_voice(voice)
{
    m_awaitTimer.setSingleShot(true);
    connect(&m_awaitTimer, &QTimer::timeout, this, [this] {
        if (m_mode != Mode::Awaiting)
            return;
        leaveAwaiting();
        emit timedOut();
    });
    m_modelFolder = QDir(QFileInfo(QSettings().fileName()).absolutePath()).filePath(QStringLiteral("wakeword"));
    if (m_voice) {
        connect(m_voice, &VoiceInput::stateChanged, this, &WakeWord::refresh);
        connect(m_voice, &VoiceInput::configChanged, this, [this] {
            m_halted = false;
            refresh();
            emit configChanged();
        });
    }
}

WakeWord::~WakeWord()
{
    closeMic();
    if (m_worker) {
        m_workerThread.quit();
        m_workerThread.wait();
    }
}

void WakeWord::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        m_halted = false;
        clearError();
    }
    refresh();
    emit configChanged();
}

void WakeWord::setPhrase(const QString &phrase)
{
    const QString p = phrase.simplified();
    if (p == m_phrase)
        return;
    m_phrase = p;
    refresh();
    emit configChanged();
}

QString WakeWord::blockedReason() const
{
    if (!m_enabled)
        return QString();
    if (!m_voice || !m_voice->enabled())
        return tr("Turn on voice input first.");
    if (!m_voice->configured())
        return m_voice->problem();
    // The local detector needs nothing more: the phrase is the model's.
    if (usingLocal())
        return QString();
    return wake::phraseProblem(m_phrase);
}

QString WakeWord::state() const
{
    if (!m_enabled)
        return QStringLiteral("off");
    if (!blockedReason().isEmpty() || m_halted)
        return QStringLiteral("blocked");
    if (usingLocal() && m_local != Local::Ready)
        return QStringLiteral("loading");
    if (!m_micOpen)
        return QStringLiteral("paused");
    return m_mode == Mode::Awaiting ? QStringLiteral("awaiting") : QStringLiteral("listening");
}

void WakeWord::setCapture(std::unique_ptr<AudioCapture> capture)
{
    closeMic();
    m_capture = std::move(capture);
    refresh();
}

void WakeWord::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    m_paused = paused;
    refresh();
}

void WakeWord::cancelAwaiting()
{
    if (m_mode == Mode::Awaiting)
        leaveAwaiting();
}

void WakeWord::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

bool WakeWord::shouldListen() const
{
    return m_enabled && blockedReason().isEmpty() && !m_halted && !m_paused
        && !(m_voice && m_voice->busy())
        && (!usingLocal() || m_local == Local::Ready);
}

void WakeWord::refresh()
{
    if (m_enabled && m_local == Local::Untried && localWanted() && m_voice && m_voice->enabled()
        && m_voice->configured())
        startLocal();
    if (shouldListen()) {
        if (!m_micOpen)
            openMic();
    } else if (m_micOpen) {
        closeMic();
    }
    if (const QString now = state(); now != m_lastState) {
        m_lastState = now;
        emit stateChanged();
    }
}

bool WakeWord::openMic()
{
    if (!m_capture)
        m_capture = makeMicrophoneCapture();
    disconnect(m_capture.get(), nullptr, this, nullptr);
    connect(m_capture.get(), &AudioCapture::audio, this, &WakeWord::onAudio);
    connect(m_capture.get(), &AudioCapture::failed, this, [this](const QString &message) {
        closeMic();
        m_halted = true;
        fail(message);
        refresh();
    });
    m_segmenter.setConfig(m_listenConfig);
    m_segmenter.reset();
    m_mode = Mode::Listening;
    resetLocalDetector();
    QString why;
    if (!m_capture->start(&why)) {
        m_halted = true;   // until the settings change, instead of retrying a missing microphone forever
        fail(why.isEmpty() ? tr("The microphone could not be opened.") : why);
        return false;
    }
    m_micOpen = true;
    return true;
}

void WakeWord::closeMic()
{
    ++m_generation;
    if (m_ticket) {
        m_voice->abort(m_ticket);
        m_ticket = 0;
    }
    if (m_capture && m_micOpen)
        m_capture->stop();
    m_micOpen = false;
    resetLocalDetector();
    m_segmenter.reset();
    m_awaitTimer.stop();
    m_mode = Mode::Listening;
    m_segmenter.setConfig(m_listenConfig);
}

void WakeWord::onAudio(const QByteArray &pcm)
{
    if (!m_micOpen)
        return;
    if (m_mode == Mode::Listening && usingLocal()) {
        feedLocal(pcm);     // the phrase is found on this computer; nothing is sent anywhere
        return;
    }
    QList<QByteArray> found;
    const bool started = m_segmenter.feed(pcm, &found);
    if (started && m_mode == Mode::Awaiting)
        m_awaitTimer.stop();   // they are speaking: wait for the end of it, not the clock
    for (const QByteArray &u : std::as_const(found))
        onUtterance(u);
}

void WakeWord::onUtterance(const QByteArray &pcm)
{
    if (voice::isSilent(pcm))
        return;
    if (m_ticket) {                // one check at a time; the rest is dropped, not queued up
        ++m_dropped;
        return;
    }
    check(pcm, m_mode);
}

void WakeWord::check(const QByteArray &pcm, Mode mode)
{
    ++m_checked;
    const quint64 gen = m_generation;
    m_ticket = m_voice->transcribe(voice::trimSilence(pcm), this,
                                   [this, mode, gen](const VoiceInput::Transcription &t) {
        if (gen != m_generation)
            return;
        m_ticket = 0;
        onTranscript(t, mode);
    });
}

void WakeWord::deliver(const QString &text)
{
    m_lastCommandMs = QDateTime::currentMSecsSinceEpoch();
    emit command(text.left(kMaxCommandChars));
}

void WakeWord::onTranscript(const VoiceInput::Transcription &t, Mode mode)
{
    if (!t.ok) {
        failure(t, mode);
        return;
    }
    if (mode == Mode::Awaiting) {
        leaveAwaiting();
        QString text = t.text;
        // "Hey buddy, ..." said again as part of the request: the request is what follows.
        if (const wake::Match again = wake::match(text, m_phrase); again.found)
            text = again.rest;
        if (text.trimmed().isEmpty()) {
            emit timedOut();
            return;
        }
        deliver(text);
        return;
    }
    const wake::Match m = wake::match(t.text, m_phrase);
    if (!m.found)
        return;          // just talk: compared with the phrase, then forgotten
    if (QDateTime::currentMSecsSinceEpoch() - m_lastCommandMs < m_cooldownMs)
        return;
    emit woke();
    if (!m.rest.isEmpty())
        deliver(m.rest);
    else
        enterAwaiting();
}

void WakeWord::failure(const VoiceInput::Transcription &t, Mode mode)
{
    if (t.empty) {         // the engine ran and heard no words in it
        if (mode == Mode::Awaiting) {
            leaveAwaiting();
            emit timedOut();
        }
        return;
    }
    if (mode == Mode::Awaiting)
        leaveAwaiting();
    fail(t.error);
    m_halted = true;       // the model failed; asking again will not help until the settings change
    refresh();
}

void WakeWord::enterAwaiting()
{
    m_mode = Mode::Awaiting;
    m_segmenter.reset();
    wake::SegmenterConfig c = m_listenConfig;
    c.endSilenceMs = qMax(c.endSilenceMs, 1200);   // people pause mid-request
    c.maxMs = 20000;
    m_segmenter.setConfig(c);
    m_awaitTimer.start(m_awaitStartMs);
    emit stateChanged();
}

void WakeWord::leaveAwaiting()
{
    m_mode = Mode::Listening;
    m_segmenter.setConfig(m_listenConfig);
    m_awaitTimer.stop();
    emit stateChanged();
}

// ---- the local detector -------------------------------------------------------------------

QString WakeWord::detector() const
{
    if (!usingLocal())
        return QStringLiteral("speech");
    return m_local == Local::Ready ? QStringLiteral("local") : QStringLiteral("loading");
}

bool WakeWord::localWanted() const
{
    if (m_localWantedCache < 0)
        m_localWantedCache = !wakeModelPath(m_modelFolder).isEmpty()
                             && !featureModel(m_modelFolder, kMelName).isEmpty()
                             && !featureModel(m_modelFolder, kEmbedName).isEmpty();
    return m_localWantedCache == 1;
}

bool WakeWord::usingLocal() const
{
    return localWanted() && m_local != Local::Failed;
}

void WakeWord::setModelFolder(const QString &folder)
{
    if (folder == m_modelFolder)
        return;
    m_modelFolder = folder;
    m_localWantedCache = -1;
    m_local = Local::Untried;
    m_localError.clear();
    resetLocalDetector();
    refresh();
    emit detectorChanged();
}

void WakeWord::openModelFolder() const
{
    QDir().mkpath(m_modelFolder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_modelFolder));
}

QVariantMap WakeWord::detectorModelStatus() const
{
    QVariantMap s;
    s[QStringLiteral("detector")] = detector();
    s[QStringLiteral("problem")] = m_localError;
    QVariantList files;
    auto describe = [&files](const QString &name, const QString &path) {
        const QFileInfo fi(path);
        files.append(QVariantMap{
            { QStringLiteral("name"), name },
            { QStringLiteral("path"), path },
            // The bundled copies live in the Qt resources (":/..."), which is "found" too.
            { QStringLiteral("found"), !path.isEmpty() && QFile::exists(path) },
            { QStringLiteral("bytes"), qint64(path.startsWith(QLatin1Char(':')) || !fi.exists() ? 0 : fi.size()) },
        });
    };
    describe(kMelName, featureModel(m_modelFolder, kMelName));
    describe(kEmbedName, featureModel(m_modelFolder, kEmbedName));
    describe(QStringLiteral("wake word model (.onnx)"), wakeModelPath(m_modelFolder));
    s[QStringLiteral("files")] = files;
    return s;
}

void WakeWord::setThreshold(double t)
{
    const double v = std::clamp(t, 0.2, 0.9);
    if (qFuzzyCompare(v, m_threshold))
        return;
    m_threshold = v;
    emit configChanged();
}

void WakeWord::setLocalState(Local state, const QString &error)
{
    m_local = state;
    m_localError = error;
    emit detectorChanged();
}

void WakeWord::startLocal()
{
    if (!m_worker) {
        m_worker = new WakeWorker;
        m_worker->moveToThread(&m_workerThread);
        connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &WakeWorker::ready, this, &WakeWord::onWorkerReady);
        connect(m_worker, &WakeWorker::scored, this, &WakeWord::onWorkerScore);
        m_workerThread.start();
    }
    setLocalState(Local::Loading);
    const QString folder = m_modelFolder;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, folder] { w->init(folder); }, Qt::QueuedConnection);
}

void WakeWord::onWorkerReady(bool ok, const QString &error)
{
    if (ok) {
        setLocalState(Local::Ready);
    } else {
        // Falls back to transcribing each sentence on this computer and comparing it
        // with the phrase.
        qWarning().noquote() << "WakeWord: local detector unavailable:" << error;
        setLocalState(Local::Failed, error);
        emit configChanged();
    }
    refresh();
}

void WakeWord::feedLocal(const QByteArray &pcm)
{
    m_localBuf += pcm;
    constexpr int kBatchBytes = WakeDetector::kChunkSamples * 2;   // one 80 ms step
    if (m_localBuf.size() < kBatchBytes || !m_worker)
        return;
    // A worker that fell behind drops audio instead of building a queue that
    // would make the phrase arrive late.
    if (m_inflight.load() > 25) {
        m_localBuf.clear();
        ++m_dropped;
        return;
    }
    ++m_inflight;
    const QByteArray batch = std::exchange(m_localBuf, QByteArray());
    const quint64 gen = m_detGeneration;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, batch, gen] { w->feed(batch, gen); }, Qt::QueuedConnection);
}

void WakeWord::resetLocalDetector()
{
    ++m_detGeneration;
    m_wakeStreak = 0;
    m_localBuf.clear();
    if (m_worker && m_local == Local::Ready)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->reset(); }, Qt::QueuedConnection);
}

void WakeWord::onWorkerScore(float best, quint64 generation)
{
    --m_inflight;
    if (generation != m_detGeneration || m_mode != Mode::Listening || !m_micOpen)
        return;
    // One hot chunk is not a wake phrase: require the score to sustain
    // across consecutive callbacks (160 ms of "hey buddy", not a click).
    if (!wake::confirmSustained(m_wakeStreak, best, float(m_threshold), kWakeStreakNeeded))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastCommandMs < m_cooldownMs || now - m_lastWakeMs < m_cooldownMs)
        return;
    m_lastWakeMs = now;
    resetLocalDetector();       // what it just heard must not wake it a second time
    emit woke();
    enterAwaiting();            // the request that follows is what is transcribed by Moonshine
}

void WakeWord::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    emit failed(message);
}

#include "WakeWord.moc"
