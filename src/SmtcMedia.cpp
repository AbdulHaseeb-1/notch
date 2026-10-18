#include "SmtcMedia.h"

#include "ImageAlpha.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMutexLocker>
#include <QVariantMap>

#ifdef Q_OS_WIN

#include "WinRtAbi.h"

using namespace nochwin;

namespace {

constexpr qint64 kFileTimeToUnixMs = 11644473600000LL; // 1601 -> 1970 in ms
constexpr qint64 kTicksPerMs = 10000LL;                // TimeSpan / DateTime ticks

template <typename T>
struct IReferenceAbi : IInspectableAbi {
    virtual HRESULT STDMETHODCALLTYPE get_Value(T *value) = 0;
};

struct SourceClassification {
    QString appName;
    QString appKind;
    QString exe;
};

QString collectWindowTitlesForProcess()
{
    QStringList titles;
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd))
                return TRUE;
            wchar_t buf[512] = {};
            const int len = GetWindowTextW(hwnd, buf, 511);
            if (len > 0) {
                auto *list = reinterpret_cast<QStringList *>(lParam);
                list->append(QString::fromWCharArray(buf, len));
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&titles));
    return titles.join(QLatin1Char('\n'));
}

SourceClassification classifySource(const QString &aumid, const QString &title,
                                    const QString &artist, const QString &album)
{
    const QString lower = aumid.toLower();
    SourceClassification out;
    out.appKind = QStringLiteral("system");

    if (lower.contains(QLatin1String("spotify"))) {
        out.appName = QStringLiteral("Spotify");
        out.appKind = QStringLiteral("spotify");
        out.exe = QStringLiteral("spotify.exe");
        return out;
    }
    if (lower.contains(QLatin1String("youtube"))) {
        out.appName = QStringLiteral("YouTube");
        out.appKind = QStringLiteral("youtube");
        out.exe = QStringLiteral("chrome.exe");
        return out;
    }
    if (lower.contains(QLatin1String("vlc"))) {
        out.appName = QStringLiteral("VLC");
        out.appKind = QStringLiteral("vlc");
        out.exe = QStringLiteral("vlc.exe");
        return out;
    }
    if (lower.contains(QLatin1String("applemusic")) || lower.contains(QLatin1String("itunes"))) {
        out.appName = QStringLiteral("Apple Music");
        out.appKind = QStringLiteral("applemusic");
        out.exe = lower.contains(QLatin1String("itunes")) ? QStringLiteral("iTunes.exe")
                                                          : QStringLiteral("AppleMusic.exe");
        return out;
    }

    QString browserName;
    if (lower.contains(QLatin1String("chrome"))) {
        browserName = QStringLiteral("Chrome");
        out.appKind = QStringLiteral("chrome");
        out.exe = QStringLiteral("chrome.exe");
    } else if (lower.contains(QLatin1String("msedge")) || lower.contains(QLatin1String("edge"))) {
        browserName = QStringLiteral("Edge");
        out.appKind = QStringLiteral("edge");
        out.exe = QStringLiteral("msedge.exe");
    } else if (lower.contains(QLatin1String("firefox")) || lower.contains(QLatin1String("mozilla"))
               || lower.contains(QLatin1String("308046b0af4a39cb"))) {
        browserName = QStringLiteral("Firefox");
        out.appKind = QStringLiteral("firefox");
        out.exe = QStringLiteral("firefox.exe");
    } else if (lower.contains(QLatin1String("brave"))) {
        browserName = QStringLiteral("Brave");
        out.appKind = QStringLiteral("brave");
        out.exe = QStringLiteral("brave.exe");
    } else if (lower.contains(QLatin1String("opera"))) {
        browserName = QStringLiteral("Opera");
        out.appKind = QStringLiteral("opera");
        out.exe = QStringLiteral("opera.exe");
    }

    if (!browserName.isEmpty()) {
        out.appName = browserName;
        const QString meta = (title + QLatin1Char(' ') + artist + QLatin1Char(' ') + album
                              + QLatin1Char(' ') + collectWindowTitlesForProcess())
                                 .toLower();
        if (meta.contains(QLatin1String("youtube music"))) {
            out.appName = QStringLiteral("YouTube Music · ") + browserName;
            out.appKind = QStringLiteral("youtube");
        } else if (meta.contains(QLatin1String("- youtube")) || meta.contains(QLatin1String("youtube"))) {
            out.appName = QStringLiteral("YouTube · ") + browserName;
            out.appKind = QStringLiteral("youtube");
        } else if (meta.contains(QLatin1String("spotify"))) {
            out.appName = QStringLiteral("Spotify · ") + browserName;
            out.appKind = QStringLiteral("spotify");
        } else if (meta.contains(QLatin1String("soundcloud"))) {
            out.appName = QStringLiteral("SoundCloud · ") + browserName;
            out.appKind = QStringLiteral("soundcloud");
        }
        return out;
    }

    if (aumid.isEmpty())
        return out;

    QString name = QFileInfo(aumid).fileName();
    if (name.isEmpty())
        name = aumid;
    const qsizetype bang = name.indexOf(QLatin1Char('!'));
    if (bang >= 0)
        name = name.mid(bang + 1);
    else if (name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        out.exe = name;
        name.chop(4);
    }
    if (!name.isEmpty())
        name[0] = name.at(0).toUpper();
    out.appName = name;
    return out;
}

void sendMediaVirtualKey(WORD vk)
{
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

} // namespace

#endif // Q_OS_WIN

// ------------------------------------------------------------------ worker
void SmtcWorker::post(Command command, qint64 value, const QString &text)
{
    QMutexLocker lock(&m_mutex);
    m_commands.append({command, value, text});
    m_wake.wakeOne();
}

void SmtcWorker::stop()
{
    {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
        m_wake.wakeOne();
    }
    // The run loop checks m_stop inside every WinRT wait, so this only lasts
    // long if a COM object refuses to wake up; 15s is the same bound its
    // waits use.
    wait(15000);
}

void SmtcWorker::run()
{
#ifdef Q_OS_WIN
    if (!winRtApi().valid() || FAILED(winRtApi().roInitialize(1 /*MULTITHREADED*/)))
        return;

    ComPtr<ISessionManagerAbi> manager;
    quint64 managerAttempt = 0;
    QString trackKey;
    bool hasCoverForTrack = false;
    QString preferredSessionId;
    quint64 tick = 0;
    int lastKnownState = 0;

    for (;;) {
        QList<Entry> pending;
        {
            QMutexLocker lock(&m_mutex);
            if (m_stop)
                break;
            pending = m_commands;
            m_commands.clear();
        }

        for (const Entry &entry : pending) {
            if (entry.command == SelectSession)
                preferredSessionId = entry.text;
        }

        if (!manager) {
            if (GetTickCount64() - managerAttempt > 3000) {
                managerAttempt = GetTickCount64();
                ComPtr<IUnknown> requestOp;
                ComPtr<ISessionManagerStaticsAbi> statics;
                if (SUCCEEDED(getActivationFactory(
                        L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager",
                        kIidSessionStatics, statics.putVoid()))
                    && SUCCEEDED(statics->RequestAsync(requestOp.putVoid()))) {
                    operationResult<ISessionManagerAbi *,
                                    IAsyncOperationAbi<ISessionManagerAbi *>>(
                        requestOp.get(), manager.put(), &m_stop);
                }
            }
        }

        // Gather all active sessions so Spotify, Chrome/YouTube, etc. are all visible.
        struct Candidate {
            ComPtr<ISessionAbi> session;
            QString aumid;
            int status = PlaybackClosed;
        };
        std::vector<Candidate> candidates;
        ComPtr<ISessionAbi> currentSession;
        QString currentAumid;

        if (manager) {
            manager->GetCurrentSession(reinterpret_cast<void **>(&currentSession.p));
            if (currentSession) {
                HStringHandle src = nullptr;
                currentSession->get_SourceAppUserModelId(&src);
                currentAumid = takeHString(&src);
            }

            ComPtr<IVectorViewAbi<ISessionAbi *>> sessionList;
            if (SUCCEEDED(manager->GetSessions(reinterpret_cast<void **>(&sessionList.p)))
                && sessionList) {
                uint32_t count = 0;
                sessionList->get_Size(&count);
                for (uint32_t i = 0; i < count; ++i) {
                    ComPtr<ISessionAbi> s;
                    if (FAILED(sessionList->GetAt(i, s.put())) || !s)
                        continue;
                    HStringHandle src = nullptr;
                    s->get_SourceAppUserModelId(&src);
                    const QString aumid = takeHString(&src);
                    int st = PlaybackClosed;
                    ComPtr<IPlaybackInfoAbi> info;
                    if (SUCCEEDED(s->GetPlaybackInfo(reinterpret_cast<void **>(&info.p))) && info)
                        info->get_PlaybackStatus(&st);

                    Candidate c;
                    c.session = std::move(s);
                    c.aumid = aumid;
                    c.status = st;
                    candidates.push_back(std::move(c));
                }
            }

            if (candidates.empty() && currentSession) {
                int st = PlaybackClosed;
                ComPtr<IPlaybackInfoAbi> info;
                if (SUCCEEDED(currentSession->GetPlaybackInfo(reinterpret_cast<void **>(&info.p)))
                    && info)
                    info->get_PlaybackStatus(&st);
                Candidate c;
                c.session = std::move(currentSession);
                c.aumid = currentAumid;
                c.status = st;
                candidates.push_back(std::move(c));
            }
        }

        // Choose which session drives the player:
        // 1. User's explicitly selected session if it is playing, or if no other session is playing
        // 2. Any session currently playing (preferring currentAumid if playing)
        // 3. User's explicitly selected session if it still exists
        // 4. currentAumid or first candidate
        int chosenIdx = -1;
        int playingIdx = -1;
        int preferredIdx = -1;
        int currentIdx = -1;

        for (int i = 0; i < int(candidates.size()); ++i) {
            if (candidates[i].aumid == preferredSessionId && !preferredSessionId.isEmpty())
                preferredIdx = i;
            if (candidates[i].aumid == currentAumid && !currentAumid.isEmpty() && currentIdx < 0)
                currentIdx = i;
            if (candidates[i].status == PlaybackPlaying) {
                if (playingIdx < 0 || candidates[i].aumid == currentAumid)
                    playingIdx = i;
            }
        }

        if (preferredIdx >= 0 && (candidates[preferredIdx].status == PlaybackPlaying || playingIdx < 0))
            chosenIdx = preferredIdx;
        else if (playingIdx >= 0)
            chosenIdx = playingIdx;
        else if (preferredIdx >= 0)
            chosenIdx = preferredIdx;
        else if (currentIdx >= 0)
            chosenIdx = currentIdx;
        else if (!candidates.empty())
            chosenIdx = 0;

        // Build lightweight session list for the UI switcher every 2 ticks.
        if (tick % 2 == 0) {
            QVariantList sessionItems;
            QStringList seenIds;
            for (int i = 0; i < int(candidates.size()); ++i) {
                const QString id = candidates[i].aumid;
                if (id.isEmpty() || seenIds.contains(id))
                    continue;
                seenIds.append(id);
                const SourceClassification cls = classifySource(id, {}, {}, {});
                sessionItems.append(QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), cls.appName.isEmpty() ? id : cls.appName},
                    {QStringLiteral("kind"), cls.appKind},
                    {QStringLiteral("exe"), cls.exe},
                    {QStringLiteral("playing"), candidates[i].status == PlaybackPlaying},
                    {QStringLiteral("active"), i == chosenIdx},
                });
            }
            emit sessionsArrived(sessionItems);
        }

        ISessionAbi *session = (chosenIdx >= 0) ? candidates[chosenIdx].session.get() : nullptr;
        const QString activeAumid = (chosenIdx >= 0) ? candidates[chosenIdx].aumid : QString();
        emit sessionArrived(session != nullptr);

        if (session) {
            for (const Entry &entry : pending) {
                ComPtr<IUnknown> op;
                switch (entry.command) {
                case Toggle:
                    if (FAILED(session->TryTogglePlayPauseAsync(op.putVoid())))
                        sendMediaVirtualKey(VK_MEDIA_PLAY_PAUSE);
                    break;
                case Next:
                    if (FAILED(session->TrySkipNextAsync(op.putVoid())))
                        sendMediaVirtualKey(VK_MEDIA_NEXT_TRACK);
                    break;
                case Previous:
                    if (FAILED(session->TrySkipPreviousAsync(op.putVoid())))
                        sendMediaVirtualKey(VK_MEDIA_PREV_TRACK);
                    break;
                case Seek:
                    session->TryChangePlaybackPositionAsync(entry.value * kTicksPerMs,
                                                             op.putVoid());
                    break;
                case SetShuffle:
                    session->TryChangeShuffleActiveAsync(entry.value != 0, op.putVoid());
                    break;
                case SetRepeat: {
                    // Noch: 0=off, 1=all, 2=one -> WinRT: 0=None, 2=List, 1=Track
                    const int32_t winRtMode = entry.value == 1 ? 2 : (entry.value == 2 ? 1 : 0);
                    session->TryChangeAutoRepeatModeAsync(winRtMode, op.putVoid());
                    break;
                }
                case SelectSession:
                    break;
                }
            }

            // Playback state + available controls.
            int playbackStatus = PlaybackClosed;
            bool canNext = false;
            bool canPrev = false;
            bool canSeek = false;
            bool canShuffle = false;
            bool canRepeat = false;
            bool shuffleActive = false;
            int repeatMode = 0;
            {
                ComPtr<IPlaybackInfoAbi> info;
                if (SUCCEEDED(session->GetPlaybackInfo(reinterpret_cast<void **>(&info.p)))
                    && info) {
                    info->get_PlaybackStatus(&playbackStatus);
                    ComPtr<IPlaybackControlsAbi> controls;
                    if (SUCCEEDED(
                            info->get_Controls(reinterpret_cast<void **>(&controls.p)))
                        && controls) {
                        controls->get_IsNextEnabled(&canNext);
                        controls->get_IsPreviousEnabled(&canPrev);
                        controls->get_IsPlaybackPositionEnabled(&canSeek);
                        controls->get_IsShuffleEnabled(&canShuffle);
                        controls->get_IsRepeatEnabled(&canRepeat);
                    }
                    ComPtr<IReferenceAbi<bool>> shuffleRef;
                    if (SUCCEEDED(info->get_IsShuffleActive(reinterpret_cast<void **>(&shuffleRef.p)))
                        && shuffleRef) {
                        shuffleRef->get_Value(&shuffleActive);
                    }
                    ComPtr<IReferenceAbi<int32_t>> repeatRef;
                    if (SUCCEEDED(info->get_AutoRepeatMode(reinterpret_cast<void **>(&repeatRef.p)))
                        && repeatRef) {
                        int32_t rawMode = 0;
                        if (SUCCEEDED(repeatRef->get_Value(&rawMode)))
                            repeatMode = (rawMode == 1) ? 2 : ((rawMode == 2) ? 1 : 0);
                    }
                }
            }

            const int state = playbackStatus == PlaybackPlaying
                    ? 1
                    : (playbackStatus == PlaybackPaused || playbackStatus == PlaybackOpened ? 2
                                                                                            : 0);
            lastKnownState = state;
            emit stateArrived(state);

            // Timeline: length and where the track is right now.
            qint64 position = 0;
            qint64 duration = 0;
            {
                ComPtr<ITimelinePropertiesAbi> timeline;
                if (SUCCEEDED(session->GetTimelineProperties(
                        reinterpret_cast<void **>(&timeline.p)))
                    && timeline) {
                    int64_t start = 0;
                    int64_t pos = 0;
                    int64_t end = 0;
                    int64_t updated = 0;
                    timeline->get_StartTime(&start);
                    timeline->get_Position(&pos);
                    timeline->get_EndTime(&end);
                    timeline->get_LastUpdatedTime(&updated);
                    position = qMax<int64_t>(0, pos - start) / kTicksPerMs;
                    duration = qMax<int64_t>(0, end - start) / kTicksPerMs;
                    if (playbackStatus == PlaybackPlaying && updated > 0) {
                        const qint64 updatedMs = updated / kTicksPerMs - kFileTimeToUnixMs;
                        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                        if (updatedMs > 0 && nowMs > updatedMs && (nowMs - updatedMs) < 15000)
                            position += nowMs - updatedMs;
                    }
                    if (duration > 0)
                        position = qBound<qint64>(0, position, duration);
                }
            }
            emit timelineArrived(position, duration, canSeek, canNext, canPrev,
                                 canShuffle, canRepeat, shuffleActive, repeatMode);

            // Media properties + artwork retry for late-arriving YouTube/Spotify thumbnails.
            ComPtr<IUnknown> propsOp;
            if (SUCCEEDED(session->TryGetMediaPropertiesAsync(propsOp.putVoid()))) {
                ComPtr<IMediaPropertiesAbi> props;
                if (SUCCEEDED((operationResult<IMediaPropertiesAbi *,
                                               IAsyncOperationAbi<IMediaPropertiesAbi *>>(
                        propsOp.get(), props.put(), &m_stop)))
                    && props) {
                    HStringHandle title = nullptr;
                    HStringHandle artist = nullptr;
                    HStringHandle albumArtist = nullptr;
                    HStringHandle subtitle = nullptr;
                    HStringHandle album = nullptr;
                    props->get_Title(&title);
                    props->get_Artist(&artist);
                    props->get_AlbumArtist(&albumArtist);
                    props->get_Subtitle(&subtitle);
                    props->get_AlbumTitle(&album);

                    const QString titleText = takeHString(&title);
                    QString artistText = takeHString(&artist);
                    const QString albumArtistText = takeHString(&albumArtist);
                    const QString subtitleText = takeHString(&subtitle);
                    const QString albumText = takeHString(&album);
                    if (artistText.isEmpty())
                        artistText = !albumArtistText.isEmpty() ? albumArtistText : subtitleText;

                    const SourceClassification cls =
                        classifySource(activeAumid, titleText, artistText, albumText);

                    const QString key = titleText + QLatin1Char('\x1f') + artistText
                                      + QLatin1Char('\x1f') + cls.appName
                                      + QLatin1Char('\x1f') + activeAumid;
                    const bool trackChangedNow = (key != trackKey);
                    if (trackChangedNow) {
                        trackKey = key;
                        hasCoverForTrack = false;
                        emit trackArrived(titleText, artistText, albumText,
                                          cls.appName, cls.appKind, cls.exe, activeAumid);
                    }

                    if (trackChangedNow || (!hasCoverForTrack && tick % 2 == 0)) {
                        QImage cover;
                        ComPtr<IInspectableAbi> thumbnail;
                        if (SUCCEEDED(props->get_Thumbnail(
                                reinterpret_cast<void **>(&thumbnail.p)))
                            && thumbnail) {
                            const QByteArray bytes = readStreamReference(thumbnail.get(), &m_stop);
                            if (!bytes.isEmpty())
                                cover.loadFromData(bytes);
                        }
                        if (!cover.isNull()) {
                            hasCoverForTrack = true;
                            emit coverArrived(cover);
                        } else if (trackChangedNow) {
                            emit coverArrived(QImage());
                        }
                    }
                }
            }
        } else {
            for (const Entry &entry : pending) {
                switch (entry.command) {
                case Toggle:
                    sendMediaVirtualKey(VK_MEDIA_PLAY_PAUSE);
                    break;
                case Next:
                    sendMediaVirtualKey(VK_MEDIA_NEXT_TRACK);
                    break;
                case Previous:
                    sendMediaVirtualKey(VK_MEDIA_PREV_TRACK);
                    break;
                default:
                    break;
                }
            }
            trackKey.clear();
            hasCoverForTrack = false;
            emit stateArrived(0);
            emit timelineArrived(0, 0, false, false, false, false, false, false, 0);
        }

        ++tick;

        // Sleep until the next poll, or earlier when a command arrives. Pace:
        // 450ms while something is playing (the progress line needs the freshest
        // position), slower once paused or empty — nothing there reads fast.
        const int waitMs = lastKnownState == 1 ? 450 : 1500;
        QMutexLocker lock(&m_mutex);
        if (!m_stop && m_commands.isEmpty())
            m_wake.wait(&m_mutex, waitMs);
    }

    winRtApi().roUninitialize();
#else
    // Development builds: nothing to poll.
    QMutexLocker lock(&m_mutex);
    while (!m_stop)
        m_wake.wait(&m_mutex, 1000);
#endif
}

// ------------------------------------------------------------------ media
SmtcMedia::SmtcMedia(QObject *parent)
    : QObject(parent)
{
    m_thread = new SmtcWorker();
    connect(m_thread, &SmtcWorker::sessionArrived, this, &SmtcMedia::applySession,
            Qt::QueuedConnection);
    connect(m_thread, &SmtcWorker::sessionsArrived, this, &SmtcMedia::applySessions,
            Qt::QueuedConnection);
    connect(m_thread, &SmtcWorker::trackArrived, this, &SmtcMedia::applyTrack,
            Qt::QueuedConnection);
    connect(m_thread, &SmtcWorker::stateArrived, this, &SmtcMedia::applyState,
            Qt::QueuedConnection);
    connect(m_thread, &SmtcWorker::timelineArrived, this, &SmtcMedia::applyTimeline,
            Qt::QueuedConnection);
    connect(m_thread, &SmtcWorker::coverArrived, this, &SmtcMedia::applyCover,
            Qt::QueuedConnection);
    m_thread->start();
}

SmtcMedia::~SmtcMedia()
{
    if (m_thread) {
        m_thread->stop();
        if (m_thread->isFinished()) {
            delete m_thread;
        } else {
            // Never delete a QThread that is still running: run() would keep
            // using the destroyed mutex/condition. The loop sees m_stop and
            // exits on its own; the process reclaims the object at exit.
        }
        m_thread = nullptr;
    }
}

void SmtcMedia::applySession(bool hasSession)
{
    if (m_hasSession == hasSession)
        return;
    m_hasSession = hasSession;
    if (!hasSession) {
        applyTrack({}, {}, {}, {}, {}, {}, {});
        applyCover({});
    }
    emit sessionChanged();
}

void SmtcMedia::applySessions(const QVariantList &sessions)
{
    if (m_sessions == sessions)
        return;
    m_sessions = sessions;
    emit sessionsChanged();
}

void SmtcMedia::applyTrack(const QString &title, const QString &artist, const QString &album,
                           const QString &sourceApp, const QString &sourceKind,
                           const QString &sourceExe, const QString &sessionId)
{
    if (m_title == title && m_artist == artist && m_album == album && m_sourceApp == sourceApp
        && m_sourceKind == sourceKind && m_sourceExe == sourceExe && m_sessionId == sessionId)
        return;
    m_title = title;
    m_artist = artist;
    m_album = album;
    m_sourceApp = sourceApp;
    m_sourceKind = sourceKind;
    m_sourceExe = sourceExe;
    m_sessionId = sessionId;
    emit trackChanged();
}

void SmtcMedia::applyState(int state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void SmtcMedia::applyTimeline(qint64 position, qint64 duration, bool canSeek, bool canNext,
                              bool canPrev, bool canShuffle, bool canRepeat, bool shuffle,
                              int repeatMode)
{
    if (m_canSeek != canSeek || m_canNext != canNext || m_canPrev != canPrev
        || m_canShuffle != canShuffle || m_canRepeat != canRepeat || m_shuffle != shuffle
        || m_repeatMode != repeatMode) {
        m_canSeek = canSeek;
        m_canNext = canNext;
        m_canPrev = canPrev;
        m_canShuffle = canShuffle;
        m_canRepeat = canRepeat;
        m_shuffle = shuffle;
        m_repeatMode = repeatMode;
        emit controlsChanged();
    }
    if (m_duration != duration) {
        m_duration = duration;
        emit durationChanged();
    }
    if (m_position != position) {
        m_position = position;
        emit positionChanged();
    }
}

void SmtcMedia::applyCover(const QImage &cover)
{
    {
        QMutexLocker lock(&m_coverMutex);
        m_cover = cover;
        m_hasCover = !cover.isNull();
        ++m_coverRev;
    }
    emit coverChanged();
}

QImage SmtcMedia::coverImage() const
{
    QMutexLocker lock(&m_coverMutex);
    return m_cover;
}

void SmtcMedia::toggle()
{
    if (m_thread)
        m_thread->post(SmtcWorker::Toggle);
}

void SmtcMedia::next()
{
    if (m_thread)
        m_thread->post(SmtcWorker::Next);
}

void SmtcMedia::previous()
{
    if (m_thread)
        m_thread->post(SmtcWorker::Previous);
}

void SmtcMedia::seek(qint64 ms)
{
    if (m_thread)
        m_thread->post(SmtcWorker::Seek, ms);
}

void SmtcMedia::toggleShuffle()
{
    if (m_thread)
        m_thread->post(SmtcWorker::SetShuffle, m_shuffle ? 0 : 1);
}

void SmtcMedia::cycleRepeat()
{
    if (m_thread)
        m_thread->post(SmtcWorker::SetRepeat, (m_repeatMode + 1) % 3);
}

void SmtcMedia::selectSession(const QString &id)
{
    if (m_thread)
        m_thread->post(SmtcWorker::SelectSession, 0, id);
}

// ------------------------------------------------------------------ provider
SmtcCoverProvider::SmtcCoverProvider(SmtcMedia *media)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_media(media)
{
}

QImage SmtcCoverProvider::requestImage(const QString &id, QSize *size,
                                       const QSize &requestedSize)
{
    QImage image;
    if (m_media)
        image = m_media->coverImage();
    if (!image.isNull() && id.endsWith(QLatin1String("/round"))) {
        // Squares cropped into circles by QML still draw their corners over
        // the rounded frame, so the provider delivers pre-rounded pixels.
        image = roundedImageCorners(image, 0.5);
    }
    if (size)
        *size = image.size();
    return image;
}
