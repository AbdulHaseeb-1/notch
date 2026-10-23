#include "MusicPlayer.h"

#include "ImageAlpha.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QSet>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace {

const QStringList &audioSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("wav"),
        QStringLiteral("m4a"),  QStringLiteral("aac"),  QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("wma"),  QStringLiteral("aiff"),
        QStringLiteral("aif"),  QStringLiteral("alac"), QStringLiteral("m4b"),
    };
    return suffixes;
}

bool isAudioFile(const QString &path)
{
    return audioSuffixes().contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

QUrl urlFromVariant(const QVariant &value)
{
    const QUrl url = value.toUrl();
    if (url.isValid() && !url.isEmpty())
        return url;
    return QUrl::fromUserInput(value.toString());
}

QString localPath(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString(QUrl::PreferLocalFile);
    if (path.isEmpty())
        return {};
    return QFileInfo(path).absoluteFilePath();
}

bool naturalLess(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qulonglong na = 0;
            qulonglong nb = 0;
            while (i < a.size() && a.at(i).isDigit()) {
                na = na * 10 + quint64(a.at(i).digitValue());
                ++i;
            }
            while (j < b.size() && b.at(j).isDigit()) {
                nb = nb * 10 + quint64(b.at(j).digitValue());
                ++j;
            }
            if (na != nb)
                return na < nb;
        } else {
            const QChar ca = a.at(i).toLower();
            const QChar cb = b.at(j).toLower();
            if (ca != cb)
                return ca < cb;
            ++i;
            ++j;
        }
    }
    return a.size() < b.size();
}

QString stringOrList(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        const QString joined = value.toStringList().join(QStringLiteral(", "));
        if (!joined.isEmpty())
            return joined;
    }
    return value.toString();
}

} // namespace

MusicPlayer::MusicPlayer(QObject *parent)
    : QObject(parent)
{
    m_saveTimer.setInterval(400);
    m_saveTimer.setSingleShot(true);
    connect(&m_saveTimer, &QTimer::timeout, this, &MusicPlayer::saveNow);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &MusicPlayer::saveNow);

    load();
}

// The media backend (Qt's player and its FFmpeg decoder) costs start-up time and
// memory, so it is only created the first time something is played. Until then the
// saved queue and position are kept here and no file is opened.
void MusicPlayer::ensureBackend()
{
    if (m_player)
        return;
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(float(m_volume));
    m_audio->setMuted(m_muted);

    connect(m_player, &QMediaPlayer::positionChanged, this, &MusicPlayer::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MusicPlayer::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            m_failStreak = 0;
        emit stateChanged();
        if (state != QMediaPlayer::PlayingState)
            scheduleSave();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            if (m_pendingSeek >= 0) {
                qint64 seekTo = m_pendingSeek;
                m_pendingSeek = -1;
                const qint64 length = m_player->duration();
                if (length > 0 && seekTo > length - 1500)
                    seekTo = 0;
                if (seekTo > 0)
                    m_player->setPosition(seekTo);
            }
            readMetadata();
        } else if (status == QMediaPlayer::EndOfMedia) {
            handleEnded();
        }
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &MusicPlayer::readMetadata);
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &) {
        if (error != QMediaPlayer::NoError)
            advanceAfterError();
    });
}

MusicPlayer::~MusicPlayer()
{
    // Wait for any folder scan instead of destroying it mid-run: QThread
    //::create threads are freed by deleteLater once done, and the scan is
    // bounded, so patience is cheap here.
    if (m_scanThread)
        m_scanThread->wait(30000);
    saveNow();
}

bool MusicPlayer::hasTrack() const
{
    return m_index >= 0 && m_index < m_tracks.size();
}

QString MusicPlayer::title() const
{
    if (!hasTrack())
        return {};
    const Track &track = m_tracks.at(m_index);
    if (!track.title.isEmpty())
        return track.title;
    return QFileInfo(track.path).completeBaseName();
}

QString MusicPlayer::artist() const
{
    return hasTrack() ? m_tracks.at(m_index).artist : QString();
}

QString MusicPlayer::album() const
{
    return hasTrack() ? m_tracks.at(m_index).album : QString();
}

qint64 MusicPlayer::position() const
{
    return m_player ? m_player->position() : qMax<qint64>(0, m_pendingSeek);
}

qint64 MusicPlayer::duration() const
{
    return m_player ? m_player->duration() : 0;
}

int MusicPlayer::state() const
{
    if (!m_player)
        return 0;
    switch (m_player->playbackState()) {
    case QMediaPlayer::PlayingState:
        return 1;
    case QMediaPlayer::PausedState:
        return 2;
    default:
        return 0;
    }
}

void MusicPlayer::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qAbs(m_volume - volume) < 0.001)
        return;
    m_volume = volume;
    if (m_muted && volume > 0.001) {
        m_muted = false;
        if (m_audio)
            m_audio->setMuted(false);
        emit mutedChanged();
    }
    if (m_audio)
        m_audio->setVolume(float(volume));
    emit volumeChanged();
    scheduleSave();
}

void MusicPlayer::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    if (m_audio)
        m_audio->setMuted(muted);
    emit mutedChanged();
    scheduleSave();
}

void MusicPlayer::toggleMute()
{
    setMuted(!m_muted);
}

void MusicPlayer::setRepeatAll(bool repeat)
{
    setRepeatMode(repeat ? 1 : 0);
}

void MusicPlayer::setRepeatMode(int mode)
{
    mode = qBound(0, mode, 2);
    if (m_repeatMode == mode)
        return;
    m_repeatMode = mode;
    emit repeatModeChanged();
    emit repeatAllChanged();
    scheduleSave();
}

void MusicPlayer::cycleRepeat()
{
    setRepeatMode((m_repeatMode + 1) % 3);
}

void MusicPlayer::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle)
        return;
    m_shuffle = shuffle;
    emit shuffleChanged();
    scheduleSave();
}

QVariantList MusicPlayer::tracks() const
{
    QVariantList list;
    list.reserve(m_tracks.size());
    for (const Track &track : m_tracks) {
        QVariantMap entry;
        entry.insert(QStringLiteral("title"),
                     track.title.isEmpty() ? QFileInfo(track.path).completeBaseName() : track.title);
        entry.insert(QStringLiteral("artist"), track.artist);
        entry.insert(QStringLiteral("path"), track.path);
        list.push_back(entry);
    }
    return list;
}

QImage MusicPlayer::coverImage() const
{
    QMutexLocker lock(&m_coverMutex);
    return m_cover;
}

void MusicPlayer::toggle()
{
    if (m_tracks.isEmpty())
        return;
    if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        return;
    }
    if (!hasTrack() || !m_player || m_player->mediaStatus() == QMediaPlayer::NoMedia) {
        // The first play of a restored track resumes at its saved position, which
        // playIndex() would otherwise forget.
        const qint64 resumeAt = m_player ? -1 : m_pendingSeek;
        playIndex(qMax(0, m_index), true);
        m_pendingSeek = resumeAt;
    } else {
        m_player->play();
    }
}

void MusicPlayer::next()
{
    if (m_tracks.isEmpty())
        return;
    if (m_shuffle && m_tracks.size() > 1) {
        int pick = QRandomGenerator::global()->bounded(m_tracks.size() - 1);
        if (pick >= m_index)
            ++pick;
        playIndex(pick, true);
        return;
    }
    if (m_index + 1 < m_tracks.size())
        playIndex(m_index + 1, true);
    else if (m_repeatMode > 0)
        playIndex(0, true);
    else if (m_player)
        m_player->pause();
}

void MusicPlayer::previous()
{
    if (m_tracks.isEmpty())
        return;
    if (position() > 3000) {
        if (m_player)
            m_player->setPosition(0);
        else
            m_pendingSeek = 0;
        return;
    }
    if (m_index > 0)
        playIndex(m_index - 1, true);
    else if (m_repeatMode > 0)
        playIndex(m_tracks.size() - 1, true);
    else if (m_player)
        m_player->setPosition(0);
}

void MusicPlayer::seek(qint64 ms)
{
    if (!m_player) {
        // Nothing is open yet (a restored queue, not played): the position is
        // applied when the track loads.
        if (hasTrack())
            m_pendingSeek = qMax<qint64>(0, ms);
        return;
    }
    if (m_player->duration() <= 0)
        return;
    m_player->setPosition(qBound(qint64(0), ms, m_player->duration()));
}

void MusicPlayer::setIndex(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    playIndex(index, true);
}

int MusicPlayer::addUrls(const QVariantList &urls)
{
    // Only auto-start when nothing has been started yet: hijacking a paused
    // session silently rewound the user's spot in the old behaviour.
    const bool startPlaying = m_tracks.isEmpty() || state() == 0;
    const int firstNew = m_tracks.size();
    int added = 0;
    for (const QVariant &value : urls) {
        const QUrl url = urlFromVariant(value);
        const QString path = localPath(url);
        QFileInfo info(path);
        if (info.isDir()) {
            enqueueFolderScan(info.absoluteFilePath());
            continue;
        }
        added += addPath(path);
    }
    if (added <= 0)
        return 0;
    emit playlistChanged();
    saveNow();
    if (startPlaying)
        playIndex(qMin(firstNew, m_tracks.size() - 1), true);
    return added;
}

void MusicPlayer::remove(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    const bool current = index == m_index;
    const bool wasPlaying = state() == 1;
    m_tracks.removeAt(index);
    if (m_tracks.isEmpty()) {
        m_index = -1;
        if (m_player) {
            m_player->stop();
            m_player->setSource({});
        }
        setCover({});
        emit trackChanged();
    } else if (current) {
        playIndex(qMin(index, m_tracks.size() - 1), wasPlaying);
    } else if (index < m_index) {
        --m_index;
        emit trackChanged();
    }
    emit playlistChanged();
    saveNow();
}

void MusicPlayer::clear()
{
    if (m_player) {
        m_player->stop();
        m_player->setSource({});
    }
    m_pendingSeek = -1;
    m_tracks.clear();
    m_index = -1;
    setCover({});
    emit trackChanged();
    emit playlistChanged();
    saveNow();
}

QVariantMap MusicPlayer::ingestDrop(const QVariantList &urls)
{
    const bool startPlaying = m_tracks.isEmpty() || state() == 0;
    int firstNew = m_tracks.size();
    int added = 0;
    int folders = 0;
    QVariantList other;
    for (const QVariant &value : urls) {
        const QUrl url = urlFromVariant(value);
        const QString path = localPath(url);
        const QFileInfo info(path);
        if (info.isDir()) {
            // Scans run on a worker and report later; the drop is not "other".
            enqueueFolderScan(info.absoluteFilePath());
            ++folders;
            continue;
        }
        const int n = addPath(path);
        if (n > 0)
            added += n;
        else
            other.push_back(QVariant::fromValue(url));
    }
    if (folders > 0) {
        emit folderScanNotice(folders == 1
                                  ? QStringLiteral("Scanning folder…")
                                  : QStringLiteral("Scanning %1 folders…").arg(folders));
    }
    if (added > 0) {
        emit playlistChanged();
        saveNow();
        if (startPlaying && !m_scanActive)
            playIndex(qMin(firstNew, m_tracks.size() - 1), true);
    }
    QVariantMap result;
    result.insert(QStringLiteral("added"), added);
    result.insert(QStringLiteral("folders"), folders);
    result.insert(QStringLiteral("other"), other);
    return result;
}

void MusicPlayer::load()
{
    m_loading = true;
    QSettings settings;
    settings.beginGroup(QStringLiteral("player"));
    m_volume = qBound(0.0, settings.value(QStringLiteral("volume"), 0.8).toDouble(), 1.0);
    m_muted = settings.value(QStringLiteral("muted"), false).toBool();
    if (settings.contains(QStringLiteral("repeatMode")))
        m_repeatMode = qBound(0, settings.value(QStringLiteral("repeatMode"), 1).toInt(), 2);
    else
        m_repeatMode = settings.value(QStringLiteral("repeat"), true).toBool() ? 1 : 0;
    m_shuffle = settings.value(QStringLiteral("shuffle"), false).toBool();

    const QStringList paths = settings.value(QStringLiteral("tracks")).toStringList();
    for (const QString &path : paths) {
        if (QFileInfo::exists(path) && isAudioFile(path))
            m_tracks.append(Track{QFileInfo(path).absoluteFilePath(), {}, {}, {}});
    }
    const int savedIndex = settings.value(QStringLiteral("index"), 0).toInt();
    const qint64 savedPosition = settings.value(QStringLiteral("position"), 0).toLongLong();
    settings.endGroup();

    if (!m_tracks.isEmpty()) {
        // Opened when it is first played (see toggle()), so start-up does not decode it.
        m_index = qBound(0, savedIndex, m_tracks.size() - 1);
        m_pendingSeek = savedPosition;
    }
    m_loading = false;
    emit playlistChanged();
    emit trackChanged();
    emit volumeChanged();
    emit mutedChanged();
    emit repeatAllChanged();
    emit repeatModeChanged();
    emit shuffleChanged();
}

void MusicPlayer::saveNow()
{
    if (m_loading)
        return;
    // Songs from the internet are for this session only (their stream addresses expire): the
    // saved queue and the position in it leave them out.
    int savedIndex = 0;
    const QStringList paths = persistentPaths(&savedIndex);
    const bool currentSaved = hasTrack() && !m_tracks.at(m_index).online;

    QSettings settings;
    settings.beginGroup(QStringLiteral("player"));
    settings.setValue(QStringLiteral("volume"), m_volume);
    settings.setValue(QStringLiteral("muted"), m_muted);
    settings.setValue(QStringLiteral("repeat"), m_repeatMode > 0);
    settings.setValue(QStringLiteral("repeatMode"), m_repeatMode);
    settings.setValue(QStringLiteral("shuffle"), m_shuffle);
    settings.setValue(QStringLiteral("index"), savedIndex);
    settings.setValue(QStringLiteral("position"), currentSaved ? position() : 0);
    settings.setValue(QStringLiteral("tracks"), paths);
    settings.endGroup();
}

void MusicPlayer::scheduleSave()
{
    if (!m_loading)
        m_saveTimer.start();
}

int MusicPlayer::addPath(const QString &path)
{
    if (path.isEmpty())
        return 0;
    const QFileInfo info(path);
    if (info.isDir()) {
        enqueueFolderScan(info.absoluteFilePath());
        return 0;
    }

    if (!info.isFile() || !isAudioFile(path))
        return 0;
    const QString absolute = info.absoluteFilePath();
    if (knownTrackPaths().contains(absolute.toLower()))
        return 0;
    m_tracks.append(Track{absolute, {}, {}, {}});
    return 1;
}

QSet<QString> MusicPlayer::knownTrackPaths() const
{
    QSet<QString> known;
    known.reserve(m_tracks.size());
    for (const Track &track : m_tracks)
        known.insert(track.path.toLower());
    return known;
}

void MusicPlayer::enqueueFolderScan(const QString &root)
{
    const QString clean = QDir::cleanPath(root);
    if (clean.isEmpty() || m_scanQueue.contains(clean))
        return;
    m_scanQueue.append(clean);
    startNextFolderScan();
}

void MusicPlayer::startNextFolderScan()
{
    if (m_scanActive || m_scanQueue.isEmpty())
        return;
    const QString root = m_scanQueue.takeFirst();
    m_scanActive = true;
    const bool fromRoot = QFileInfo(root).isRoot();

    QThread *thread = QThread::create([this, root, fromRoot]() {
        // Worker thread: filesystem walking only, no GUI objects touched.
        QStringList patterns;
        for (const QString &suffix : audioSuffixes())
            patterns.push_back(QStringLiteral("*.") + suffix);

        QStringList found;
        bool truncated = false;
        int visited = 0;
        QDirIterator it(root, patterns, QDir::Files,
                        fromRoot ? QDirIterator::NoIteratorFlags
                                 : QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (found.size() >= 300 || visited >= 4000) {
                // Bounded: big trees are capped instead of walked for minutes.
                truncated = true;
                break;
            }
            found.push_back(QFileInfo(it.next()).absoluteFilePath());
            ++visited;
        }
        QMetaObject::invokeMethod(this,
                                  [this, root, found = std::move(found),
                                   truncated]() mutable {
                                      applyFolderScan(root, found, truncated);
                                  },
                                  Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_scanThread = thread;
    thread->start();
}

void MusicPlayer::applyFolderScan(const QString &root, const QStringList &files, bool truncated)
{
    Q_UNUSED(root)
    m_scanThread = nullptr; // the thread is done (deleteLater owns cleanup)

    // Sort by full path so subdirectories stay in order instead of merging
    // "01 …" files from every album folder into one scramble.
    QStringList sorted = files;
    std::sort(sorted.begin(), sorted.end(),
              [](const QString &a, const QString &b) { return naturalLess(a, b); });

    const QSet<QString> known = knownTrackPaths();
    bool addedTracks = false;
    for (const QString &file : sorted) {
        if (!known.contains(file.toLower())) {
            m_tracks.append(Track{file, {}, {}, {}});
            addedTracks = true;
        }
    }
    if (addedTracks) {
        emit playlistChanged();
        saveNow();
        // Only jump-start playback when the player is idle; a paused spot in
        // the queue stays paused (see addUrls).
        if (state() == 0)
            playIndex(qMax(0, m_index), true);
    } else {
        saveNow();
    }
    if (truncated)
        emit folderScanNotice(QStringLiteral("Added the first %1 songs; the rest was not queued")
                                  .arg(m_tracks.size()));

    m_scanActive = false;
    startNextFolderScan();
}

void MusicPlayer::playIndex(int index, bool start)
{
    if (m_tracks.isEmpty()) {
        m_index = -1;
        emit trackChanged();
        return;
    }
    if (index < 0)
        index = m_tracks.size() - 1;
    if (index >= m_tracks.size())
        index = 0;

    ensureBackend();
    m_index = index;
    m_pendingSeek = -1;
    setCover({});
    const Track &t = m_tracks.at(m_index);
    // A stream address from YouTube is good for some hours; a track replayed after that is renewed first.
    constexpr qint64 kStreamLifeMs = 4LL * 60 * 60 * 1000;
    if (t.online && m_resolver && QDateTime::currentMSecsSinceEpoch() - t.resolvedAtMs > kStreamLifeMs) {
        emit trackChanged();
        renewAndPlay(index, start);
        return;
    }
    m_player->setSource(t.online ? QUrl(t.streamUrl) : QUrl::fromLocalFile(t.path));
    if (t.online && !t.thumbnailUrl.isEmpty())
        loadThumbnail(t.thumbnailUrl, t.path);
    emit trackChanged();
    if (start)
        m_player->play();
    scheduleSave();
}

void MusicPlayer::renewAndPlay(int index, bool start)
{
    ensureBackend();
    const QString page = m_tracks.at(index).path;
    m_resolver(page, [this, page, start](const OnlineTrack &fresh, const QString &error) {
        // Still the track the user is on? (They may have skipped while it was being renewed.)
        if (m_index < 0 || m_index >= m_tracks.size() || m_tracks.at(m_index).path != page)
            return;
        if (!fresh.valid()) {
            emit playbackError(error.isEmpty() ? QStringLiteral("Couldn't play this track") : error);
            return;
        }
        Track &t = m_tracks[m_index];
        t.streamUrl = fresh.streamUrl;
        t.resolvedAtMs = fresh.resolvedAtMs;
        m_player->setSource(QUrl(t.streamUrl));
        if (!t.thumbnailUrl.isEmpty())
            loadThumbnail(t.thumbnailUrl, t.path);
        if (start)
            m_player->play();
    });
}

QStringList MusicPlayer::persistentPaths(int *savedIndex) const
{
    QStringList paths;
    paths.reserve(m_tracks.size());
    int idx = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).online)
            continue;
        if (i <= m_index)
            idx = int(paths.size());
        paths.push_back(m_tracks.at(i).path);
    }
    if (savedIndex)
        *savedIndex = idx;
    return paths;
}

bool MusicPlayer::currentIsOnline() const
{
    return hasTrack() && m_tracks.at(m_index).online;
}

void MusicPlayer::playOnline(const OnlineTrack &online)
{
    if (!online.valid())
        return;
    Track t;
    t.path = online.webUrl.isEmpty() ? online.streamUrl : online.webUrl;
    t.title = online.title;
    t.artist = online.artist;
    t.album = QStringLiteral("YouTube");
    t.online = true;
    t.streamUrl = online.streamUrl;
    t.thumbnailUrl = online.thumbnailUrl;
    t.resolvedAtMs = online.resolvedAtMs ? online.resolvedAtMs : QDateTime::currentMSecsSinceEpoch();
    // Next in line, so what was queued carries on afterwards; asking for the same song again replaces it.
    for (int i = m_tracks.size() - 1; i >= 0; --i)
        if (m_tracks.at(i).online && m_tracks.at(i).path == t.path) {
            m_tracks.removeAt(i);
            if (i < m_index)
                --m_index;
        }
    const int at = hasTrack() ? m_index + 1 : m_tracks.size();
    m_tracks.insert(at, t);
    emit playlistChanged();
    playIndex(at, true);
}

void MusicPlayer::loadThumbnail(const QString &url, const QString &forPage)
{
    if (m_thumbReply) {
        QNetworkReply *old = m_thumbReply;
        m_thumbReply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    const QUrl u(url);
    if (u.scheme() != QLatin1String("https") && !(u.scheme() == QLatin1String("http") && u.host() == QLatin1String("127.0.0.1")))
        return;
    QNetworkRequest req(u);
    req.setTransferTimeout(15000);
    m_thumbReply = m_nam.get(req);
    QNetworkReply *reply = m_thumbReply;
    // A picture is a few hundred KB; stop reading (and drop the download) past
    // 8 MiB instead of buffering whatever a metadata URL points at.
    constexpr qsizetype kMaxThumbBytes = 8 * 1024 * 1024;
    auto body = std::make_shared<QByteArray>();
    auto tooBig = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, body, tooBig] {
        if (*tooBig)
            return;
        body->append(reply->readAll());
        if (body->size() > kMaxThumbBytes) {
            *tooBig = true;
            body->clear();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, tooBig, forPage] {
        if (m_thumbReply == reply)
            m_thumbReply = nullptr;
        reply->deleteLater();
        if (*tooBig || reply->error() != QNetworkReply::NoError)
            return;
        if (!hasTrack() || m_tracks.at(m_index).path != forPage)
            return;                         // the track changed while it downloaded
        body->append(reply->readAll());
        const QImage image = QImage::fromData(*body);
        if (!image.isNull())
            setCover(image);
    });
}

void MusicPlayer::readMetadata()
{
    if (!hasTrack())
        return;
    // A song from the internet already came with its title, artist and picture.
    if (m_tracks.at(m_index).online)
        return;
    const QMediaMetaData meta = m_player->metaData();
    Track &track = m_tracks[m_index];
    const QString title = meta.stringValue(QMediaMetaData::Title).trimmed();
    QString artist = meta.stringValue(QMediaMetaData::AlbumArtist).trimmed();
    if (artist.isEmpty())
        artist = stringOrList(meta.value(QMediaMetaData::ContributingArtist)).trimmed();
    const QString album = meta.stringValue(QMediaMetaData::AlbumTitle).trimmed();

    const bool changed = title != track.title || artist != track.artist || album != track.album;
    if (!title.isEmpty())
        track.title = title;
    track.artist = artist;
    track.album = album;
    if (changed) {
        emit trackChanged();
        emit playlistChanged();
    }

    QImage cover = meta.value(QMediaMetaData::CoverArtImage).value<QImage>();
#if QT_VERSION < QT_VERSION_CHECK(6, 12, 0)
    if (cover.isNull())
        cover = meta.value(QMediaMetaData::ThumbnailImage).value<QImage>();
#endif
    if (!cover.isNull())
        setCover(cover);
}

void MusicPlayer::setCover(const QImage &image)
{
    QImage scaled = image;
    if (!scaled.isNull() && (scaled.width() > 360 || scaled.height() > 360))
        scaled = scaled.scaled(360, 360, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    {
        QMutexLocker lock(&m_coverMutex);
        m_cover = scaled;
    }
    m_hasCover = !scaled.isNull();
    ++m_coverRev;
    emit coverChanged();
}

void MusicPlayer::handleEnded()
{
    if (m_tracks.isEmpty())
        return;
    if (m_repeatMode == 2) {
        m_player->setPosition(0);
        m_player->play();
        return;
    }
    if (m_shuffle && m_tracks.size() > 1) {
        int pick = QRandomGenerator::global()->bounded(m_tracks.size() - 1);
        if (pick >= m_index)
            ++pick;
        playIndex(pick, true);
        return;
    }
    if (m_index + 1 < m_tracks.size())
        playIndex(m_index + 1, true);
    else if (m_repeatMode > 0)
        playIndex(0, true);
    else
        m_player->pause();
}

void MusicPlayer::advanceAfterError()
{
    if (m_tracks.isEmpty())
        return;
    ++m_failStreak;
    if (m_failStreak >= m_tracks.size()) {
        m_failStreak = 0;
        emit playbackError(QStringLiteral("Couldn't play this file"));
        return;
    }
    if (m_index + 1 < m_tracks.size())
        playIndex(m_index + 1, true);
    else if (m_repeatMode > 0)
        playIndex(0, true);
    else
        emit playbackError(QStringLiteral("Couldn't play this file"));
}

CoverImageProvider::CoverImageProvider(MusicPlayer *player)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_player(player)
{
}

QImage CoverImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const bool round = id.endsWith(QLatin1String("/round"));
    const bool soft = id.endsWith(QLatin1String("/soft"));
    QImage image = m_player ? m_player->coverImage() : QImage();
    if (!image.isNull() && round)
        image = roundedImageCorners(image, 0.5);
    else if (!image.isNull() && soft)
        image = roundedImageCorners(image, 0.22);
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    } else if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0
               && (image.width() > requestedSize.width() || image.height() > requestedSize.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size)
        *size = image.size();
    return image;
}
