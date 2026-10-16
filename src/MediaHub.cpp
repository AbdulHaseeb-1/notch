#include "MediaHub.h"

#include "AudioDevices.h"
#include "MusicPlayer.h"
#include "RunningApps.h"
#include "SmtcMedia.h"

#include <QDateTime>
#include <QImage>
#include <QVariantMap>

namespace {

struct PalettePair {
    QColor accent;
    QColor ambient;
};

PalettePair brandPaletteForKind(const QString &kind)
{
    if (kind == QLatin1String("spotify"))
        return {QColor(0x1D, 0xB9, 0x54), QColor(0x0F, 0x6B, 0x30)};
    if (kind == QLatin1String("youtube"))
        return {QColor(0xFF, 0x26, 0x44), QColor(0x99, 0x0F, 0x24)};
    if (kind == QLatin1String("chrome"))
        return {QColor(0x42, 0x85, 0xF4), QColor(0x1E, 0x4E, 0x9E)};
    if (kind == QLatin1String("edge"))
        return {QColor(0x00, 0x91, 0xFF), QColor(0x00, 0x4E, 0x9A)};
    if (kind == QLatin1String("firefox"))
        return {QColor(0xFF, 0x71, 0x39), QColor(0x9E, 0x34, 0x14)};
    if (kind == QLatin1String("brave"))
        return {QColor(0xFB, 0x54, 0x2B), QColor(0x99, 0x28, 0x10)};
    if (kind == QLatin1String("applemusic"))
        return {QColor(0xFA, 0x2D, 0x48), QColor(0x96, 0x14, 0x28)};
    if (kind == QLatin1String("vlc"))
        return {QColor(0xFF, 0x88, 0x00), QColor(0x99, 0x4D, 0x00)};
    if (kind == QLatin1String("soundcloud"))
        return {QColor(0xFF, 0x55, 0x00), QColor(0x99, 0x33, 0x00)};
    return {QColor(0x8B, 0x5C, 0xF6), QColor(0x4C, 0x1D, 0x95)};
}

PalettePair extractCoverPalette(const QImage &cover, const QString &fallbackKind)
{
    if (cover.isNull())
        return brandPaletteForKind(fallbackKind);

    const QImage small =
        cover.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .convertToFormat(QImage::Format_ARGB32);

    struct Bucket {
        double weight = 0.0;
        double hSum = 0.0;
        double sSum = 0.0;
        double vSum = 0.0;
    };
    Bucket buckets[12];

    for (int y = 0; y < small.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = QColor::fromRgba(line[x]);
            if (c.alpha() < 128)
                continue;
            int h = 0, s = 0, v = 0;
            c.getHsv(&h, &s, &v);
            if (h < 0 || s < 36 || v < 42)
                continue;
            const int bin = (h / 30) % 12;
            const double w = (double(s) / 255.0) * (double(v) / 255.0);
            buckets[bin].weight += w;
            buckets[bin].hSum += h * w;
            buckets[bin].sSum += s * w;
            buckets[bin].vSum += v * w;
        }
    }

    int bestBin = -1;
    double bestWeight = 0.0;
    for (int i = 0; i < 12; ++i) {
        if (buckets[i].weight > bestWeight) {
            bestWeight = buckets[i].weight;
            bestBin = i;
        }
    }

    if (bestBin < 0 || bestWeight < 1.2)
        return brandPaletteForKind(fallbackKind);

    const int avgH = qBound(0, int(buckets[bestBin].hSum / bestWeight), 359);
    const int avgS = qBound(0, int(buckets[bestBin].sSum / bestWeight), 255);
    const int avgV = qBound(0, int(buckets[bestBin].vSum / bestWeight), 255);

    const QColor accent =
        QColor::fromHsv(avgH, qBound(135, int(avgS * 1.15), 240), qBound(195, int(avgV * 1.15), 255));
    const QColor ambient =
        QColor::fromHsv(avgH, qBound(145, int(avgS * 1.25), 250), qBound(110, int(avgV * 0.80), 195));
    return {accent, ambient};
}

} // namespace

MediaHub::MediaHub(MusicPlayer *player, SmtcMedia *smtc, AudioDevices *audio,
                   RunningApps *runningApps, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_smtc(smtc)
    , m_audio(audio)
    , m_runningApps(runningApps)
{
    if (m_player) {
        connect(m_player, &MusicPlayer::trackChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::playlistChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::stateChanged, this, [this] {
            if (m_player->state() == 1)
                m_forcedSource.clear();
            refresh();
        });
        connect(m_player, &MusicPlayer::positionChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::durationChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::coverChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::shuffleChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::repeatModeChanged, this, &MediaHub::refresh);
        connect(m_player, &MusicPlayer::volumeChanged, this, &MediaHub::volumeChanged);
        connect(m_player, &MusicPlayer::mutedChanged, this, &MediaHub::volumeChanged);
    }
    if (m_smtc) {
        connect(m_smtc, &SmtcMedia::trackChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::sessionsChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::stateChanged, this, [this] {
            if (m_smtc->state() == 1)
                m_forcedSource.clear();
            refresh();
        });
        connect(m_smtc, &SmtcMedia::durationChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::coverChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::controlsChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::sessionChanged, this, &MediaHub::refresh);
        connect(m_smtc, &SmtcMedia::positionChanged, this, [this] {
            m_systemStamp = QDateTime::currentMSecsSinceEpoch();
            emit changed();
        });
    }
    if (m_audio) {
        connect(m_audio, &AudioDevices::masterVolumeChanged, this, &MediaHub::volumeChanged);
    }

    m_ticker.setInterval(250);
    connect(&m_ticker, &QTimer::timeout, this, &MediaHub::refresh);
    m_ticker.start();
    updateVisuals();
}

QString MediaHub::source() const
{
    if (!m_player || !m_smtc)
        return {};

    const bool localHas = m_player->hasTrack();
    const bool systemHas = m_smtc->hasTrack() || m_smtc->hasSession();
    if (!localHas && !systemHas)
        return {};

    if (m_forcedSource == QLatin1String("local") && localHas)
        return QStringLiteral("local");
    if (m_forcedSource == QLatin1String("system") && systemHas)
        return QStringLiteral("system");

    if (m_player->state() == 1)
        return QStringLiteral("local");
    if (m_smtc->state() == 1)
        return QStringLiteral("system");

    if (m_lastActive == QLatin1String("system") && systemHas)
        return QStringLiteral("system");
    if (m_lastActive == QLatin1String("local") && localHas)
        return QStringLiteral("local");
    if (systemHas)
        return QStringLiteral("system");
    return QStringLiteral("local");
}

QString MediaHub::sourceName() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc->sourceApp().isEmpty() ? tr("System media") : m_smtc->sourceApp();
    if (which == QLatin1String("local"))
        return tr("Noch Player");
    return {};
}

QString MediaHub::sourceKind() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc->sourceKind().isEmpty() ? QStringLiteral("system") : m_smtc->sourceKind();
    if (which == QLatin1String("local"))
        return QStringLiteral("local");
    return {};
}

QString MediaHub::sourceExe() const
{
    return source() == QLatin1String("system") ? m_smtc->sourceExe() : QString();
}

QVariantList MediaHub::sources() const
{
    QVariantList list;
    const QString activeWhich = source();
    if (m_smtc) {
        const QVariantList smtcList = m_smtc->sessions();
        for (const QVariant &v : smtcList) {
            QVariantMap map = v.toMap();
            map[QStringLiteral("active")] =
                (activeWhich == QLatin1String("system") && map.value(QStringLiteral("active")).toBool());
            list.append(map);
        }
    }
    if (m_player && (m_player->count() > 0 || activeWhich == QLatin1String("local"))) {
        list.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("local")},
            {QStringLiteral("name"), tr("Noch Player")},
            {QStringLiteral("kind"), QStringLiteral("local")},
            {QStringLiteral("playing"), m_player->state() == 1},
            {QStringLiteral("active"), activeWhich == QLatin1String("local")},
        });
    }
    return list;
}

bool MediaHub::hasTrack() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->hasTrack() : false;
    if (which == QLatin1String("local"))
        return m_player ? m_player->hasTrack() : false;
    return false;
}

QString MediaHub::title() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->title() : QString();
    if (which == QLatin1String("local"))
        return m_player ? m_player->title() : QString();
    return {};
}

QString MediaHub::artist() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->artist() : QString();
    if (which == QLatin1String("local"))
        return m_player ? m_player->artist() : QString();
    return {};
}

QString MediaHub::album() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->album() : QString();
    if (which == QLatin1String("local"))
        return m_player ? m_player->album() : QString();
    return {};
}

int MediaHub::state() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->state() : 0;
    if (which == QLatin1String("local"))
        return m_player ? m_player->state() : 0;
    return 0;
}

qint64 MediaHub::position() const
{
    const QString which = source();
    if (which != QLatin1String("system"))
        return m_player ? m_player->position() : 0;
    if (!m_smtc)
        return 0;
    if (m_smtc->state() != 1 || m_systemStamp <= 0)
        return m_smtc->position();
    qint64 live = m_smtc->position() + (QDateTime::currentMSecsSinceEpoch() - m_systemStamp);
    const qint64 span = m_smtc->duration();
    if (span > 0)
        live = qBound<qint64>(0, live, span);
    return live;
}

qint64 MediaHub::duration() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->duration() : 0;
    if (which == QLatin1String("local"))
        return m_player ? m_player->duration() : 0;
    return 0;
}

bool MediaHub::hasCover() const
{
    const QString which = source();
    if (which == QLatin1String("system"))
        return m_smtc ? m_smtc->hasCover() : false;
    if (which == QLatin1String("local"))
        return m_player ? m_player->hasCover() : false;
    return false;
}

QString MediaHub::coverUrl() const
{
    if (!hasCover())
        return {};
    if (source() == QLatin1String("system"))
        return QStringLiteral("image://smtccover/")
               + QString::number(m_smtc ? m_smtc->coverRev() : 0);
    return QStringLiteral("image://cover/")
           + QString::number(m_player ? m_player->coverRev() : 0);
}

QString MediaHub::coverUrlRound() const
{
    const QString base = coverUrl();
    if (base.isEmpty())
        return {};
    return base + QStringLiteral("/round");
}

bool MediaHub::canSeek() const
{
    if (source() == QLatin1String("system"))
        return m_smtc->canSeek() && m_smtc->duration() > 0;
    return duration() > 0;
}

bool MediaHub::canNext() const
{
    return source() == QLatin1String("system") ? m_smtc->canNext() : (m_player && m_player->count() > 0);
}

bool MediaHub::canPrev() const
{
    return source() == QLatin1String("system") ? m_smtc->canPrev() : (m_player && m_player->count() > 0);
}

bool MediaHub::canShuffle() const
{
    return source() == QLatin1String("system") ? m_smtc->canShuffle() : true;
}

bool MediaHub::canRepeat() const
{
    return source() == QLatin1String("system") ? m_smtc->canRepeat() : true;
}

bool MediaHub::shuffle() const
{
    return source() == QLatin1String("system") ? m_smtc->shuffle()
                                               : (m_player ? m_player->shuffle() : false);
}

int MediaHub::repeatMode() const
{
    return source() == QLatin1String("system") ? m_smtc->repeatMode()
                                               : (m_player ? m_player->repeatMode() : 0);
}

qreal MediaHub::volume() const
{
    if (source() == QLatin1String("system") && m_audio)
        return m_audio->masterVolume();
    return m_player ? m_player->volume() : 0.8;
}

bool MediaHub::muted() const
{
    if (source() == QLatin1String("system") && m_audio)
        return m_audio->masterMuted();
    return m_player ? m_player->muted() : false;
}

void MediaHub::updateVisuals()
{
    const QString which = source();
    const QString kind = sourceKind();
    const QString key = which + QLatin1Char('|') + kind + QLatin1Char('|') + coverUrl();
    if (key == m_visualKey)
        return;
    m_visualKey = key;

    QImage img;
    if (which == QLatin1String("system") && m_smtc && m_smtc->hasCover())
        img = m_smtc->coverImage();
    else if (which == QLatin1String("local") && m_player && m_player->hasCover())
        img = m_player->coverImage();

    qreal aspect = 1.0;
    if (!img.isNull() && img.width() > 0 && img.height() > 0) {
        const qreal raw = qreal(img.width()) / qreal(img.height());
        if (raw > 1.18)
            aspect = qBound(1.0, raw, 1.78);
    } else if (kind == QLatin1String("youtube")) {
        aspect = 1.65;
    }

    const PalettePair pal = extractCoverPalette(img, kind);
    m_coverAspect = aspect;
    m_accentColor = pal.accent;
    m_ambientColor = pal.ambient;
    emit visualsChanged();
}

void MediaHub::refresh()
{
    updateVisuals();
    emit changed();
    emit volumeChanged();
}

void MediaHub::toggle()
{
    const QString which = source();
    if (which == QLatin1String("system")) {
        m_lastActive = QStringLiteral("system");
        m_smtc->toggle();
    } else if (which == QLatin1String("local")) {
        m_lastActive = QStringLiteral("local");
        m_player->toggle();
    } else if (m_smtc) {
        m_smtc->toggle();
    }
    emit changed();
}

void MediaHub::next()
{
    const QString which = source();
    if (which == QLatin1String("system")) {
        m_lastActive = QStringLiteral("system");
        m_smtc->next();
    } else if (which == QLatin1String("local")) {
        m_lastActive = QStringLiteral("local");
        m_player->next();
    }
    emit changed();
}

void MediaHub::previous()
{
    const QString which = source();
    if (which == QLatin1String("system")) {
        m_lastActive = QStringLiteral("system");
        m_smtc->previous();
    } else if (which == QLatin1String("local")) {
        m_lastActive = QStringLiteral("local");
        m_player->previous();
    }
    emit changed();
}

void MediaHub::seek(qint64 ms)
{
    if (source() == QLatin1String("system")) {
        if (m_smtc->canSeek()) {
            m_smtc->seek(ms);
            m_systemStamp = QDateTime::currentMSecsSinceEpoch();
        }
    } else if (m_player) {
        m_player->seek(ms);
    }
    emit changed();
}

void MediaHub::toggleShuffle()
{
    if (source() == QLatin1String("system")) {
        m_smtc->toggleShuffle();
    } else if (m_player) {
        m_player->setShuffle(!m_player->shuffle());
    }
    emit changed();
}

void MediaHub::cycleRepeat()
{
    if (source() == QLatin1String("system")) {
        m_smtc->cycleRepeat();
    } else if (m_player) {
        m_player->cycleRepeat();
    }
    emit changed();
}

void MediaHub::setVolume(qreal vol)
{
    if (source() == QLatin1String("system") && m_audio) {
        m_audio->setMasterVolume(vol);
    } else if (m_player) {
        m_player->setVolume(vol);
    }
    emit volumeChanged();
}

void MediaHub::toggleMute()
{
    if (source() == QLatin1String("system") && m_audio) {
        m_audio->toggleMasterMute();
    } else if (m_player) {
        m_player->toggleMute();
    }
    emit volumeChanged();
}

void MediaHub::selectSource(const QString &id)
{
    if (id == QLatin1String("local")) {
        m_lastActive = QStringLiteral("local");
        m_forcedSource = QStringLiteral("local");
    } else {
        m_lastActive = QStringLiteral("system");
        m_forcedSource = QStringLiteral("system");
        if (m_smtc)
            m_smtc->selectSession(id);
    }
    refresh();
}

bool MediaHub::activateSourceApp()
{
    if (source() != QLatin1String("system") || !m_runningApps)
        return false;
    const QString exe = sourceExe();
    if (exe.isEmpty())
        return false;
    return m_runningApps->activateApp(exe);
}
