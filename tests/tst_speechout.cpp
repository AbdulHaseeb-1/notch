// Spoken reports: when it speaks, what it says, and that nothing in a reply can run as a command.
#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "SpeechFakes.h"
#include "SpeechOut.h"

namespace {

// A 16-bit mono WAV file of a tone.
QByteArray wavFile(double seconds, int rate = 24000, int channels = 1)
{
    const int n = int(seconds * rate);
    QByteArray pcm(n * 2 * channels, Qt::Uninitialized);
    auto *p = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < channels; ++c)
            p[i * channels + c] = qint16(8000 * std::sin(2 * M_PI * 300 * i / rate));
    QByteArray h;
    QDataStream d(&h, QIODevice::WriteOnly);
    d.setByteOrder(QDataStream::LittleEndian);
    d.writeRawData("RIFF", 4); d << quint32(36 + pcm.size()); d.writeRawData("WAVEfmt ", 8);
    d << quint32(16) << quint16(1) << quint16(channels) << quint32(rate) << quint32(rate * 2 * channels)
      << quint16(2 * channels) << quint16(16);
    d.writeRawData("data", 4); d << quint32(pcm.size());
    return h + pcm;
}

QByteArray googleAnswer(const QByteArray &wav)
{
    return QJsonDocument(QJsonObject{ { "audioContent", QString::fromLatin1(wav.toBase64()) } }).toJson(QJsonDocument::Compact);
}

} // namespace

class TstSpeechOut : public QObject
{
    Q_OBJECT

    // A "voice" that writes what it is told to a file.
    static void useRecorder(SpeechOut &s, const QString &file, const QString &script = QString()) {
        s.setCommand(QStringLiteral("sh"), { QStringLiteral("-c"), script.isEmpty() ? QStringLiteral("cat > '%1'").arg(file) : script });
    }
    static QByteArray read(const QString &file) {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QSKIP("These tests use a POSIX shell as the stand-in voice.");
#endif
    }

    void offByDefaultSaysNothing() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        QVERIFY(!s.enabled());
        s.speakReport("Done.");
        s.say("Hello");
        QTest::qWait(150);
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!s.speaking());
    }

    void speaksASummaryOfTheReply() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.speakReport("**Done.** I made the file.\n```\nlong code block\n```\nSee https://example.com for more.");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        const QString said = QString::fromUtf8(read(out));
        QVERIFY2(said.contains("Done. I made the file."), qPrintable(said));
        QVERIFY2(!said.contains("long code block") && !said.contains("https") && !said.contains("*"), qPrintable(said));
    }

    void sayIsExactlyTheShortLine() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say("I need your approval to continue.");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QStringLiteral("I need your approval to continue."));
    }

    void textIsDataNotACommand() {
        // The reply goes in on stdin. Shell syntax in it is read aloud as words, never run.
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        const QString trap = dir.filePath("PWNED");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say(QStringLiteral("$(touch '%1'); `touch '%1'`; touch '%1' && echo \"x\" 'y'").arg(trap));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QVERIFY(!QFileInfo::exists(trap));
        QVERIFY(QString::fromUtf8(read(out)).contains("touch"));
    }

    void unicodeSurvives() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say(QString::fromUtf8("Fertig \xc3\xa4\xc3\xb6\xc3\xbc \xe2\x80\x94 caf\xc3\xa9"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QString::fromUtf8("Fertig \xc3\xa4\xc3\xb6\xc3\xbc \xe2\x80\x94 caf\xc3\xa9"));
    }

    void speakingIsReportedAndStopCutsIt() {
        SpeechOut s;
        s.setCommand("sh", { "-c", "sleep 5" });
        s.setEnabled(true);
        QSignalSpy spy(&s, &SpeechOut::speakingChanged);
        s.say("A long report");
        QTRY_VERIFY(s.speaking());
        QVERIFY(spy.count() >= 1);
        s.stop();
        QVERIFY(!s.speaking());
    }

    void aNewReportReplacesTheOldOne() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        // Records, then lingers, so the first is still "speaking" when the second arrives.
        s.setCommand("sh", { "-c", QStringLiteral("cat >> '%1'; sleep 5").arg(out) });
        s.setEnabled(true);
        s.say("first report");
        QTRY_VERIFY(s.speaking());
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromUtf8(read(out)), QStringLiteral("first report"), 3000);
        s.say("second report");
        QTRY_VERIFY_WITH_TIMEOUT(QString::fromUtf8(read(out)).contains("second report"), 3000);
        s.stop();
    }

    void turningItOffCutsTheVoiceAtOnce() {
        SpeechOut s;
        s.setCommand("sh", { "-c", "sleep 5" });
        s.setEnabled(true);
        s.say("Hello there");
        QTRY_VERIFY(s.speaking());
        s.setEnabled(false);
        QVERIFY(!s.speaking());
    }

    void noVoiceInstalledIsUnavailableNotAnError() {
        SpeechOut s;
        s.setCommand(QString(), {});
        s.setEnabled(true);
        QVERIFY(!s.available());
        s.speakReport("Done.");       // quietly nothing
        QVERIFY(!s.speaking());
    }

    void aMissingProgramDoesNotCrash() {
        SpeechOut s;
        s.setCommand("/nonexistent/voice", {});
        s.setEnabled(true);
        s.say("Hello");
        QTest::qWait(150);
        QVERIFY(!s.speaking());
    }

    // ---- pure helpers ----------------------------------------------------------------------

    void kokoroAddressesAreChecked() {
        QCOMPARE(SpeechOut::kokoroUrl("http://127.0.0.1:8880"), QString("http://127.0.0.1:8880/v1/audio/speech"));
        QCOMPARE(SpeechOut::kokoroUrl("localhost:8880/"), QString("http://localhost:8880/v1/audio/speech"));
        QCOMPARE(SpeechOut::kokoroUrl("http://localhost:8880/v1"), QString("http://localhost:8880/v1/audio/speech"));
        QCOMPARE(SpeechOut::kokoroUrl("https://tts.example.com"), QString("https://tts.example.com/v1/audio/speech"));
        QVERIFY(SpeechOut::kokoroUrl("http://tts.example.com").isEmpty());   // plain http to another machine
        QVERIFY(SpeechOut::kokoroUrl("").isEmpty());
        QVERIFY(!SpeechOut::kokoroAddressProblem("ftp://127.0.0.1").isEmpty());
        QVERIFY(SpeechOut::kokoroAddressProblem("https://tts.example.com").isEmpty());
    }

    void googleLanguageComesFromTheVoiceName() {
        QCOMPARE(SpeechOut::googleLanguage("en-US-Neural2-F"), QString("en-US"));
        QCOMPARE(SpeechOut::googleLanguage("en-gb-Studio-B"), QString("en-GB"));
        QCOMPARE(SpeechOut::googleLanguage("nonsense"), QString("en-US"));
    }

    void wavIsReadAndBadFilesAreRefused() {
        int rate = 0, ch = 0;
        QString err;
        const QByteArray pcm = SpeechPlayer::wavToPcm(wavFile(0.5, 24000, 2), &rate, &ch, &err);
        QCOMPARE(rate, 24000);
        QCOMPARE(ch, 2);
        QCOMPARE(pcm.size(), 12000 * 2 * 2);
        QVERIFY(SpeechPlayer::wavToPcm("not a wav file at all", &rate, &ch, &err).isEmpty());
        QVERIFY(!err.isEmpty());
        QVERIFY(SpeechPlayer::wavToPcm(QByteArray(), &rate, &ch, &err).isEmpty());
    }

    void convertResamplesToTheDeviceFormat() {
        QAudioFormat f;
        f.setSampleRate(48000);
        f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Float);
        int rate = 0, ch = 0;
        QString err;
        const QByteArray pcm = SpeechPlayer::wavToPcm(wavFile(0.5), &rate, &ch, &err);
        const QByteArray out = SpeechPlayer::convert(pcm, rate, ch, f);
        // 0.5 s at 48 kHz, 2 channels, 4 bytes a sample (a sample of slack for rounding).
        QVERIFY(qAbs(out.size() - 24000 * 2 * 4) <= 16);
        const float *p = reinterpret_cast<const float *>(out.constData());
        float peak = 0;
        for (int i = 0; i < out.size() / 4; ++i)
            peak = qMax(peak, qAbs(p[i]));
        QVERIFY(peak > 0.1f && peak < 0.5f);
    }

    // ---- Google Cloud Text-to-Speech -------------------------------------------------------------

    void googleSendsTheTextAndPlaysTheAudio() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.4));
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("KEY-123");
        s.setGoogleVoice("en-GB-Neural2-A");
        s.setSpeed(1.2);
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        s.setEnabled(true);
        QVERIFY(s.available());
        QSignalSpy speaking(&s, &SpeechOut::speakingChanged);
        s.say("Hello there");
        QVERIFY(s.speaking());
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        QVERIFY(speaking.count() >= 2);
        QVERIFY(s.error().isEmpty());
        QVERIFY(!QFileInfo::exists(out));                                  // the system voice stayed quiet
        QVERIFY2(api.headers.contains("x-goog-api-key: key-123"), api.headers.constData());
        QVERIFY(!api.requestLine.contains("KEY"));                         // the key is never in the address
        const QJsonObject body = QJsonDocument::fromJson(api.body).object();
        QCOMPARE(body["input"].toObject()["text"].toString(), QString("Hello there"));
        QCOMPARE(body["voice"].toObject()["name"].toString(), QString("en-GB-Neural2-A"));
        QCOMPARE(body["voice"].toObject()["languageCode"].toString(), QString("en-GB"));
        QCOMPARE(body["audioConfig"].toObject()["audioEncoding"].toString(), QString("LINEAR16"));
        QCOMPARE(body["audioConfig"].toObject()["speakingRate"].toDouble(), 1.2);
    }

    void googleWithoutAKeyUsesTheSystemVoice() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEngine("google");
        s.setEnabled(true);
        QVERIFY(!s.available());
        QVERIFY(!s.problem().isEmpty());
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QVERIFY(!s.error().isEmpty());
    }

    void googleRefusalIsReportedAndReadBySystemVoice() {
        FakeStt api;
        api.status = 403;
        api.reply = R"({"error":{"code":403,"message":"Cloud Text-to-Speech API has not been used in project 1"}})";
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("bad");
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        s.setEnabled(true);
        s.say("Please read me");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QString("Please read me"));
        QVERIFY2(s.error().contains("has not been used"), qPrintable(s.error()));
        QCOMPARE(s.player()->playedUtterances(), 0);
    }

    void garbageAudioFallsBack() {
        FakeStt api;
        api.reply = R"({"audioContent":"bm90IGEgd2F2"})";
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        s.setEnabled(true);
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QVERIFY(!s.error().isEmpty());
    }

    // ---- Kokoro ----------------------------------------------------------------------------------

    void kokoroPostsAnOpenAiSpeechRequest() {
        FakeStt api;
        api.reply = wavFile(0.4);
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("kokoro");
        s.setKokoroBase(api.base());
        s.setKokoroVoice("af_bella");
        s.setSpeed(0.9);
        s.setEnabled(true);
        QVERIFY(s.available());
        s.speakReport("**Done.** I made the file.");
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        QVERIFY(api.requestLine.contains("POST /v1/audio/speech"));
        const QJsonObject body = QJsonDocument::fromJson(api.body).object();
        QCOMPARE(body["voice"].toString(), QString("af_bella"));
        QCOMPARE(body["response_format"].toString(), QString("wav"));
        QCOMPARE(body["speed"].toDouble(), 0.9);
        QVERIFY2(body["input"].toString().contains("Done. I made the file."), qPrintable(body["input"].toString()));
        QVERIFY(!body["input"].toString().contains("*"));
        QVERIFY(s.error().isEmpty());
    }

    void kokoroServerDownFallsBackToTheSystemVoice() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEngine("kokoro");
        s.setKokoroBase("http://127.0.0.1:1");
        s.setEnabled(true);
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 5000);
        QVERIFY2(s.error().contains("Kokoro"), qPrintable(s.error()));
    }

    void kokoroOnAnotherMachineOverPlainHttpIsRefused() {
        FakeStt api;
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEngine("kokoro");
        s.setKokoroBase("http://tts.example.com:8880");
        s.setEnabled(true);
        QVERIFY(!s.available());
        QVERIFY(s.problem().contains("https"));
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(api.requests, 0);
    }

    // ---- stopping and replacing ------------------------------------------------------------------

    void stopDropsAReplyStillOnItsWay() {
        FakeStt api;
        api.delayMs = 300;
        api.reply = googleAnswer(wavFile(0.4));
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        s.setEnabled(true);
        s.say("Hello");
        QVERIFY(s.speaking());
        s.stop();
        QVERIFY(!s.speaking());
        QTest::qWait(700);
        QCOMPARE(s.player()->playedUtterances(), 0);
        QVERIFY(!s.speaking());
    }

    void aNewReplyReplacesTheOldOne() {
        FakeStt api;
        api.delayMs = 150;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        s.setEnabled(true);
        s.say("First");
        s.say("Second");
        QTRY_VERIFY_WITH_TIMEOUT(s.player()->playedUtterances() >= 1 && !s.speaking(), 4000);
        QTest::qWait(300);
        QCOMPARE(s.player()->playedUtterances(), 1);
    }

    void testVoiceWorksEvenWhenReadingAloudIsOff() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1/text:synthesize");
        QVERIFY(!s.enabled());
        s.say("ignored");
        QCOMPARE(api.requests, 0);
        s.test();
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
    }

    void settingsAreClamped() {
        SpeechOut s;
        s.setSpeed(9);
        QCOMPARE(s.speed(), 1.5);
        s.setSpeed(0.1);
        QCOMPARE(s.speed(), 0.7);
        s.setMaxChars(5);
        QCOMPARE(s.maxChars(), 120);
        s.setEngine("bogus");
        QCOMPARE(s.engine(), QString("system"));
    }

    void emptyOrCodeOnlyReplyIsNotSpoken() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.speakReport("```\nonly code\n```");
        s.speakReport("   ");
        QTest::qWait(150);
        QVERIFY(!QFileInfo::exists(out));
    }
};

QTEST_MAIN(TstSpeechOut)
#include "tst_speechout.moc"
