#pragma once

#include <QImage>
#include <atomic>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QThread>
#include <QVariantList>
#include <QWaitCondition>

class SmtcWorker;

// Now playing from *any* app: Windows exposes media sessions (Spotify, YouTube
// in a browser, VLC, Podcasts, ...) through the System Media Transport Controls.
// A worker thread polls active sessions and reports title, artist, artwork,
// position, transport controls and all available sessions.
class SmtcMedia final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSession READ hasSession NOTIFY sessionChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString album READ album NOTIFY trackChanged)
    Q_PROPERTY(QString sourceApp READ sourceApp NOTIFY trackChanged)
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY trackChanged)
    Q_PROPERTY(QString sourceExe READ sourceExe NOTIFY trackChanged)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY trackChanged)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged) // 1 playing, 2 paused, 0 stopped/none
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY controlsChanged)
    Q_PROPERTY(bool canPrev READ canPrev NOTIFY controlsChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY controlsChanged)
    Q_PROPERTY(bool canShuffle READ canShuffle NOTIFY controlsChanged)
    Q_PROPERTY(bool canRepeat READ canRepeat NOTIFY controlsChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY controlsChanged)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY controlsChanged)
    Q_PROPERTY(bool hasCover READ hasCover NOTIFY coverChanged)
    Q_PROPERTY(int coverRev READ coverRev NOTIFY coverChanged)

public:
    explicit SmtcMedia(QObject *parent = nullptr);
    ~SmtcMedia() override;

    bool hasSession() const { return m_hasSession; }
    bool hasTrack() const { return !m_title.isEmpty(); }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString sourceApp() const { return m_sourceApp; }
    QString sourceKind() const { return m_sourceKind; }
    QString sourceExe() const { return m_sourceExe; }
    QString sessionId() const { return m_sessionId; }
    QVariantList sessions() const { return m_sessions; }
    int state() const { return m_state; }
    qint64 position() const { return m_position; }
    qint64 duration() const { return m_duration; }
    bool canNext() const { return m_canNext; }
    bool canPrev() const { return m_canPrev; }
    bool canSeek() const { return m_canSeek; }
    bool canShuffle() const { return m_canShuffle; }
    bool canRepeat() const { return m_canRepeat; }
    bool shuffle() const { return m_shuffle; }
    int repeatMode() const { return m_repeatMode; }
    bool hasCover() const { QMutexLocker lock(&m_coverMutex); return m_hasCover; }
    int coverRev() const { QMutexLocker lock(&m_coverMutex); return m_coverRev; }

    QImage coverImage() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void selectSession(const QString &id);

    // Mirror of MusicPlayer's surface, for the QML adapter.
    Q_INVOKABLE void toggleMute() {}

signals:
    void sessionChanged();
    void sessionsChanged();
    void trackChanged();
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void controlsChanged();
    void coverChanged();

private:
    void applyTrack(const QString &title, const QString &artist, const QString &album,
                    const QString &sourceApp, const QString &sourceKind, const QString &sourceExe,
                    const QString &sessionId);
    void applySession(bool hasSession);
    void applySessions(const QVariantList &sessions);
    void applyState(int state);
    void applyTimeline(qint64 position, qint64 duration, bool canSeek, bool canNext, bool canPrev,
                       bool canShuffle, bool canRepeat, bool shuffle, int repeatMode);
    void applyCover(const QImage &cover);

    bool m_hasSession = false;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_sourceApp;
    QString m_sourceKind;
    QString m_sourceExe;
    QString m_sessionId;
    QVariantList m_sessions;
    int m_state = 0;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    bool m_canSeek = false;
    bool m_canNext = false;
    bool m_canPrev = false;
    bool m_canShuffle = false;
    bool m_canRepeat = false;
    bool m_shuffle = false;
    int m_repeatMode = 0;

    mutable QMutex m_coverMutex;
    QImage m_cover;
    bool m_hasCover = false;
    int m_coverRev = 0;

    SmtcWorker *m_thread = nullptr;};

// The polling thread. On Windows it talks to the WinRT session manager; other
// platforms compile to a sleeping stub so development builds still work.
class SmtcWorker : public QThread
{
    Q_OBJECT

public:
    enum Command { Toggle, Next, Previous, Seek, SetShuffle, SetRepeat, SelectSession };

    ~SmtcWorker() override { stop(); }
    void post(Command command, qint64 value = 0, const QString &text = QString());
    void stop();

signals:
    void sessionArrived(bool hasSession);
    void sessionsArrived(QVariantList sessions);
    void trackArrived(QString title, QString artist, QString album, QString sourceApp,
                      QString sourceKind, QString sourceExe, QString sessionId);
    void stateArrived(int state);
    void timelineArrived(qint64 position, qint64 duration, bool canSeek, bool canNext,
                         bool canPrev, bool canShuffle, bool canRepeat, bool shuffle,
                         int repeatMode);
    void coverArrived(QImage cover);

protected:
    void run() override;

private:
    struct Entry {
        Command command = Toggle;
        qint64 value = 0;
        QString text;
    };

    QMutex m_mutex;
    QWaitCondition m_wake;
    QList<Entry> m_commands;
    // Atomic, not mutex-guarded: it is also read inside the polling loop's
    // WinRT waits, which must abort the moment shutdown is requested.
    std::atomic<bool> m_stop{false};
};

// Serves "image://smtccover/<rev>": the artwork of the current system session.
class SmtcCoverProvider final : public QQuickImageProvider
{
public:
    explicit SmtcCoverProvider(SmtcMedia *media);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    SmtcMedia *m_media;
};
