#pragma once

#include <QString>

// A song found on the internet (YouTube), ready to be played by Noch's own player:
// where to stream it from now, and what to show for it.
struct OnlineTrack
{
    QString id;                 // the site's id for it (a YouTube video id)
    QString title;
    QString artist;
    QString webUrl;             // the page, which survives; the stream address below does not
    QString streamUrl;          // a direct audio address; expires after some hours
    QString thumbnailUrl;
    int durationSec = 0;        // 0 for a live stream
    bool live = false;
    qint64 resolvedAtMs = 0;    // when streamUrl was obtained

    bool valid() const { return !streamUrl.isEmpty(); }
};
