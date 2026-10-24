// What Buddy's `music` tool can see and do, as a small interface so the agent
// code does not depend on the audio stack (and tests can use a fake).
//
// The real implementation (media/MusicAgentBackend) drives the music pill: Noch's
// own queue, or whichever Windows media session (Spotify, a browser ...) is
// active. Everything here runs on the GUI thread.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>

struct MusicStatus {
    bool ready = false;      // a player exists at all
    bool playing = false;
    bool hasTrack = false;
    QString title;
    QString artist;
    QString source;          // "Noch Player", "Spotify", ... ("" when nothing is active)
    bool local = true;       // Noch's own queue: queue / add / play_track only work here
    int queueCount = 0;      // tracks in Noch's own queue
    int volumePct = 0;       // 0..100
    bool muted = false;
};

class MusicBackend
{
public:
    virtual ~MusicBackend() = default;

    virtual MusicStatus status() const = 0;

    // Start or resume; nothing happens when it already plays. Returns false
    // (and says why) when there is nothing to play.
    virtual bool play(QString *why) = 0;
    virtual void pause() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void setVolume(int percent) = 0;
    virtual void setMuted(bool muted) = 0;

    // Noch's own queue ("Title — Artist"), at most `limit` entries.
    virtual QStringList queueTitles(int limit) const = 0;
    // Plays the first queued track whose title, artist or file name contains every
    // word of `query`. Returns how many tracks matched (0 = nothing played);
    // `played` gets the title that started.
    virtual int playMatching(const QString &query, QString *played) = 0;

    // Songs from YouTube, played in the pill by Noch's own player: no browser, no window.
    // onlineReady() says whether that can work at all (and why not); playOnline() finds the
    // best match for `query` (words, or a YouTube address) and starts it. `done(title, error)`
    // is called exactly once, a few seconds later: `title` is what is playing, or `error`.
    virtual bool onlineReady(QString *why) const
    {
        if (why)
            *why = QStringLiteral("Playing from YouTube is not available.");
        return false;
    }
    virtual void playOnline(const QString &query, std::function<void(const QString &title, const QString &error)> done)
    {
        Q_UNUSED(query);
        if (done)
            done(QString(), QStringLiteral("Playing from YouTube is not available."));
    }

    // The user's Music folder ("" if there is none).
    virtual QString musicFolder() const = 0;
    // Queue a file or a whole folder. A folder is scanned in the background, so
    // `done(added, error)` may arrive later; it is called exactly once.
    virtual void add(const QString &path, std::function<void(int added, const QString &error)> done) = 0;
};
