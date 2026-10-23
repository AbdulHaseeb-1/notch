#pragma once

#include <QImage>
#include <QList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "OnlineTrack.h"

class QAudioOutput;
class QMediaPlayer;
class QNetworkReply;

// Noch's own player: a queue of local audio files, with play / pause / seek
// and whatever title, artist and cover the file actually contains.
class MusicPlayer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(int index READ index NOTIFY trackChanged)
    Q_PROPERTY(int count READ count NOTIFY playlistChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(bool repeatAll READ repeatAll WRITE setRepeatAll NOTIFY repeatAllChanged)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(bool hasCover READ hasCover NOTIFY coverChanged)
    Q_PROPERTY(int coverRev READ coverRev NOTIFY coverChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY playlistChanged)

public:
    explicit MusicPlayer(QObject *parent = nullptr);
    ~MusicPlayer() override;

    bool hasTrack() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    int index() const { return m_index; }
    int count() const { return m_tracks.size(); }
    qint64 position() const;
    qint64 duration() const;
    int state() const;
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    bool repeatAll() const { return m_repeatMode > 0; }
    void setRepeatAll(bool repeat);
    int repeatMode() const { return m_repeatMode; }
    void setRepeatMode(int mode);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    bool hasCover() const { return m_hasCover; }
    int coverRev() const { return m_coverRev; }
    QVariantList tracks() const;

    QImage coverImage() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void setIndex(int index);
    Q_INVOKABLE int addUrls(const QVariantList &urls);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    // A song from the internet (see YtDlp): put next in the queue and played now, shown in
    // the pill with the title, artist and thumbnail it came with. It lives for this session
    // only: it is not saved with the queue, since its stream address expires.
    void playOnline(const OnlineTrack &track);
    // How an expired stream address is renewed (for a track replayed hours later): given the
    // page, hand back a fresh OnlineTrack or an error.
    using OnlineResolver = std::function<void(const QString &webUrl, std::function<void(const OnlineTrack &, const QString &)> done)>;
    void setOnlineResolver(OnlineResolver resolver) { m_resolver = std::move(resolver); }
    bool currentIsOnline() const;
    // The queue as it is saved (songs from the internet left out) and where the saved position falls in it.
    QStringList persistentPaths(int *savedIndex = nullptr) const;
    // Splits a notch drop into songs (queued) and everything else (returned
    // so the dock can pin it).
    Q_INVOKABLE QVariantMap ingestDrop(const QVariantList &urls);

    signals:
    void trackChanged();
    void playlistChanged();
    void positionChanged();
    void durationChanged();
    void stateChanged();
    void volumeChanged();
    void mutedChanged();
    void repeatAllChanged();
    void repeatModeChanged();
    void shuffleChanged();
    void coverChanged();
    void playbackError(const QString &message);
    void folderScanNotice(const QString &message);

private:
    struct Track {
        QString path;
        QString title;
        QString artist;
        QString album;
        // A song from the internet: `path` is its page, played from `streamUrl`.
        bool online = false;
        QString streamUrl;
        QString thumbnailUrl;
        qint64 resolvedAtMs = 0;
    };

    void load();
    void ensureBackend();
    void saveNow();
    void scheduleSave();
    int addPath(const QString &path);
    QSet<QString> knownTrackPaths() const;
    void enqueueFolderScan(const QString &root);
    void startNextFolderScan();
    void applyFolderScan(const QString &root, const QStringList &files, bool truncated);
    void playIndex(int index, bool start);
    void loadThumbnail(const QString &url, const QString &forPage);
    void renewAndPlay(int index, bool start);
    void readMetadata();
    void setCover(const QImage &image);
    void handleEnded();
    void advanceAfterError();

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QList<Track> m_tracks;
    int m_index = -1;
    qreal m_volume = 0.8;
    bool m_muted = false;
    int m_repeatMode = 1; // 0 = off, 1 = repeat all, 2 = repeat one
    bool m_shuffle = false;
    bool m_loading = false;
    int m_failStreak = 0;
    qint64 m_pendingSeek = -1;

    OnlineResolver m_resolver;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_thumbReply;

    mutable QMutex m_coverMutex;
    QImage m_cover;
    bool m_hasCover = false;
    int m_coverRev = 0;

    QTimer m_saveTimer;
    // Folder drops are scanned on a worker thread so a slow share or a big
    // tree cannot freeze the UI; results are applied on the GUI thread.
    QStringList m_scanQueue;
    bool m_scanActive = false;
    QThread *m_scanThread = nullptr;
};

// Serves "image://cover/<rev>" for the track currently loaded.
class CoverImageProvider final : public QQuickImageProvider
{
public:
    explicit CoverImageProvider(MusicPlayer *player);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    MusicPlayer *m_player;
};
