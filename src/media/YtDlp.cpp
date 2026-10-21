#include "YtDlp.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
const char *const kExeName = "yt-dlp.exe";
#else
const char *const kExeName = "yt-dlp";
#endif

bool isYouTubeHost(const QString &host)
{
    const QString h = host.toLower();
    return h == QLatin1String("youtu.be") || h == QLatin1String("youtube.com") || h.endsWith(QLatin1String(".youtube.com"));
}

QString clean(const QString &s, int max)
{
    QString t = s.simplified();
    if (t.size() > max)
        t = t.left(max - 1).trimmed() + QStringLiteral("…");
    return t;
}

} // namespace

YtDlp::YtDlp(QObject *parent)
    : QObject(parent)
{
    refresh();
}

YtDlp::~YtDlp()
{
    for (const QPointer<QProcess> &p : std::as_const(m_procs)) {
        if (p) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(500);
        }
    }
    for (const QPointer<QNetworkReply> &r : std::as_const(m_replies)) {
        if (r) {
            r->disconnect(this);
            r->abort();
        }
    }
}

QString YtDlp::toolsFolder() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.noch");
    return QDir(base).filePath(QStringLiteral("tools"));
}

QString YtDlp::locate() const
{
    auto usable = [](const QString &p) {
        const QFileInfo fi(p);
        return fi.isFile() && fi.isExecutable() ? fi.absoluteFilePath() : QString();
    };
    if (!m_configured.isEmpty())
        return usable(m_configured);          // the user's choice: no silent fallback to something else
    if (!m_searchPath)
        return usable(QDir(toolsFolder()).filePath(QLatin1String(kExeName)));
    const QString next = usable(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/") + QLatin1String(kExeName)));
    if (!next.isEmpty())
        return next;
    const QString data = usable(QDir(toolsFolder()).filePath(QLatin1String(kExeName)));
    if (!data.isEmpty())
        return data;
    return QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
}

void YtDlp::setConfiguredPath(const QString &path)
{
    const QString p = path.trimmed();
    if (p == m_configured)
        return;
    m_configured = p;
    refresh();
}

void YtDlp::refresh()
{
    const QString found = locate();
    const bool changedPath = found != m_path;
    m_path = found;
    if (changedPath)
        m_version.clear();
    emit changed();
    if (m_path.isEmpty())
        return;
    run({ QStringLiteral("--version") }, 15000, [this, found](int code, const QByteArray &out, const QByteArray &, bool) {
        if (found != m_path)
            return;
        m_version = code == 0 ? QString::fromUtf8(out).simplified().left(40) : QString();
        if (code != 0)
            setMessage(tr("yt-dlp did not start. Install it again, or choose another copy."), true);
        else
            emit changed();
    });
}

void YtDlp::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit changed();
}

void YtDlp::setMessage(const QString &message, bool isError)
{
    m_message = message;
    m_messageIsError = isError;
    emit changed();
}

void YtDlp::openToolsFolder() const
{
    QDir().mkpath(toolsFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(toolsFolder()));
}

// ---- running it ------------------------------------------------------------------------------------

// One process, one argument vector (never a shell), killed if it takes too long.
void YtDlp::run(const QStringList &args, int timeoutMs,
                std::function<void(int, const QByteArray &, const QByteArray &, bool)> done)
{
    if (m_path.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done(-1, {}, QByteArray("yt-dlp is not installed."), false); });
        return;
    }
    auto *proc = new QProcess(this);
    m_procs.append(proc);
#ifdef Q_OS_WIN
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    auto *timer = new QTimer(proc);
    timer->setSingleShot(true);
    auto timedOut = std::make_shared<bool>(false);
    connect(timer, &QTimer::timeout, proc, [proc, timedOut] {
        *timedOut = true;
        proc->kill();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        m_procs.removeAll(proc);
        proc->deleteLater();
        done(-1, {}, QByteArray("yt-dlp could not be started."), false);
    });
    connect(proc, &QProcess::finished, this, [this, proc, done, timedOut](int code, QProcess::ExitStatus) {
        m_procs.removeAll(proc);
        const QByteArray out = proc->readAllStandardOutput();
        const QByteArray err = proc->readAllStandardError();
        proc->deleteLater();
        done(code, out, err, *timedOut);
    });
    proc->setProgram(m_path);
    proc->setArguments(args);
    proc->start();
    timer->start(timeoutMs);
}

// ---- looking things up ------------------------------------------------------------------------------

QString YtDlp::queryProblem(const QString &query)
{
    const QString q = query.simplified();
    if (q.isEmpty())
        return QObject::tr("Say what to play.");
    if (q.contains(QLatin1String("://")) || q.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        const QUrl u = QUrl::fromUserInput(q);
        if (!u.isValid() || !isYouTubeHost(u.host()) || (u.scheme() != QLatin1String("https") && u.scheme() != QLatin1String("http")))
            return QObject::tr("Only YouTube addresses can be played from here.");
    }
    return QString();
}

QString YtDlp::target(const QString &query)
{
    const QString q = query.simplified();
    if (q.contains(QLatin1String("://")) || q.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) {
        QUrl u = QUrl::fromUserInput(q);
        u.setScheme(QStringLiteral("https"));
        return u.toString(QUrl::FullyEncoded);
    }
    return QStringLiteral("ytsearch1:") + q.left(200);
}

QStringList YtDlp::lookupArguments(const QString &target)
{
    QStringList a = { QStringLiteral("--no-playlist"), QStringLiteral("--no-warnings"), QStringLiteral("--ignore-config"),
                      QStringLiteral("--encoding"), QStringLiteral("utf-8"), QStringLiteral("--socket-timeout"), QStringLiteral("15"),
                      QStringLiteral("--playlist-items"), QStringLiteral("1"),
                      QStringLiteral("-f"), QStringLiteral("bestaudio[ext=m4a]/bestaudio/best"),
                      QStringLiteral("--dump-single-json"), QStringLiteral("--skip-download") };
    // everything after this is the search term, never an option
    a << QStringLiteral("--") << target;
    return a;
}

OnlineTrack YtDlp::parse(const QByteArray &json, QString *error)
{
    OnlineTrack t;
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return OnlineTrack();
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject())
        return fail(QObject::tr("YouTube did not answer in a form Noch understands."));
    QJsonObject o = doc.object();
    // A search comes back as a playlist of results: take the first that can be played.
    if (o.value(QStringLiteral("_type")).toString() == QLatin1String("playlist")) {
        QJsonObject usable;
        for (const QJsonValue &v : o.value(QStringLiteral("entries")).toArray())
            if (v.isObject() && !v.toObject().value(QStringLiteral("url")).toString().isEmpty()) {
                usable = v.toObject();
                break;
            }
        if (usable.isEmpty())
            return fail(QObject::tr("Nothing playable was found."));
        o = usable;
    }
    const QString stream = o.value(QStringLiteral("url")).toString();
    if (!stream.startsWith(QLatin1String("https://")) && !stream.startsWith(QLatin1String("http://")))
        return fail(QObject::tr("No audio stream was found for that."));
    t.streamUrl = stream;
    t.id = o.value(QStringLiteral("id")).toString();
    t.webUrl = o.value(QStringLiteral("webpage_url")).toString();
    if (t.webUrl.isEmpty() && !t.id.isEmpty())
        t.webUrl = QStringLiteral("https://www.youtube.com/watch?v=") + t.id;
    // YouTube Music tracks carry the real song and artist; ordinary videos have a title and a channel.
    const QString track = o.value(QStringLiteral("track")).toString();
    QString artist = o.value(QStringLiteral("artist")).toString();
    if (!track.isEmpty()) {
        t.title = clean(track, 120);
    } else {
        t.title = clean(o.value(QStringLiteral("title")).toString(), 120);
        if (artist.isEmpty())
            artist = o.value(QStringLiteral("channel")).toString();
        if (artist.isEmpty())
            artist = o.value(QStringLiteral("uploader")).toString();
        artist.remove(QRegularExpression(QStringLiteral("\\s*-\\s*Topic$")));
    }
    t.artist = clean(artist, 80);
    t.thumbnailUrl = o.value(QStringLiteral("thumbnail")).toString();
    t.durationSec = o.value(QStringLiteral("duration")).toInt();
    t.live = o.value(QStringLiteral("is_live")).toBool(false);
    t.resolvedAtMs = QDateTime::currentMSecsSinceEpoch();
    if (t.title.isEmpty())
        t.title = QObject::tr("YouTube");
    return t;
}

void YtDlp::lookup(const QString &targetText, Done done)
{
    ++m_busy;
    setState(QStringLiteral("searching"));
    run(lookupArguments(targetText), m_timeoutMs, [this, done](int code, const QByteArray &out, const QByteArray &err, bool timedOut) {
        if (--m_busy <= 0) {
            m_busy = 0;
            setState(QStringLiteral("idle"));
        }
        if (timedOut) {
            done({}, tr("YouTube took too long to answer."));
            return;
        }
        if (code != 0 && out.isEmpty()) {
            QString why = QString::fromUtf8(err).simplified();
            // yt-dlp prints "ERROR: [youtube] id: message"; the message is the useful part.
            const int at = why.lastIndexOf(QLatin1String("ERROR:"));
            if (at >= 0)
                why = why.mid(at + 6).trimmed();
            done({}, why.isEmpty() ? tr("yt-dlp failed to find that.") : clean(why, 240));
            return;
        }
        QString error;
        const OnlineTrack t = parse(out, &error);
        done(t, t.valid() ? QString() : error);
    });
}

void YtDlp::search(const QString &query, Done done)
{
    if (const QString p = queryProblem(query); !p.isEmpty()) {
        QTimer::singleShot(0, this, [done, p] { done({}, p); });
        return;
    }
    if (!available()) {
        QTimer::singleShot(0, this, [done] { done({}, QObject::tr("yt-dlp is not installed. Install it in Settings > Agent & tools.")); });
        return;
    }
    lookup(target(query), std::move(done));
}

void YtDlp::resolve(const QString &webUrl, Done done)
{
    search(webUrl, std::move(done));
}

// ---- installing ---------------------------------------------------------------------------------------

QString YtDlp::checksumFor(const QByteArray &sums, const QString &file)
{
    for (const QByteArray &raw : sums.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const int sp = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (sp < 0)
            continue;
        QString name = line.mid(sp).trimmed();
        if (name.startsWith(QLatin1Char('*')))
            name.remove(0, 1);
        if (name == file && line.left(sp).size() == 64)
            return line.left(sp).toLower();
    }
    return QString();
}

void YtDlp::fetch(const QUrl &url, std::function<void(const QByteArray &, const QString &)> done)
{
    if (url.scheme() != QLatin1String("https") && !(url.scheme() == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1"))) {
        QTimer::singleShot(0, this, [done] { done({}, QObject::tr("Downloads must use https.")); });
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);   // never https -> http
    req.setTransferTimeout(120000);
    QNetworkReply *reply = m_nam.get(req);
    m_replies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        m_replies.removeAll(reply);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            done({}, status > 0 ? tr("The download answered HTTP %1.").arg(status) : tr("Could not reach the download server."));
            return;
        }
        done(reply->readAll(), QString());
    });
}

void YtDlp::finishInstall(bool ok, const QString &message)
{
    setMessage(message, !ok);
    setState(QStringLiteral("idle"));
    refresh();
}

void YtDlp::install()
{
    if (m_state != QLatin1String("idle"))
        return;
#ifndef Q_OS_WIN
    setMessage(tr("Install yt-dlp with your package manager (for example: pip install yt-dlp)."), true);
    return;
#endif
    setState(QStringLiteral("installing"));
    setMessage(tr("Downloading yt-dlp…"), false);
    const QString base = m_releaseBase;
    fetch(QUrl(base + QStringLiteral("/SHA2-256SUMS")), [this, base](const QByteArray &sums, const QString &err1) {
        if (!err1.isEmpty()) {
            finishInstall(false, err1);
            return;
        }
        const QString expected = checksumFor(sums, QLatin1String(kExeName));
        if (expected.isEmpty()) {
            finishInstall(false, tr("The release did not list a checksum for yt-dlp, so it was not installed."));
            return;
        }
        fetch(QUrl(base + QLatin1Char('/') + QLatin1String(kExeName)), [this, expected](const QByteArray &exe, const QString &err2) {
            if (!err2.isEmpty()) {
                finishInstall(false, err2);
                return;
            }
            const QString actual = QString::fromLatin1(QCryptographicHash::hash(exe, QCryptographicHash::Sha256).toHex());
            if (actual != expected) {
                finishInstall(false, tr("The download did not match the published checksum, so it was thrown away."));
                return;
            }
            QDir().mkpath(toolsFolder());
            QSaveFile out(QDir(toolsFolder()).filePath(QLatin1String(kExeName)));
            if (!out.open(QIODevice::WriteOnly) || out.write(exe) != exe.size() || !out.commit()) {
                finishInstall(false, tr("Could not save yt-dlp in %1.").arg(QDir::toNativeSeparators(toolsFolder())));
                return;
            }
            m_configured.clear();           // the copy just installed is the one to use
            finishInstall(true, tr("yt-dlp installed."));
        });
    });
}

void YtDlp::update()
{
    if (m_state != QLatin1String("idle") || m_path.isEmpty())
        return;
    setState(QStringLiteral("updating"));
    setMessage(tr("Updating yt-dlp…"), false);
    run({ QStringLiteral("-U") }, 120000, [this](int code, const QByteArray &out, const QByteArray &err, bool timedOut) {
        const QString text = QString::fromUtf8(out + err).simplified();
        finishInstall(code == 0 && !timedOut, timedOut ? tr("The update took too long.")
                                              : code == 0 ? (text.isEmpty() ? tr("yt-dlp is up to date.") : clean(text, 200))
                                                          : (text.isEmpty() ? tr("The update failed.") : clean(text, 200)));
    });
}
