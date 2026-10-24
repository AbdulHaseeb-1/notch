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
#include <cmath>

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

QString phraseDisplayName(const QString &fileName)
{
    QString base = QFileInfo(fileName).completeBaseName();   // "hey_buddy"
    base.replace(QLatin1Char('_'), QLatin1Char(' ')).replace(QLatin1Char('-'), QLatin1Char(' '));
    QStringList words;
    for (const QString &w : base.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString t = w.toLower();
        if (!t.isEmpty())
            t[0] = t.at(0).toUpper();
        words << t;
    }
    return words.isEmpty() ? fileName : words.join(QLatin1Char(' '));
}

QList<PhraseModel> discoverPhraseModels(const QString &userFolder, const QString &bundledDir)
{
    // The pipeline's own models are shared by every phrase; they are not phrases.
    static const QStringList kPipeline = { QStringLiteral("melspectrogram.onnx"),
                                           QStringLiteral("embedding_model.onnx") };
    auto isPhrase = [](const QString &fileName) {
        for (const QString &p : kPipeline)
            if (fileName.compare(p, Qt::CaseInsensitive) == 0)
                return false;
        return true;
    };
    QHash<QString, PhraseModel> byName;
    auto scan = [&byName, &isPhrase](const QString &dir, bool user) {
        if (dir.isEmpty())
            return;
        const QFileInfoList files = QDir(dir).entryInfoList({ QStringLiteral("*.onnx") }, QDir::Files, QDir::Name);
        for (const QFileInfo &f : files) {
            if (!isPhrase(f.fileName()))
                continue;
            PhraseModel &pm = byName[f.fileName()];   // the user's copy is scanned second and wins
            if (pm.fileName.isEmpty()) {
                pm.fileName = f.fileName();
                pm.name = phraseDisplayName(f.fileName());
            }
            pm.path = f.absoluteFilePath();
            if (user)
                pm.userAdded = true;
            else
                pm.builtin = true;
        }
    };
    scan(bundledDir, false);
    scan(userFolder, true);
    QList<PhraseModel> out = byName.values();
    std::sort(out.begin(), out.end(), [](const PhraseModel &a, const PhraseModel &b) { return a.fileName < b.fileName; });
    return out;
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

// A model file dropped into the user's folder wins over the bundled one.
QString featureModel(const QString &folder, const QString &bundledDir, const QString &name)
{
    const QString user = QDir(folder).filePath(name);
    if (QFileInfo::exists(user))
        return user;
    const QString bundled = QDir(bundledDir).filePath(name);
    return QFile::exists(bundled) ? bundled : QString();
}

// The file a phrase id resolves to: the user's copy when there is one.
QString phraseFilePath(const QString &folder, const QString &bundledDir, const QString &fileName)
{
    return featureModel(folder, bundledDir, fileName);
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
    // Loads ONNX Runtime, the two shared models and one classifier per
    // enabled phrase. Slow (tens of milliseconds to a second), so it runs
    // here and not on the GUI thread. Runs again when the enabled set
    // changes: the detector is rebuilt with the new classifiers.
    void init(const QString &folder, const QString &bundledDir, const QStringList &phraseIds)
    {
        QString why;
        const QString appDir = QCoreApplication::applicationDirPath();
        if (!ortrt::load({ folder, appDir, appDir + QStringLiteral("/wakeword") }, &why)) {
            emit ready(false, why);
            return;
        }
        WakeDetector::Models models;
        models.melspectrogram = slurp(featureModel(folder, bundledDir, kMelName));
        models.embedding = slurp(featureModel(folder, bundledDir, kEmbedName));
        QStringList loadedIds;
        for (const QString &id : phraseIds) {
            WakeDetector::Models::Phrase p;
            p.name = wake::phraseDisplayName(id);
            p.model = slurp(phraseFilePath(folder, bundledDir, id));
            if (p.model.isEmpty())
                continue;
            models.phrases.append(p);
            loadedIds << id;
        }
        if (models.melspectrogram.isEmpty() || models.embedding.isEmpty() || models.phrases.isEmpty()) {
            emit ready(false, tr("The wake word models could not be read."));
            return;
        }
        auto detector = std::make_unique<WakeDetector>();
        if (!detector->load(models, &why)) {
            emit ready(false, why);
            return;
        }
        m_detector = std::move(detector);
        m_phraseIds = loadedIds;
        emit ready(true, QString());
    }

    void feed(const QByteArray &pcm, quint64 generation)
    {
        // One best score per loaded phrase, in init() order.
        QVector<float> scores(int(m_phraseIds.size()), 0.0f);
        if (m_detector && m_detector->loaded()) {
            std::vector<std::vector<float>> perPhrase;
            m_detector->feed(pcm, nullptr, &perPhrase);
            for (int i = 0; i < scores.size() && i < int(perPhrase.size()); ++i)
                for (const float s : perPhrase[size_t(i)])
                    scores[i] = std::max(scores[i], s);
        }
        emit scored(scores, generation);
    }

    void reset()
    {
        if (m_detector && m_detector->loaded())
            m_detector->reset();
    }

signals:
    void ready(bool ok, const QString &error);
    void scored(const QVector<float> &scores, quint64 generation);

private:
    std::unique_ptr<WakeDetector> m_detector;
    QStringList m_phraseIds;
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
    m_bundledDir = kBundledDir;
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
    // A live conversation takes over after the phrase, so only the detector has to work.
    if (m_handsOff && usingLocal())
        return QString();
    if (!m_voice || !m_voice->enabled())
        return tr("Turn on voice input first.");
    if (!m_voice->configured())
        return m_voice->problem();
    // The local detector needs nothing more: the phrase is the model's.
    if (usingLocal())
        return QString();
    // No local detector: utterances are transcribed by Deepgram and compared
    // with the phrase. VoiceInput being configured (checked above) is what
    // makes that possible; without a Deepgram key its problem is the reason.
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

void WakeWord::setHandsOff(bool on)
{
    if (on == m_handsOff)
        return;
    m_handsOff = on;
    refresh();
    emit configChanged();
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
    if (m_enabled && m_local == Local::Untried && localWanted()
        && (m_handsOff || (m_voice && m_voice->enabled() && m_voice->configured())))
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
    else if (!m_handsOff)
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

QList<wake::PhraseModel> WakeWord::enabledPhrases() const
{
    QHash<QString, wake::PhraseModel> byId;
    for (const wake::PhraseModel &p : wake::discoverPhraseModels(m_modelFolder, m_bundledDir))
        byId.insert(p.fileName, p);
    QList<wake::PhraseModel> out;
    for (const QString &id : m_wakeWords)
        if (byId.contains(id))
            out << byId.value(id);
    return out;
}

bool WakeWord::localWanted() const
{
    if (m_localWantedCache < 0)
        m_localWantedCache = !enabledPhrases().isEmpty()
                             && !featureModel(m_modelFolder, m_bundledDir, kMelName).isEmpty()
                             && !featureModel(m_modelFolder, m_bundledDir, kEmbedName).isEmpty();
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
    emit phrasesChanged();
}

void WakeWord::setBundledDir(const QString &dir)
{
    if (dir == m_bundledDir)
        return;
    m_bundledDir = dir;
    m_localWantedCache = -1;
    m_local = Local::Untried;
    m_localError.clear();
    resetLocalDetector();
    refresh();
    emit detectorChanged();
    emit phrasesChanged();
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
    describe(kMelName, featureModel(m_modelFolder, m_bundledDir, kMelName));
    describe(kEmbedName, featureModel(m_modelFolder, m_bundledDir, kEmbedName));
    const QList<wake::PhraseModel> on = enabledPhrases();
    describe(QStringLiteral("wake word model (.onnx)"), on.isEmpty() ? QString() : on.first().path);
    s[QStringLiteral("files")] = files;
    s[QStringLiteral("phrases")] = phrases();
    return s;
}

QVariantList WakeWord::phrases() const
{
    QVariantList out;
    for (const wake::PhraseModel &p : wake::discoverPhraseModels(m_modelFolder, m_bundledDir)) {
        out.append(QVariantMap{
            { QStringLiteral("id"), p.fileName },
            { QStringLiteral("fileName"), p.fileName },
            { QStringLiteral("name"), p.name },
            { QStringLiteral("source"), p.userAdded ? QStringLiteral("user") : QStringLiteral("builtin") },
            { QStringLiteral("builtin"), p.builtin },
            { QStringLiteral("userAdded"), p.userAdded },
            { QStringLiteral("enabled"), m_wakeWords.contains(p.fileName) },
            { QStringLiteral("path"), p.path },
        });
    }
    return out;
}

void WakeWord::setWakeWords(const QStringList &ids)
{
    QStringList clean;
    for (const QString &id : ids) {
        const QString t = id.trimmed();
        if (!t.isEmpty() && !clean.contains(t))
            clean << t;
    }
    if (clean == m_wakeWords)
        return;
    m_wakeWords = clean;
    m_localWantedCache = -1;
    emit wakeWordsChanged();
    emit phrasesChanged();
    reloadLocal();
    refresh();
}

void WakeWord::setPhraseEnabled(const QString &id, bool on)
{
    if (on == m_wakeWords.contains(id))
        return;
    QStringList next = m_wakeWords;
    if (on)
        next << id;
    else
        next.removeAll(id);
    setWakeWords(next);
}

void WakeWord::reloadLocal()
{
    ++m_detGeneration;      // scores scored by the old classifier set are stale
    m_streaks.clear();
    m_localBuf.clear();
    if (m_local != Local::Untried) {
        m_local = Local::Untried;
        m_localError.clear();
        emit detectorChanged();
    }
}

QString WakeWord::addWakeWordModel(const QString &path)
{
    QString local = path.trimmed();
    if (local.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        local = QUrl(local).toLocalFile();
    const QFileInfo src(local);
    if (!src.isFile())
        return tr("That file could not be found.");
    if (src.suffix().compare(QStringLiteral("onnx"), Qt::CaseInsensitive) != 0)
        return tr("Wake words are .onnx files (openWakeWord models). Choose a file ending in .onnx.");
    const QString fileName = src.fileName();
    if (fileName.compare(kMelName, Qt::CaseInsensitive) == 0 || fileName.compare(kEmbedName, Qt::CaseInsensitive) == 0)
        return tr("That is one of the shared speech models, not a wake word. Choose the wake word's own .onnx file.");
    const QByteArray bytes = slurp(src.absoluteFilePath());
    if (bytes.isEmpty())
        return tr("That file could not be read.");
    // Check it the way the detector will use it: an openWakeWord phrase takes
    // the shared 16 x 96 features and returns one score. A model that fails
    // here would only fail later, silently, in the detector.
    QString why;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!ortrt::load({ m_modelFolder, appDir, appDir + QStringLiteral("/wakeword") }, &why))
        return tr("The model could not be checked: ONNX Runtime is not available (%1).").arg(why);
    ortrt::Session session;
    if (!session.open(bytes, &why))
        return tr("That is not a working ONNX model: %1").arg(why);
    std::vector<float> features(size_t(WakeDetector::kWakeEmbeddings) * size_t(WakeDetector::kEmbedSize), 0.0f);
    std::vector<float> out;
    std::vector<int64_t> outShape;
    if (!session.run(features.data(), { 1, WakeDetector::kWakeEmbeddings, WakeDetector::kEmbedSize }, &out, &outShape, &why)
        || out.empty() || !std::isfinite(out.front()))
        return tr("That model is not an openWakeWord wake word: it has to take the shared features and return a score.");
    QDir().mkpath(m_modelFolder);
    const QString target = QDir(m_modelFolder).filePath(fileName);
    if (QFileInfo(target) != src) {
        if (QFile::exists(target) && !QFile::remove(target))
            return tr("The copy already in the models folder could not be replaced.");
        if (!QFile::copy(src.absoluteFilePath(), target))
            return tr("The model could not be copied into the models folder.");
    }
    m_localWantedCache = -1;
    emit phrasesChanged();
    if (!m_wakeWords.contains(fileName))
        setWakeWords(m_wakeWords + QStringList{ fileName });   // on at once; reloads the detector
    else
        reloadLocal();   // same name, new bytes: the new copy is what should listen
    refresh();
    return QString();
}

QString WakeWord::removeWakeWordModel(const QString &id)
{
    QString name, path;
    bool builtin = false, userAdded = false, found = false;
    for (const wake::PhraseModel &p : wake::discoverPhraseModels(m_modelFolder, m_bundledDir)) {
        if (p.fileName != id)
            continue;
        found = true;
        name = p.name;
        path = p.path;
        builtin = p.builtin;
        userAdded = p.userAdded;
        break;
    }
    if (!found)
        return tr("That wake word is not in the list.");
    if (!userAdded)
        return tr("“%1” is built in and cannot be removed. You can switch it off instead.").arg(name);
    if (!QFile::remove(path))
        return tr("The model file could not be deleted.");
    m_localWantedCache = -1;
    emit phrasesChanged();
    if (!builtin && m_wakeWords.contains(id)) {
        // Nothing is left under that name: it leaves the enabled set too.
        QStringList next = m_wakeWords;
        next.removeAll(id);
        setWakeWords(next);
    } else {
        // It was shadowing a built-in phrase: the bundled copy takes over.
        reloadLocal();
        refresh();
    }
    return QString();
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
        connect(m_worker, &WakeWorker::scored, this, &WakeWord::onWorkerScores);
        m_workerThread.start();
    }
    setLocalState(Local::Loading);
    const QString folder = m_modelFolder;
    const QString bundled = m_bundledDir;
    QStringList ids;
    for (const wake::PhraseModel &p : enabledPhrases())
        ids << p.fileName;
    m_activeIds = ids;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, folder, bundled, ids] { w->init(folder, bundled, ids); },
                              Qt::QueuedConnection);
}

void WakeWord::onWorkerReady(bool ok, const QString &error)
{
    if (ok) {
        setLocalState(Local::Ready);
    } else {
        // Falls back to transcribing each sentence (Deepgram) and comparing it
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
    m_streaks.clear();
    m_localBuf.clear();
    if (m_worker && m_local == Local::Ready)
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->reset(); }, Qt::QueuedConnection);
}

void WakeWord::onWorkerScores(const QVector<float> &scores, quint64 generation)
{
    --m_inflight;
    if (generation != m_detGeneration || m_mode != Mode::Listening || !m_micOpen)
        return;
    if (scores.size() != m_activeIds.size())
        return;
    // One hot chunk is not a wake phrase: require a phrase's score to sustain
    // across consecutive callbacks (160 ms of it, not a click). Each phrase
    // keeps its own streak.
    int fired = -1;
    for (int i = 0; i < scores.size(); ++i) {
        if (wake::confirmSustained(m_streaks[m_activeIds.at(i)], scores.at(i), float(m_threshold), kWakeStreakNeeded)) {
            fired = i;
            break;
        }
    }
    if (fired < 0)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastCommandMs < m_cooldownMs || now - m_lastWakeMs < m_cooldownMs)
        return;
    m_lastWakeMs = now;
    const QString id = m_activeIds.at(fired);
    resetLocalDetector();       // what it just heard must not wake it a second time
    emit wokePhrase(id, wake::phraseDisplayName(id));
    emit woke();
    if (!m_handsOff)
        enterAwaiting();        // the request that follows is what Deepgram transcribes
}

void WakeWord::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
    emit failed(message);
}

#include "WakeWord.moc"
