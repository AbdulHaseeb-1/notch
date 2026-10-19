#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class AudioDevices;
class MusicPlayer;
class RunningApps;
class SmtcMedia;

// One media surface for QML: whichever of Noch's own player or a Windows media
// session (Spotify, YouTube, Chrome, ...) is active drives the pill, the
// transport bar and the player card, with dynamic artwork colours and aspect.
class MediaHub final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY changed)         // "local" | "system" | ""
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed) // friendly app name
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY changed) // "spotify" | "youtube" | "chrome" | ...
    Q_PROPERTY(QString sourceExe READ sourceExe NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed) // 1 playing, 2 paused, 0 idle
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    // Own signal: it ticks several times a second while music plays, and sharing
    // `changed` would re-evaluate every other Media binding (title, sources, ...) each time.
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY changed)
    Q_PROPERTY(bool hasCover READ hasCover NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    // The same artwork with transparent rounded corners, for round frames.
    Q_PROPERTY(QString coverUrlRound READ coverUrlRound NOTIFY changed)
    Q_PROPERTY(qreal coverAspect READ coverAspect NOTIFY visualsChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY visualsChanged)
    Q_PROPERTY(QColor ambientColor READ ambientColor NOTIFY visualsChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canPrev READ canPrev NOTIFY changed)
    Q_PROPERTY(bool canShuffle READ canShuffle NOTIFY changed)
    Q_PROPERTY(bool canRepeat READ canRepeat NOTIFY changed)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY changed)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY changed)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY volumeChanged)

public:
    MediaHub(MusicPlayer *player, SmtcMedia *smtc, AudioDevices *audio = nullptr,
             RunningApps *runningApps = nullptr, QObject *parent = nullptr);

    QString source() const;
    QString sourceName() const;
    QString sourceKind() const;
    QString sourceExe() const;
    QVariantList sources() const;
    bool hasTrack() const;
    QString title() const;
    QString artist() const;
    QString album() const;
    int state() const;
    bool playing() const { return state() == 1; }
    qint64 position() const;
    qint64 duration() const;
    bool hasCover() const;
    QString coverUrl() const;
    QString coverUrlRound() const;
    qreal coverAspect() const { return m_coverAspect; }
    QColor accentColor() const { return m_accentColor; }
    QColor ambientColor() const { return m_ambientColor; }
    bool canSeek() const;
    bool canNext() const;
    bool canPrev() const;
    bool canShuffle() const;
    bool canRepeat() const;
    bool shuffle() const;
    int repeatMode() const;
    qreal volume() const;
    bool muted() const;

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void setVolume(qreal volume);
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void selectSource(const QString &id);
    Q_INVOKABLE bool activateSourceApp();

signals:
    void changed();
    void positionChanged();
    void visualsChanged();
    void volumeChanged();

private:
    void refresh();
    void emitChanged();
    void updateVisuals();

    MusicPlayer *m_player = nullptr;
    SmtcMedia *m_smtc = nullptr;
    AudioDevices *m_audio = nullptr;
    RunningApps *m_runningApps = nullptr;
    QString m_lastActive;   // "local" | "system" | ""
    QString m_forcedSource; // "local" | "system" | "" when explicitly chosen
    qint64 m_systemStamp = 0;
    qint64 m_lastLocalPositionEmit = 0;
    qint64 m_lastSystemPositionEmit = 0;

    QString m_visualKey;
    qreal m_coverAspect = 1.0;
    QColor m_accentColor{0x8B, 0x5C, 0xF6};
    QColor m_ambientColor{0x4C, 0x1D, 0x95};
};
