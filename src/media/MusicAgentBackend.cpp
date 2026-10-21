#include "MusicAgentBackend.h"

#include "MediaHub.h"
#include "MusicPlayer.h"
#include "YtDlp.h"

#include <QFileInfo>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>

MusicAgentBackend::MusicAgentBackend(MediaHub *hub, MusicPlayer *player, YtDlp *youtube)
    : m_hub(hub)
    , m_player(player)
    , m_youtube(youtube)
{
    // A song played again hours later needs a fresh stream address.
    if (m_player && m_youtube)
        m_player->setOnlineResolver([yt = QPointer<YtDlp>(m_youtube)](const QString &page,
                                                                         std::function<void(const OnlineTrack &, const QString &)> done) {
            if (!yt) {
                done({}, QStringLiteral("yt-dlp is gone."));
                return;
            }
            yt->resolve(page, std::move(done));
        });
}

bool MusicAgentBackend::onlineReady(QString *why) const
{
    if (!m_hub || !m_player) {
        if (why)
            *why = QStringLiteral("There is no music player.");
        return false;
    }
    if (!m_youtube || !m_youtube->available()) {
        if (why)
            *why = QStringLiteral("yt-dlp is not installed, so songs cannot be played from YouTube. "
                                  "Ask the user to press Install yt-dlp in Settings > Agent & tools.");
        return false;
    }
    return true;
}

void MusicAgentBackend::playOnline(const QString &query, std::function<void(const QString &, const QString &)> done)
{
    QString why;
    if (!onlineReady(&why)) {
        done(QString(), why);
        return;
    }
    m_youtube->search(query, [this, done, hub = QPointer<MediaHub>(m_hub), player = QPointer<MusicPlayer>(m_player)](
                                  const OnlineTrack &track, const QString &error) {
        if (!hub || !player) {
            done(QString(), QStringLiteral("The player went away."));
            return;
        }
        if (!track.valid()) {
            done(QString(), error.isEmpty() ? QStringLiteral("Nothing was found.") : error);
            return;
        }
        hub->selectSource(QStringLiteral("local"));   // the pill shows Noch's player, not another app's session
        player->playOnline(track);
        done(track.artist.isEmpty() ? track.title : track.title + QStringLiteral(" — ") + track.artist, QString());
    });
}

MusicStatus MusicAgentBackend::status() const
{
    MusicStatus s;
    if (!m_hub || !m_player)
        return s;
    s.ready = true;
    s.playing = m_hub->playing();
    s.hasTrack = m_hub->hasTrack();
    s.title = m_hub->title();
    s.artist = m_hub->artist();
    s.source = m_hub->sourceName();
    s.local = m_hub->source() != QLatin1String("system");
    s.queueCount = m_player->count();
    s.volumePct = qBound(0, qRound(m_hub->volume() * 100.0), 100);
    s.muted = m_hub->muted();
    return s;
}

bool MusicAgentBackend::play(QString *why)
{
    if (!m_hub || !m_player) {
        if (why)
            *why = QStringLiteral("There is no music player.");
        return false;
    }
    if (m_hub->playing())
        return true;
    if (m_hub->source() == QLatin1String("system")) {
        m_hub->toggle();   // resume the other app's session
        return true;
    }
    if (m_player->count() == 0) {
        if (why)
            *why = QStringLiteral("Nothing is queued in Noch's player and no other app is playing.");
        return false;
    }
    m_hub->selectSource(QStringLiteral("local"));
    m_player->toggle();
    return true;
}

void MusicAgentBackend::pause()
{
    if (m_hub && m_hub->playing())
        m_hub->toggle();
}

void MusicAgentBackend::next()
{
    if (m_hub)
        m_hub->next();
}

void MusicAgentBackend::previous()
{
    if (m_hub)
        m_hub->previous();
}

void MusicAgentBackend::setVolume(int percent)
{
    if (m_hub)
        m_hub->setVolume(qBound(0, percent, 100) / 100.0);
}

void MusicAgentBackend::setMuted(bool muted)
{
    if (m_hub && m_hub->muted() != muted)
        m_hub->toggleMute();
}

QStringList MusicAgentBackend::queueTitles(int limit) const
{
    QStringList out;
    if (!m_player)
        return out;
    const QVariantList tracks = m_player->tracks();
    for (const QVariant &v : tracks) {
        if (out.size() >= limit)
            break;
        const QVariantMap t = v.toMap();
        const QString artist = t.value(QStringLiteral("artist")).toString();
        const QString title = t.value(QStringLiteral("title")).toString();
        out << (artist.isEmpty() ? title : title + QStringLiteral(" — ") + artist);
    }
    return out;
}

int MusicAgentBackend::playMatching(const QString &query, QString *played)
{
    if (!m_player || !m_hub)
        return 0;
    const QStringList words = query.toLower().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return 0;
    const QVariantList tracks = m_player->tracks();
    int first = -1;
    int matches = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        const QVariantMap t = tracks.at(i).toMap();
        const QString hay = (t.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
                             + t.value(QStringLiteral("artist")).toString() + QLatin1Char(' ')
                             + QFileInfo(t.value(QStringLiteral("path")).toString()).completeBaseName()).toLower();
        bool all = true;
        for (const QString &w : words)
            all = all && hay.contains(w);
        if (!all)
            continue;
        ++matches;
        if (first < 0)
            first = i;
    }
    if (first < 0)
        return 0;
    m_hub->selectSource(QStringLiteral("local"));
    m_player->setIndex(first);
    if (played)
        *played = tracks.at(first).toMap().value(QStringLiteral("title")).toString();
    return matches;
}

QString MusicAgentBackend::musicFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
}

void MusicAgentBackend::add(const QString &path, std::function<void(int, const QString &)> done)
{
    if (!m_player) {
        done(0, QStringLiteral("There is no music player."));
        return;
    }
    const QFileInfo info(path);
    if (!info.exists()) {
        done(0, QStringLiteral("'%1' does not exist.").arg(path));
        return;
    }
    const int before = m_player->count();
    const QVariantList urls{ QUrl::fromLocalFile(info.absoluteFilePath()) };
    const int added = m_player->addUrls(urls);
    if (!info.isDir()) {
        done(added, added > 0 ? QString()
                              : QStringLiteral("That is not a supported audio file, or it is already queued."));
        return;
    }

    // A folder is scanned on a worker thread: wait for the queue to change, but
    // not for ever (an empty or huge folder just ends the wait).
    auto finished = std::make_shared<bool>(false);
    auto finish = [this, before, done, finished]() {
        if (*finished)
            return;
        *finished = true;
        const int now = m_player ? m_player->count() : before;
        done(qMax(0, now - before),
             now > before ? QString() : QStringLiteral("No new audio files were found in that folder."));
    };
    auto *ctx = new QObject(m_player);
    QObject::connect(m_player, &MusicPlayer::playlistChanged, ctx, [finish, ctx]() {
        // Let the rest of the scan land, then report once.
        QTimer::singleShot(600, ctx, [finish, ctx]() { finish(); ctx->deleteLater(); });
    });
    QObject::connect(m_player, &MusicPlayer::folderScanNotice, ctx, [finish, ctx](const QString &) {
        finish();
        ctx->deleteLater();
    });
    QTimer::singleShot(6000, ctx, [finish, ctx]() { finish(); ctx->deleteLater(); });
}
