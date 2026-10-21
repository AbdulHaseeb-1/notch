// Spoken reports: when it speaks, what it says, and that nothing in a reply can run as a command.
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
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

QByteArray googleAnswer(const QByteArray &wav, const QString &mimeType = QStringLiteral("audio/wav"))
{
    const QJsonArray content{
        QJsonObject{ { "type", "audio" },
                     { "data", QString::fromLatin1(wav.toBase64()) },
                     { "mime_type", mimeType } }
    };
    return QJsonDocument(QJsonObject{
        { "steps", QJsonArray{
              QJsonObject{ { "type", "model_output" }, { "content", content } } } }
    }).toJson(QJsonDocument::Compact);
}

QByteArray googleRawPcmAnswer(const QByteArray &pcm)
{
    return googleAnswer(pcm, QStringLiteral("audio/L16;codec=pcm;rate=24000"));
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
        // Git for Windows provides the POSIX shell these tests use as a stand-in
        // voice. Without it there is nothing to run them against.
        if (QStandardPaths::findExecutable(QStringLiteral("sh")).isEmpty())
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
        QCOMPARE(SpeechOut::kokoroVoicesUrl("http://127.0.0.1:8880"), QString("http://127.0.0.1:8880/v1/audio/voices"));
        QCOMPARE(SpeechOut::kokoroVoicesUrl("localhost:8880/v1"), QString("http://localhost:8880/v1/audio/voices"));
        QCOMPARE(SpeechOut::kokoroVoicesUrl("http://127.0.0.1:8880/v1/audio/speech"),
                 QString("http://127.0.0.1:8880/v1/audio/voices"));
        QVERIFY(SpeechOut::kokoroVoicesUrl("http://tts.example.com").isEmpty());
        QVERIFY(SpeechOut::kokoroVoicesUrl("").isEmpty());
        QVERIFY(!SpeechOut::kokoroAddressProblem("ftp://127.0.0.1").isEmpty());
        QVERIFY(SpeechOut::kokoroAddressProblem("https://tts.example.com").isEmpty());
    }

    void googleVoiceUsesAGeminiNameAndMigratesTheOldDefault() {
        SpeechOut s;
        QCOMPARE(s.googleVoice(), QString("Kore"));
        s.setGoogleVoice("Puck");
        QCOMPARE(s.googleVoice(), QString("Puck"));
        // A saved Cloud TTS name would be rejected by Gemini TTS; use its default.
        s.setGoogleVoice("en-US-Neural2-F");
        QCOMPARE(s.googleVoice(), QString("Kore"));
        s.setGoogleVoice(QString());
        QCOMPARE(s.googleVoice(), QString("Kore"));
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

    // ---- Google AI Studio / Gemini TTS ----------------------------------------------------------

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
        s.setGoogleVoice("Puck");
        s.setSpeed(1.2);
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
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
        QCOMPARE(body["model"].toString(), QString("gemini-3.8-flash-lite-tts"));
        QCOMPARE(body["store"].toBool(), false);
        const QJsonObject request = body["input"].toArray().at(0).toObject();
        QCOMPARE(request["type"].toString(), QString("user_input"));
        const QJsonObject text = request["content"].toArray().at(0).toObject();
        QCOMPARE(text["type"].toString(), QString("text"));
        QCOMPARE(text["text"].toString(), QString("Hello there"));
        QCOMPARE(text["annotations"].toArray().at(0).toObject()["style"].toString(),
                 QString("speaking rapidly"));
        QCOMPARE(body["response_format"].toObject()["type"].toString(), QString("audio"));
        QCOMPARE(body["response_format"].toObject()["mime_type"].toString(), QString("audio/wav"));
        const QJsonObject speech = body["generation_config"].toObject()["speech_config"].toArray().at(0).toObject();
        QCOMPARE(speech["voice"].toString(), QString("Puck"));
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
        api.reply = R"({"error":{"code":403,"message":"API key not valid. Please pass a valid API key."}})";
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("bad");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEnabled(true);
        s.say("Please read me");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QString("Please read me"));
        QVERIFY2(s.error().contains("API key not valid"), qPrintable(s.error()));
        QCOMPARE(s.player()->playedUtterances(), 0);
    }

    void garbageAudioFallsBack() {
        FakeStt api;
        api.reply = R"({"steps":[{"type":"model_output","content":[{"type":"audio","data":"bm90IGEgd2F2","mime_type":"audio/wav"}]}]})";
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEnabled(true);
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QVERIFY(!s.error().isEmpty());
    }

    void googleRawPcmIsWrappedAsWav() {
        FakeStt api;
        const QByteArray wav = wavFile(0.4);
        api.reply = googleRawPcmAnswer(wav.mid(44));
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEnabled(true);
        s.say("Hello");
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        QVERIFY(s.error().isEmpty());
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

    void kokoroVoiceListComesFromTheServer() {
        FakeStt api;
        api.reply = R"({"voices":["af_heart","am_michael","bf_emma"]})";
        SpeechOut s;
        s.setKokoroBase(api.base());
        QSignalSpy changed(&s, &SpeechOut::kokoroVoicesChanged);
        s.refreshKokoroVoices();
        QVERIFY(s.kokoroVoicesLoading());
        QTRY_COMPARE_WITH_TIMEOUT(s.kokoroVoices().size(), 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.kokoroVoicesLoading(), 3000);
        QCOMPARE(s.kokoroVoices(), QStringList({ "af_heart", "am_michael", "bf_emma" }));
        QVERIFY(s.kokoroVoicesProblem().isEmpty());
        QVERIFY2(api.requestLine.contains("GET /v1/audio/voices"), api.requestLine.constData());
        QVERIFY(changed.count() >= 2);
    }

    // Kokoro-FastAPI answers with objects; accept those as well as plain names.
    void kokoroVoiceListAcceptsObjectEntries() {
        FakeStt api;
        api.reply = R"({"voices":[{"id":"af_bella"},{"name":"af_nicole"},"am_adam"]})";
        SpeechOut s;
        s.setKokoroBase(api.base());
        s.refreshKokoroVoices();
        QTRY_COMPARE_WITH_TIMEOUT(s.kokoroVoices().size(), 3, 3000);
        QCOMPARE(s.kokoroVoices(), QStringList({ "af_bella", "af_nicole", "am_adam" }));
        QVERIFY(s.kokoroVoicesProblem().isEmpty());
    }

    void kokoroVoiceListReportsAnUnreachableServer() {
        SpeechOut s;
        s.setKokoroBase("http://127.0.0.1:1");
        s.refreshKokoroVoices();
        QTRY_VERIFY_WITH_TIMEOUT(!s.kokoroVoicesLoading(), 5000);
        QVERIFY2(!s.kokoroVoicesProblem().isEmpty(), "the failure should be explained");
        QVERIFY(s.kokoroVoices().isEmpty());
    }

    void changingTheKokoroServerDropsItsVoiceList() {
        FakeStt api;
        api.reply = R"({"voices":["af_heart"]})";
        SpeechOut s;
        s.setKokoroBase(api.base());
        s.refreshKokoroVoices();
        QTRY_COMPARE_WITH_TIMEOUT(s.kokoroVoices().size(), 1, 3000);
        s.setKokoroBase("http://127.0.0.1:9999");
        QVERIFY(s.kokoroVoices().isEmpty());
        QVERIFY(s.kokoroVoicesProblem().isEmpty());
    }

    void engineStatusesReportWhatIsConfigured() {        SpeechOut s;
        QVariantMap all = s.engineStatuses();
        QVERIFY(all.contains(QStringLiteral("system")));
        QVERIFY(all.contains(QStringLiteral("google")));
        QVERIFY(all.contains(QStringLiteral("kokoro")));

        const QVariantMap google = all.value(QStringLiteral("google")).toMap();
        QVERIFY(!google.value(QStringLiteral("ready")).toBool());   // no key yet
        QVERIFY(!google.value(QStringLiteral("detail")).toString().isEmpty());

        const QVariantMap kokoro = all.value(QStringLiteral("kokoro")).toMap();
        QVERIFY(kokoro.value(QStringLiteral("ready")).toBool());    // valid defaults

        s.setGoogleKey("abc123");
        s.setGoogleVoice("Kore");
        all = s.engineStatuses();
        QVERIFY(all.value(QStringLiteral("google")).toMap().value(QStringLiteral("ready")).toBool());
    }

    void kokoroPipelineSpeaksSentenceBySentence() {
        FakeStt api;
        api.reply = wavFile(0.4);
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("kokoro");
        s.setKokoroBase(api.base());
        s.setEnabled(true);
        // Two sentences: the pipeline sends one synthesis request per chunk
        // and plays them back to back.
        s.speakReport("The first sentence of this report is deliberately rather long. "
                      "The second sentence of this report is also somewhat long on purpose.");
        QTRY_COMPARE_WITH_TIMEOUT(api.requests, 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        QVERIFY(s.error().isEmpty());
    }

    void kokoroServerDownFallsBackToTheSystemVoice() {        QTemporaryDir dir;
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
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
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
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
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
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
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

    // ---- sentence chunks for pipelined synthesis -------------------------------------

    void speechChunksSplitOnSentences() {
        const QStringList chunks = SpeechOut::splitSpeechChunks(
            QStringLiteral("Press and hold the power button on the side panel for five full seconds today. "
                           "Wait until the fan inside has stopped spinning fully before doing anything else at all. "
                           "Now you can safely open up the case and look at the motherboard."));
        // Each sentence is longer than the merge floor, so each is its own chunk.
        QCOMPARE(chunks.size(), 3);
        QVERIFY(chunks.at(0).endsWith(QLatin1String("today.")));
        QVERIFY(chunks.at(2).endsWith(QLatin1String("motherboard.")));
    }

    void speechChunksMergeShortFragmentsForward() {
        // A lone "Yes." is too small to synthesize alone: it joins the next sentence.
        const QStringList chunks = SpeechOut::splitSpeechChunks(QStringLiteral("Yes. I made the file you asked for yesterday."));
        QCOMPARE(chunks.size(), 1);
        QCOMPARE(chunks.first(), QStringLiteral("Yes. I made the file you asked for yesterday."));
    }

    void speechChunksBoundALongSentence() {
        const QString longSentence = (QStringLiteral("word ") .repeated(150)).trimmed() + QStringLiteral(".");
        const QStringList chunks = SpeechOut::splitSpeechChunks(longSentence);
        QVERIFY(chunks.size() >= 2);
        for (const QString &c : chunks)
            QVERIFY2(c.size() <= 320, qPrintable(QString::number(c.size())));
    }

    void speechChunksOfNothingIsNothing() {
        QVERIFY(SpeechOut::splitSpeechChunks(QString()).isEmpty());
        QVERIFY(SpeechOut::splitSpeechChunks(QStringLiteral("   ")).isEmpty());
        QCOMPARE(SpeechOut::splitSpeechChunks(QStringLiteral("Hello world.")).size(), 1);
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
