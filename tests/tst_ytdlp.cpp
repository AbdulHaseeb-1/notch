// Songs from YouTube without a browser: the yt-dlp wrapper. What it will and will not ask
// for, how it reads the answer, what it does when yt-dlp is missing, slow or fails, and the
// installer (checksum, https, nothing installed on a mismatch). The test program itself
// stands in for yt-dlp (see FakeYtDlp.h), so nothing here touches the network or YouTube.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "FakeYtDlp.h"
#include "YtDlp.h"

namespace {

// What yt-dlp prints for a search ("ytsearch1:...") and for a single video.
QByteArray searchJson(const QString &id = QStringLiteral("tdVAqxNLXiw"))
{
    return QStringLiteral("{\"_type\":\"playlist\",\"entries\":[\n        {\"id\":\"skipme\",\"title\":\"no stream here\"},\n        {\"id\":\"%1\",\"title\":\"Coldplay - Yellow\",\"uploader\":\"Somber Sounds\",\"channel\":\"Somber Sounds - Topic\",\n         \"duration\":267,\"thumbnail\":\"https://i.ytimg.com/vi/%1/maxresdefault.jpg\",\n         \"webpage_url\":\"https://www.youtube.com/watch?v=%1\",\"url\":\"https://rr1.googlevideo.com/videoplayback?x=1\",\"is_live\":false}]}")
        .arg(id).toUtf8();
}

// A web server that answers by path.
class PathServer : public QObject
{
public:
    explicit PathServer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    const QByteArray req = s->readAll();
                    const QByteArray path = req.split(' ').value(1);
                    paths << QString::fromLatin1(path);
                    const bool known = routes.contains(QString::fromLatin1(path));
                    const QByteArray body = known ? routes.value(QString::fromLatin1(path)) : QByteArray("nope");
                    s->write("HTTP/1.1 " + QByteArray(known ? "200 OK" : "404 Not Found") + "\r\nContent-Length: "
                             + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }
    QHash<QString, QByteArray> routes;
    QStringList paths;

private:
    QTcpServer server;
};

} // namespace

class TstYtDlp : public QObject
{
    Q_OBJECT

    QTemporaryDir dir;
    QString argsFile, jsonFile;

    QString self() const { return QCoreApplication::applicationFilePath(); }
    void fake(const QByteArray &json = searchJson())
    {
        QFile::remove(argsFile);
        QFile f(jsonFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(json);
        qputenv("NOCH_FAKE_YTDLP", "1");
        qputenv("NOCH_FAKE_ARGS", argsFile.toUtf8());
        qputenv("NOCH_FAKE_JSON", jsonFile.toUtf8());
        qunsetenv("NOCH_FAKE_EXIT");
        qunsetenv("NOCH_FAKE_STDERR");
        qunsetenv("NOCH_FAKE_SLEEP");
    }
    QStringList recordedArgs() const
    {
        QFile f(argsFile);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QStringList out;
        for (const QString &l : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n')))
            out << QString(l).remove(QLatin1Char(0x0D));   // the fake writes in text mode
        return out;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);          // the data folder must not be the user's
        QVERIFY(dir.isValid());
        argsFile = dir.filePath("args.txt");
        jsonFile = dir.filePath("answer.json");
    }
    void cleanup()
    {
        qunsetenv("NOCH_FAKE_YTDLP");
    }

    // ---- what it will ask for -------------------------------------------------------------------------

    void queryProblems_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<bool>("ok");
        QTest::newRow("words") << "coldplay yellow" << true;
        QTest::newRow("watch address") << "https://www.youtube.com/watch?v=abc123" << true;
        QTest::newRow("short address") << "https://youtu.be/abc123" << true;
        QTest::newRow("music address") << "https://music.youtube.com/watch?v=abc123" << true;
        QTest::newRow("no scheme") << "www.youtube.com/watch?v=abc123" << true;
        QTest::newRow("empty") << "   " << false;
        QTest::newRow("another site") << "https://evil.example.com/video" << false;
        QTest::newRow("lookalike") << "https://youtube.com.evil.example.com/x" << false;
        QTest::newRow("a file") << "file:///C:/Users/me/secret.txt" << false;
        QTest::newRow("ftp") << "ftp://youtube.com/x" << false;
    }
    void queryProblems()
    {
        QFETCH(QString, query);
        QFETCH(bool, ok);
        QCOMPARE(YtDlp::queryProblem(query).isEmpty(), ok);
    }

    void aSearchBecomesOneSearchTerm()
    {
        QCOMPARE(YtDlp::target("  coldplay   yellow "), QStringLiteral("ytsearch1:coldplay yellow"));
        QVERIFY(YtDlp::target(QString(500, 'x')).size() <= 200 + 11);
        QVERIFY(YtDlp::target("http://youtu.be/abc123").startsWith("https://youtu.be/"));       // never plain http
    }

    void theTargetComesAfterTheDoubleDashAndNothingIsShellInterpreted()
    {
        const QString nasty = YtDlp::target("x -o C:/Windows/evil.exe --exec calc & del *");
        const QStringList a = YtDlp::lookupArguments(nasty);
        QCOMPARE(a.last(), nasty);
        QCOMPARE(a.at(a.size() - 2), QStringLiteral("--"));
        // every option came before the "--", and the search text is a single argument
        QCOMPARE(a.indexOf(QStringLiteral("--exec")), -1);
        QCOMPARE(a.indexOf(QStringLiteral("-o")), -1);
        QVERIFY(a.contains("--no-playlist") && a.contains("--ignore-config") && a.contains("--skip-download"));
    }

    // ---- what it reads -----------------------------------------------------------------------------------

    void readsTheFirstPlayableSearchResult()
    {
        QString err;
        const OnlineTrack t = YtDlp::parse(searchJson(), &err);
        QVERIFY2(t.valid(), qPrintable(err));
        QCOMPARE(t.id, QStringLiteral("tdVAqxNLXiw"));
        QCOMPARE(t.title, QStringLiteral("Coldplay - Yellow"));
        QCOMPARE(t.artist, QStringLiteral("Somber Sounds"));        // "- Topic" is not part of a channel's name
        QCOMPARE(t.durationSec, 267);
        QVERIFY(!t.live);
        QVERIFY(t.thumbnailUrl.startsWith("https://i.ytimg.com/"));
        QCOMPARE(t.webUrl, QStringLiteral("https://www.youtube.com/watch?v=tdVAqxNLXiw"));
        QVERIFY(t.resolvedAtMs > 0);
    }

    void youtubeMusicTracksUseTheirRealTitleAndArtist()
    {
        const OnlineTrack t = YtDlp::parse("{\"id\":\"a1\",\"title\":\"Official Video\",\"track\":\"Yellow\",\"artist\":\"Coldplay\",\n            \"url\":\"https://x.googlevideo.com/v\",\"uploader\":\"Parlophone\"}", nullptr);
        QCOMPARE(t.title, QStringLiteral("Yellow"));
        QCOMPARE(t.artist, QStringLiteral("Coldplay"));
        QCOMPARE(t.webUrl, QStringLiteral("https://www.youtube.com/watch?v=a1"));      // built from the id
    }

    void aLiveStreamIsMarkedAndTitlesAreTrimmed()
    {
        const OnlineTrack t = YtDlp::parse(QByteArray("{\"id\":\"l1\",\"title\":\"  lofi   radio  \",\"channel\":\"Lofi Girl\",\"is_live\":true,"
                                                       "\"url\":\"https://manifest.googlevideo.com/api/manifest/hls.m3u8\"}"), nullptr);
        QVERIFY(t.live);
        QCOMPARE(t.title, QStringLiteral("lofi radio"));
        QCOMPARE(t.durationSec, 0);
    }

    void badAnswersAreRefusedWithAReason()
    {
        QString err;
        QVERIFY(!YtDlp::parse("not json at all", &err).valid());
        QVERIFY(!err.isEmpty());
        QVERIFY(!YtDlp::parse("{\"_type\":\"playlist\",\"entries\":[]}", &err).valid());
        QVERIFY(err.contains("playable"));
        QVERIFY(!YtDlp::parse("{\"id\":\"x\",\"title\":\"t\"}", &err).valid());                  // no stream
        QVERIFY(!YtDlp::parse("{\"id\":\"x\",\"title\":\"t\",\"url\":\"file:///C:/secret.mp3\"}", &err).valid());   // only http(s)
        QVERIFY(!YtDlp::parse("{\"id\":\"x\",\"title\":\"t\",\"url\":\"javascript:alert(1)\"}", &err).valid());
    }

    void checksumsAreReadFromTheReleaseList()
    {
        const QByteArray sums = "aaaa  yt-dlp\n" + QByteArray(64, 'A') + "  yt-dlp.exe\n" + QByteArray(64, 'b') + " *yt-dlp_x86.exe\n";
        QCOMPARE(YtDlp::checksumFor(sums, "yt-dlp.exe"), QString(64, 'a'));
        QCOMPARE(YtDlp::checksumFor(sums, "yt-dlp_x86.exe"), QString(64, 'b'));
        QCOMPARE(YtDlp::checksumFor(sums, "yt-dlp_macos"), QString());
        QCOMPARE(YtDlp::checksumFor(QByteArray(), "yt-dlp.exe"), QString());
    }

    // ---- running it --------------------------------------------------------------------------------------

    void findsAConfiguredCopyAndItsVersion()
    {
        fake();
        YtDlp yt;
        yt.setSearchPath(false);
        QVERIFY(!yt.available() || yt.path() != self());
        yt.setConfiguredPath(self());
        QVERIFY(yt.available());
        QCOMPARE(QFileInfo(yt.path()), QFileInfo(self()));
        QTRY_COMPARE(yt.version(), QStringLiteral("2099.01.01"));
        // a path that is not there does not fall back to something else
        yt.setConfiguredPath(dir.filePath("nothing-here.exe"));
        QVERIFY(!yt.available());
    }

    void searchesAndReturnsTheTrack()
    {
        fake();
        YtDlp yt;
        yt.setConfiguredPath(self());
        QSignalSpy changed(&yt, &YtDlp::changed);
        OnlineTrack got;
        QString error = "unset";
        bool done = false;
        yt.search("coldplay yellow", [&](const OnlineTrack &t, const QString &e) { got = t; error = e; done = true; });
        QCOMPARE(yt.state(), QStringLiteral("searching"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(got.valid());
        QCOMPARE(got.title, QStringLiteral("Coldplay - Yellow"));
        QCOMPARE(yt.state(), QStringLiteral("idle"));
        const QStringList args = recordedArgs();
        QVERIFY2(args.contains("ytsearch1:coldplay yellow"), qPrintable(args.join("|")));
        QVERIFY(args.indexOf("--") == args.indexOf("ytsearch1:coldplay yellow") - 1);
        QVERIFY2(!args.contains("--extractor-args"), "yt-dlp chooses its own YouTube clients: a forced one gives addresses the player is refused");
    }

    void hostileSearchTextStaysOneArgument()
    {
        fake();
        YtDlp yt;
        yt.setConfiguredPath(self());
        bool done = false;
        yt.search("foo --exec calc -o evil", [&](const OnlineTrack &, const QString &) { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        const QStringList args = recordedArgs();
        QVERIFY2(args.contains("ytsearch1:foo --exec calc -o evil"), qPrintable(args.join("|")));
        QCOMPARE(args.indexOf("--exec"), -1);
    }

    void aLookupOfAnotherSiteIsRefusedBeforeAnythingRuns()
    {
        fake();
        YtDlp yt;
        yt.setConfiguredPath(self());
        QString error;
        bool done = false;
        yt.search("https://evil.example.com/x", [&](const OnlineTrack &t, const QString &e) { error = e; done = !t.valid(); });
        QTRY_VERIFY(done);
        QVERIFY(error.contains("YouTube"));
        QVERIFY(recordedArgs().isEmpty());                         // yt-dlp never ran
    }

    void whenYtDlpIsMissingItSaysHowToGetIt()
    {
        YtDlp yt;
        yt.setConfiguredPath(dir.filePath("nothing-here.exe"));
        QVERIFY(!yt.available());
        QString error;
        bool done = false;
        yt.search("anything", [&](const OnlineTrack &, const QString &e) { error = e; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(error.contains("not installed"), qPrintable(error));
    }

    void ytDlpFailuresAreExplained()
    {
        fake();
        qputenv("NOCH_FAKE_EXIT", "1");
        qputenv("NOCH_FAKE_STDERR", "ERROR: [youtube] abc123: Video unavailable. This video is private");
        YtDlp yt;
        yt.setConfiguredPath(self());
        QString error;
        bool done = false;
        yt.search("x", [&](const OnlineTrack &, const QString &e) { error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY2(error.startsWith("[youtube] abc123: Video unavailable"), qPrintable(error));
        QVERIFY(!error.contains("ERROR:"));
        QCOMPARE(yt.state(), QStringLiteral("idle"));
    }

    void aSlowYtDlpIsStopped()
    {
        fake();
        qputenv("NOCH_FAKE_SLEEP", "5000");
        YtDlp yt;
        yt.setConfiguredPath(self());
        yt.setTimeoutMs(500);
        QString error;
        bool done = false;
        QElapsedTimer t;
        t.start();
        yt.search("x", [&](const OnlineTrack &, const QString &e) { error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 6000);
        QVERIFY2(error.contains("too long"), qPrintable(error));
        QVERIFY(t.elapsed() < 4000);                                // it did not wait the five seconds
    }

    // ---- installing -----------------------------------------------------------------------------------------

#ifdef Q_OS_WIN
    void installDownloadsChecksThenSaves()
    {
        const QByteArray exe = "MZ-not-a-real-program-but-it-has-a-known-checksum";
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(exe, QCryptographicHash::Sha256).toHex());
        PathServer web;
        web.routes["/SHA2-256SUMS"] = (sha + "  yt-dlp.exe\n").toUtf8();
        web.routes["/yt-dlp.exe"] = exe;
        YtDlp yt;
        yt.setSearchPath(false);
        yt.setConfiguredPath(QString());
        const QString target = QDir(yt.toolsFolder()).filePath("yt-dlp.exe");
        QFile::remove(target);
        yt.setReleaseBase(web.base());
        yt.install();
        QCOMPARE(yt.state(), QStringLiteral("installing"));
        yt.install();                                                // a second press while it runs does nothing
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 15000);
        QVERIFY2(QFile::exists(target), qPrintable(yt.message()));
        QFile f(target);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), exe);
        QCOMPARE(web.paths.count("/yt-dlp.exe"), 1);
        QFile::remove(target);
    }

    void aDownloadThatDoesNotMatchItsChecksumIsThrownAway()
    {
        PathServer web;
        web.routes["/SHA2-256SUMS"] = (QString(64, '0') + "  yt-dlp.exe\n").toUtf8();
        web.routes["/yt-dlp.exe"] = "something else entirely";
        YtDlp yt;
        yt.setSearchPath(false);
        yt.setConfiguredPath(QString());
        const QString target = QDir(yt.toolsFolder()).filePath("yt-dlp.exe");
        QFile::remove(target);
        yt.setReleaseBase(web.base());
        yt.install();
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 15000);
        QVERIFY(yt.messageIsError());
        QVERIFY2(yt.message().contains("checksum"), qPrintable(yt.message()));
        QVERIFY(!QFile::exists(target));
        QVERIFY(!yt.available());
    }

    void noChecksumMeansNoInstall()
    {
        PathServer web;
        web.routes["/SHA2-256SUMS"] = "deadbeef  something-else.exe\n";
        web.routes["/yt-dlp.exe"] = "x";
        YtDlp yt;
        yt.setSearchPath(false);
        yt.setConfiguredPath(QString());
        QFile::remove(QDir(yt.toolsFolder()).filePath("yt-dlp.exe"));
        yt.setReleaseBase(web.base());
        yt.install();
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 15000);
        QVERIFY(yt.messageIsError());
        QVERIFY(!QFile::exists(QDir(yt.toolsFolder()).filePath("yt-dlp.exe")));
        QCOMPARE(web.paths.count("/yt-dlp.exe"), 0);                // the program itself was never even fetched
    }

    void serverErrorsAndPlainHttpAreRefused()
    {
        PathServer web;                                              // knows no routes: 404
        YtDlp yt;
        yt.setSearchPath(false);
        yt.setConfiguredPath(QString());
        yt.setReleaseBase(web.base());
        yt.install();
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 15000);
        QVERIFY(yt.messageIsError());
        QVERIFY2(yt.message().contains("404"), qPrintable(yt.message()));
        yt.setReleaseBase("http://example.com/yt-dlp");              // not local, not https
        yt.install();
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 5000);
        QVERIFY2(yt.message().contains("https"), qPrintable(yt.message()));
    }
#endif

    // ---- the real thing (needs the program and the internet; skipped unless NOCH_YTDLP_EXE names it) ----

    void realYouTubeSearchFindsAPlayableSong()
    {
        const QString exe = qEnvironmentVariable("NOCH_YTDLP_EXE");
        if (exe.isEmpty())
            QSKIP("Set NOCH_YTDLP_EXE to a yt-dlp executable to search YouTube for real.");
        qunsetenv("NOCH_FAKE_YTDLP");
        YtDlp yt;
        yt.setConfiguredPath(exe);
        QVERIFY(yt.available());
        QTRY_VERIFY_WITH_TIMEOUT(!yt.version().isEmpty(), 20000);
        qInfo().noquote() << "yt-dlp" << yt.version();
        OnlineTrack got;
        QString error;
        bool done = false;
        QElapsedTimer t;
        t.start();
        yt.search("Coldplay Yellow", [&](const OnlineTrack &tr, const QString &e) { got = tr; error = e; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 90000);
        QVERIFY2(got.valid(), qPrintable(error));
        qInfo().noquote() << "found" << got.title << "by" << got.artist << "in" << t.elapsed() << "ms";
        QVERIFY(got.streamUrl.startsWith("https://"));
        QVERIFY(got.durationSec > 60);
        QVERIFY(!got.title.isEmpty());
        QVERIFY(got.webUrl.contains("youtube.com/watch?v="));
    }

    void updateRunsYtDlpsOwnUpdater()
    {
        fake();
        YtDlp yt;
        yt.setConfiguredPath(self());
        yt.update();
        QCOMPARE(yt.state(), QStringLiteral("updating"));
        QTRY_COMPARE_WITH_TIMEOUT(yt.state(), QStringLiteral("idle"), 15000);
        QVERIFY2(yt.message().contains("up to date"), qPrintable(yt.message()));
        QVERIFY(!yt.messageIsError());
        QVERIFY(recordedArgs().contains("-U"));
    }
};

int main(int argc, char **argv)
{
    if (fakeYtDlpRequested())
        return runFakeYtDlp(argc, argv);
    QCoreApplication app(argc, argv);
    TstYtDlp tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_ytdlp.moc"
