// Buddy's music tool against the real player: MusicAgentBackend driving a real
// MusicPlayer through a real MediaHub (with the real Windows media-session
// watcher). The audio is a few seconds of silence, so nothing is heard.
//
// If another app (Spotify, a browser) is playing, the hub follows it and these
// calls would pause or re-volume the user's music: the tests skip themselves then.
#include <QtTest>
#include <QDateTime>
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "AudioDevices.h"
#include "FakeYtDlp.h"
#include "MediaHub.h"
#include "MusicAgentBackend.h"
#include "MusicPlayer.h"
#include "RunningApps.h"
#include "SmtcMedia.h"
#include "WindowMonitor.h"
#include "YtDlp.h"

namespace {

// 16-bit mono PCM silence, `seconds` long.
bool writeWav(const QString &path, double seconds)
{
    const int rate = 8000;
    const quint32 dataBytes = quint32(seconds * rate) * 2;
    QByteArray h;
    auto le32 = [&h](quint32 v) { for (int i = 0; i < 4; ++i) h.append(char((v >> (8 * i)) & 0xff)); };
    auto le16 = [&h](quint16 v) { h.append(char(v & 0xff)); h.append(char(v >> 8)); };
    h.append("RIFF"); le32(36 + dataBytes); h.append("WAVEfmt "); le32(16); le16(1); le16(1);
    le32(rate); le32(rate * 2); le16(2); le16(16); h.append("data"); le32(dataBytes);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(h);
    f.write(QByteArray(int(dataBytes), '\0'));
    return true;
}

} // namespace

class TstMusicAgent : public QObject
{
    Q_OBJECT

    MusicPlayer *player = nullptr;
    SmtcMedia *smtc = nullptr;
    AudioDevices *audio = nullptr;
    RunningApps *running = nullptr;
    MediaHub *hub = nullptr;
    MusicAgentBackend *backend = nullptr;
    QTemporaryDir music;

    // An empty queue is arranged by building a fresh player stack: the saved
    // queue is wiped first, or the new player would resurrect it from
    // settings. (MusicPlayer has no clear() any more — it was test-only
    // surface with no production caller and no queue UI.)
    void resetPlayer() {
        delete backend;
        delete hub;
        delete player;
        QSettings settings;
        settings.beginGroup(QStringLiteral("player"));
        settings.remove(QStringLiteral("tracks"));
        settings.remove(QStringLiteral("index"));
        settings.remove(QStringLiteral("position"));
        settings.endGroup();
        player = new MusicPlayer(this);
        hub = new MediaHub(player, smtc, audio, this);
        backend = new MusicAgentBackend(hub, player);
        hub->selectSource(QStringLiteral("local"));
    }

    // MusicPlayer::currentIsOnline() is gone (test-only surface too). Within
    // this fixture the same fact is observable: playOnline() stamps the
    // track's album as "YouTube", and the local files here carry no album tag.
    bool currentIsOnline() const {
        return player->hasTrack() && player->album() == QStringLiteral("YouTube");
    }

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);   // the player's saved queue must not be the user's
        QVERIFY(music.isValid());
        player = new MusicPlayer(this);
        smtc = new SmtcMedia(this);
        audio = new AudioDevices(this);
        running = new RunningApps(new WindowMonitor(this), this);
        hub = new MediaHub(player, smtc, audio, this);
        backend = new MusicAgentBackend(hub, player);
        // Let the Windows media sessions report in before deciding anything.
        QTest::qWait(1500);
        hub->selectSource(QStringLiteral("local"));
        if (hub->source() == QLatin1String("system") && hub->playing())
            QSKIP("Another app is playing music; these tests would pause or re-volume it.");
    }

    void startsEmptyAndSaysSo() {
        resetPlayer();
        QVERIFY(backend->status().ready);
        QCOMPARE(backend->status().queueCount, 0);
        QString why;
        QVERIFY(!backend->play(&why));
        QVERIFY2(why.contains("Nothing is queued"), qPrintable(why));
        QVERIFY(backend->queueTitles(20).isEmpty());
        QCOMPARE(backend->playMatching("anything", nullptr), 0);
        QVERIFY(!backend->musicFolder().isEmpty());   // Windows always has one, even in test mode
    }

    void addingAFileQueuesItAndStartsPlayback() {
        const QString file = music.filePath("Blue Train.wav");
        QVERIFY(writeWav(file, 4.0));
        int added = -1;
        QString error = QStringLiteral("not called");
        backend->add(file, [&](int n, const QString &e) { added = n; error = e; });
        QCOMPARE(added, 1);                            // a file is synchronous
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(backend->status().queueCount, 1);
        QCOMPARE(backend->queueTitles(5), QStringList{ QStringLiteral("Blue Train") });
        // The first song added to an empty queue starts by itself.
        QTRY_VERIFY_WITH_TIMEOUT(backend->status().hasTrack, 5000);
        QVERIFY(backend->status().local);
        QCOMPARE(backend->status().title, QStringLiteral("Blue Train"));
    }

    void pauseAndPlayFollowThePlayer() {
        QTRY_VERIFY_WITH_TIMEOUT(backend->status().playing || !backend->status().hasTrack
                                     || player->state() != 0, 5000);
        if (!backend->status().playing)
            QSKIP("No audio output on this computer: the player never reaches the playing state.");
        backend->pause();
        QTRY_VERIFY_WITH_TIMEOUT(!backend->status().playing, 3000);
        QString why;
        QVERIFY(backend->play(&why));
        QTRY_VERIFY_WITH_TIMEOUT(backend->status().playing, 3000);
        // playing twice is harmless
        QVERIFY(backend->play(&why));
        QVERIFY(backend->status().playing);
    }

    void volumeAndMuteAreClampedAndApplied() {
        QVERIFY2(backend->status().local, "the hub must be on Noch's own player for this test");
        backend->setVolume(35);
        QCOMPARE(backend->status().volumePct, 35);
        backend->setVolume(250);                       // out of range: clamped
        QCOMPARE(backend->status().volumePct, 100);
        backend->setVolume(-5);
        QCOMPARE(backend->status().volumePct, 0);
        backend->setVolume(80);
        backend->setMuted(true);
        QVERIFY(backend->status().muted);
        backend->setMuted(true);                       // not toggled back by a second "mute"
        QVERIFY(backend->status().muted);
        backend->setMuted(false);
        QVERIFY(!backend->status().muted);
    }

    void playMatchingFindsByWordsAndPlaysIt() {
        QVERIFY(writeWav(music.filePath("So What - Miles Davis.wav"), 3.0));
        int added = 0;
        backend->add(music.filePath("So What - Miles Davis.wav"), [&](int n, const QString &) { added = n; });
        QCOMPARE(added, 1);
        QCOMPARE(backend->status().queueCount, 2);

        QString played;
        QCOMPARE(backend->playMatching("miles so", &played), 1);   // every word must match, in any order
        QCOMPARE(played, QStringLiteral("So What - Miles Davis"));
        QTRY_COMPARE_WITH_TIMEOUT(backend->status().title, QStringLiteral("So What - Miles Davis"), 3000);

        QCOMPARE(backend->playMatching("blue train", &played), 1);
        QCOMPARE(played, QStringLiteral("Blue Train"));
        QCOMPARE(backend->playMatching("no such song", &played), 0);
        QCOMPARE(backend->playMatching("   ", &played), 0);
    }

    void aFolderIsScannedInTheBackgroundAndReportedOnce() {
        QDir().mkpath(music.filePath("album"));
        QVERIFY(writeWav(music.filePath("album/one.wav"), 2.0));
        QVERIFY(writeWav(music.filePath("album/two.wav"), 2.0));
        QFile notes(music.filePath("album/notes.txt"));
        QVERIFY(notes.open(QIODevice::WriteOnly));   // not music
        const int before = backend->status().queueCount;

        int calls = 0, added = -1;
        QString error;
        backend->add(music.filePath("album"), [&](int n, const QString &e) { ++calls; added = n; error = e; });
        QTRY_VERIFY_WITH_TIMEOUT(calls > 0, 10000);
        QTest::qWait(1200);                            // a second report would show up here
        QCOMPARE(calls, 1);
        QCOMPARE(added, 2);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(backend->status().queueCount, before + 2);
    }

    void badPathsAreReported() {
        int calls = 0, added = -1;
        QString error;
        backend->add(music.filePath("missing.mp3"), [&](int n, const QString &e) { ++calls; added = n; error = e; });
        QCOMPARE(calls, 1);
        QCOMPARE(added, 0);
        QVERIFY2(error.contains("does not exist"), qPrintable(error));

        const QString text = music.filePath("readme.txt");
        QFile f(text);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not audio");
        f.close();
        calls = 0;
        backend->add(text, [&](int n, const QString &e) { ++calls; added = n; error = e; });
        QCOMPARE(calls, 1);
        QCOMPARE(added, 0);
        QVERIFY(!error.isEmpty());

        // adding something already queued is not an error that breaks anything
        calls = 0;
        backend->add(music.filePath("Blue Train.wav"), [&](int n, const QString &e) { ++calls; added = n; error = e; });
        QCOMPARE(calls, 1);
        QCOMPARE(added, 0);
    }

    // ---- songs from YouTube ---------------------------------------------------------------------------

    static OnlineTrack onlineTrack(const QString &streamFile, const QString &id = QStringLiteral("abc123")) {
        OnlineTrack t;
        t.id = id;
        t.title = QStringLiteral("Yellow");
        t.artist = QStringLiteral("Coldplay");
        t.webUrl = QStringLiteral("https://www.youtube.com/watch?v=") + id;
        t.streamUrl = QUrl::fromLocalFile(streamFile).toString();     // stands in for the googlevideo address
        t.resolvedAtMs = QDateTime::currentMSecsSinceEpoch();
        return t;
    }

    void anOnlineSongPlaysNextAndShowsItsOwnTitleAndArtist() {
        resetPlayer();
        const QString local = music.filePath("queued.wav");
        const QString stream = music.filePath("stream.wav");
        QVERIFY(writeWav(local, 3.0));
        QVERIFY(writeWav(stream, 3.0));
        QCOMPARE(player->addUrls({ QUrl::fromLocalFile(local) }), 1);
        QVERIFY(!currentIsOnline());

        player->playOnline(onlineTrack(stream));
        QCOMPARE(player->count(), 2);
        QCOMPARE(player->index(), 1);                                  // next in line, so the queue carries on after it
        QVERIFY(currentIsOnline());
        QCOMPARE(player->title(), QStringLiteral("Yellow"));
        QCOMPARE(player->artist(), QStringLiteral("Coldplay"));
        QCOMPARE(player->album(), QStringLiteral("YouTube"));
        QTRY_COMPARE_WITH_TIMEOUT(player->state(), 1, 8000);           // playing
        QTest::qWait(300);                                             // the file's own (empty) tags must not replace them
        QCOMPARE(player->title(), QStringLiteral("Yellow"));
        QCOMPARE(player->artist(), QStringLiteral("Coldplay"));
        QVERIFY(backend->status().playing);
        QCOMPARE(backend->status().title, QStringLiteral("Yellow"));

        // Asking for the same song again replaces it instead of piling up copies.
        player->playOnline(onlineTrack(stream));
        QCOMPARE(player->count(), 2);
        QCOMPARE(player->index(), 1);
        // Songs from the internet are for this session: the saved queue leaves them out.
        int saved = -1;
        const QStringList paths = player->persistentPaths(&saved);
        QCOMPARE(paths.size(), 1);
        QVERIFY(!paths.first().contains("youtube"));
        QCOMPARE(saved, 0);
        player->next();
        QVERIFY(!currentIsOnline());                           // the queue went on to the local song
    }

    void aNewOnlineSongAfterASkipGoesNextInLine() {
        resetPlayer();
        const QString stream = music.filePath("stream.wav");
        QVERIFY(writeWav(stream, 2.0));
        player->playOnline(onlineTrack(stream, "first00000a"));
        player->playOnline(onlineTrack(stream, "second0000b"));
        QCOMPARE(player->count(), 2);
        QCOMPARE(player->index(), 1);
        player->previous();
        QCOMPARE(player->index(), 0);
        player->playOnline(onlineTrack(stream, "third00000c"));
        QCOMPARE(player->count(), 3);
        QCOMPARE(player->index(), 1);                                  // right after the one that was playing
        QCOMPARE(player->persistentPaths().size(), 0);
    }

    void withoutYtDlpTheAgentIsToldHowToGetIt() {
        QString why;
        QVERIFY(!backend->onlineReady(&why));                          // this backend was made without one
        QVERIFY2(why.contains("not installed"), qPrintable(why));
        bool done = false;
        QString error;
        backend->playOnline("anything", [&](const QString &, const QString &e) { done = true; error = e; });
        QVERIFY(done);
        QVERIFY(error.contains("yt-dlp"));

        YtDlp missing;
        missing.setConfiguredPath(music.filePath("nope.exe"));
        MusicAgentBackend noTool(hub, player, &missing);
        QVERIFY(!noTool.onlineReady(&why));
        QVERIFY2(why.contains("Install yt-dlp"), qPrintable(why));
    }

    void theBackendFindsAndPlaysASongFromYouTube() {
        resetPlayer();
        const QString json = music.filePath("answer.json");
        QFile f(json);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"_type\":\"playlist\",\"entries\":[{\"id\":\"abc123\",\"title\":\"Official Video\",\"track\":\"Yellow\",\"artist\":\"Coldplay\",\n            \"url\":\"https://127.0.0.1:9/stream.m4a\",\"webpage_url\":\"https://www.youtube.com/watch?v=abc123\",\"thumbnail\":\"\"}]}");
        f.close();
        qputenv("NOCH_FAKE_YTDLP", "1");
        qputenv("NOCH_FAKE_ARGS", music.filePath("args.txt").toUtf8());
        qputenv("NOCH_FAKE_JSON", json.toUtf8());
        YtDlp yt;
        yt.setConfiguredPath(QCoreApplication::applicationFilePath());
        QVERIFY(yt.available());
        MusicAgentBackend online(hub, player, &yt);
        QString why;
        QVERIFY2(online.onlineReady(&why), qPrintable(why));

        bool done = false;
        QString title, error;
        online.playOnline("coldplay yellow", [&](const QString &t, const QString &e) { done = true; title = t; error = e; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(title, QStringLiteral("Yellow — Coldplay"));
        QVERIFY(currentIsOnline());
        QCOMPARE(player->title(), QStringLiteral("Yellow"));
        QCOMPARE(hub->source(), QStringLiteral("local"));              // the pill shows Noch's player
        QCOMPARE(hub->title(), QStringLiteral("Yellow"));

        // a failed lookup is passed on, and nothing is queued
        qputenv("NOCH_FAKE_EXIT", "1");
        qputenv("NOCH_FAKE_STDERR", "ERROR: [youtube] nothing: Video unavailable");
        const int before = player->count();
        done = false;
        online.playOnline("something gone", [&](const QString &t, const QString &e) { done = true; title = t; error = e; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(title.isEmpty());
        QVERIFY2(error.contains("Video unavailable"), qPrintable(error));
        QCOMPARE(player->count(), before);
        qunsetenv("NOCH_FAKE_EXIT");
        qunsetenv("NOCH_FAKE_STDERR");
        qunsetenv("NOCH_FAKE_YTDLP");
    }

    // The whole way, for real: yt-dlp finds the song, Noch's player streams it from YouTube.
    // Needs NOCH_YTDLP_EXE (the program) and the internet; plays silence-free audio, so it
    // mutes the player first.
    void realSongFromYouTubePlaysInThePill() {
        const QString exe = qEnvironmentVariable("NOCH_YTDLP_EXE");
        if (exe.isEmpty())
            QSKIP("Set NOCH_YTDLP_EXE to a yt-dlp executable to play from YouTube for real.");
        qunsetenv("NOCH_FAKE_YTDLP");
        resetPlayer();
        player->setMuted(true);                                        // nobody should be woken by a test
        YtDlp yt;
        yt.setConfiguredPath(exe);
        MusicAgentBackend online(hub, player, &yt);
        bool done = false;
        QString title, error;
        online.playOnline("Coldplay Yellow", [&](const QString &t, const QString &e) { done = true; title = t; error = e; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 90000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        qInfo().noquote() << "playing" << title;
        QVERIFY(currentIsOnline());
        QTRY_COMPARE_WITH_TIMEOUT(player->state(), 1, 30000);          // the stream opened and plays
        QTRY_VERIFY_WITH_TIMEOUT(player->duration() > 60000, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(player->position() > 500, 30000);     // and time moves
        QTRY_VERIFY_WITH_TIMEOUT(player->hasCover(), 30000);           // the thumbnail arrived
        QVERIFY(backend->status().playing || hub->playing());
        player->setMuted(false);
    }
};

int main(int argc, char **argv)
{
    // The test program doubles as a fake yt-dlp for the tests above (see FakeYtDlp.h).
    if (fakeYtDlpRequested())
        return runFakeYtDlp(argc, argv);
    QGuiApplication app(argc, argv);
    TstMusicAgent tc;
    return QTest::qExec(&tc, argc, argv);
}
//
#include "tst_musicagent.moc"
