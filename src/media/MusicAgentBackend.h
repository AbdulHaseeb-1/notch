#pragma once

#include "MusicBackend.h"

class MediaHub;
class MusicPlayer;
class YtDlp;

// The agent's view of the music pill: transport and volume go through MediaHub
// (so they act on whatever the pill is showing), the queue is Noch's own player.
class MusicAgentBackend final : public MusicBackend
{
public:
    // `youtube` (optional) lets the agent play songs from YouTube.
    MusicAgentBackend(MediaHub *hub, MusicPlayer *player, YtDlp *youtube = nullptr);

    MusicStatus status() const override;
    bool play(QString *why) override;
    void pause() override;
    void next() override;
    void previous() override;
    void setVolume(int percent) override;
    void setMuted(bool muted) override;
    QStringList queueTitles(int limit) const override;
    int playMatching(const QString &query, QString *played) override;
    bool onlineReady(QString *why) const override;
    void playOnline(const QString &query, std::function<void(const QString &, const QString &)> done) override;
    QString musicFolder() const override;
    void add(const QString &path, std::function<void(int, const QString &)> done) override;

private:
    MediaHub *m_hub;
    MusicPlayer *m_player;
    YtDlp *m_youtube;
};
