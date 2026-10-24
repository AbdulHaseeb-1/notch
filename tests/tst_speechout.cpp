// Spoken reports: when it speaks, what it says, and that nothing in a reply can run as a command.
// Both voices are cloud services; the tests run them against a fake server and a silent player.
#include <QtTest>
#include <QFileInfo>
#include <QJsonArray>
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

// The text the fake Google server was last asked to speak.
QString spokenText(const FakeStt &api)
{
    const QJsonObject body = QJsonDocument::fromJson(api.body).object();
    return body["input"].toArray().at(0).toObject()["content"].toArray().at(0).toObject()["text"].toString();
}

// SpeechOut set up to speak through the fake Google server, with no real sound.
void useGoogle(SpeechOut &s, FakeStt &api)
{
    s.player()->setDeviceDisabled(true);
    s.setEngine("google");
    s.setGoogleKey("k");
    s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
    s.setEnabled(true);
}

} // namespace

class TstSpeechOut : public QObject
{
    Q_OBJECT

private slots:
    void offByDefaultSaysNothing() {
        SpeechOut s;
        QVERIFY(!s.enabled());
        s.speakReport("Done.");
        s.say("Hello");
        QTest::qWait(150);
        QVERIFY(!s.speaking());
    }

    void speaksASummaryOfTheReply() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        useGoogle(s, api);
        s.speakReport("**Done.** I made the file.\n```\nlong code block\n```\nSee https://example.com for more.");
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        const QString said = spokenText(api);
        QVERIFY2(said.contains("Done. I made the file."), qPrintable(said));
        QVERIFY2(!said.contains("long code block") && !said.contains("https") && !said.contains("*"), qPrintable(said));
    }

    void sayIsExactlyTheShortLine() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        useGoogle(s, api);
        s.say("I need your approval to continue.");
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QCOMPARE(spokenText(api), QStringLiteral("I need your approval to continue."));
    }

    void textIsDataNotACommand() {
        // The reply travels as JSON data in a request body. Shell syntax in it
        // is spoken as words, never run: nothing here can execute it.
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.3));
        QTemporaryDir dir;
        const QString trap = dir.filePath("PWNED");
        SpeechOut s;
        useGoogle(s, api);
        s.say(QStringLiteral("$(touch '%1'); `touch '%1'`; touch '%1' && echo \"x\" 'y'").arg(trap));
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QVERIFY(!QFileInfo::exists(trap));
        QVERIFY(spokenText(api).contains("touch"));
    }

    void unicodeSurvives() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        useGoogle(s, api);
        const QString line = QString::fromUtf8("Fertig \xc3\xa4\xc3\xb6\xc3\xbc \xe2\x80\x94 caf\xc3\xa9");
        s.say(line);
        QTRY_COMPARE_WITH_TIMEOUT(s.player()->playedUtterances(), 1, 3000);
        QCOMPARE(spokenText(api), line);
    }

    void speakingIsReportedAndStopCutsIt() {
        FakeStt api;
        api.delayMs = 2000;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        useGoogle(s, api);
        QSignalSpy spy(&s, &SpeechOut::speakingChanged);
        s.say("A long report");
        QTRY_VERIFY(s.speaking());
        QVERIFY(spy.count() >= 1);
        s.stop();
        QVERIFY(!s.speaking());
    }

    void turningItOffCutsTheVoiceAtOnce() {
        FakeStt api;
        api.delayMs = 2000;
        api.reply = googleAnswer(wavFile(0.3));
        SpeechOut s;
        useGoogle(s, api);
        s.say("Hello there");
        QTRY_VERIFY(s.speaking());
        s.setEnabled(false);
        QVERIFY(!s.speaking());
    }

    void noKeyIsUnavailableAndReported() {
        SpeechOut s;   // google by default, and no key
        s.setEnabled(true);
        QVERIFY(!s.available());
        QVERIFY(!s.problem().isEmpty());
        s.speakReport("Done.");
        QVERIFY(!s.speaking());
        QCOMPARE(s.error(), s.problem());   // the reason is surfaced, not silently swallowed
    }

    // ---- pure helpers ----------------------------------------------------------------------

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
        SpeechOut s;
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

    void googleWithoutAKeySaysNothingAndSaysWhy() {
        FakeStt api;
        SpeechOut s;
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEngine("google");
        s.setEnabled(true);
        QVERIFY(!s.available());
        QVERIFY(!s.problem().isEmpty());
        s.say("Hello");
        QVERIFY(!s.error().isEmpty());
        QCOMPARE(api.requests, 0);          // nothing was even asked, let alone spoken
        QVERIFY(!s.speaking());
    }

    void googleRefusalIsReportedAndNothingIsPlayed() {
        FakeStt api;
        api.status = 403;
        api.reply = R"({"error":{"code":403,"message":"API key not valid. Please pass a valid API key."}})";
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("bad");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEnabled(true);
        s.say("Please read me");
        QTRY_VERIFY_WITH_TIMEOUT(!s.error().isEmpty() && !s.speaking(), 3000);
        QVERIFY2(s.error().contains("API key not valid"), qPrintable(s.error()));
        QCOMPARE(s.player()->playedUtterances(), 0);
    }

    void garbageAudioIsReportedAndNothingIsPlayed() {
        FakeStt api;
        api.reply = R"({"steps":[{"type":"model_output","content":[{"type":"audio","data":"bm90IGEgd2F2","mime_type":"audio/wav"}]}]})";
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
        s.setEnabled(true);
        s.say("Hello");
        QTRY_VERIFY_WITH_TIMEOUT(!s.error().isEmpty() && !s.speaking(), 3000);
        QCOMPARE(s.player()->playedUtterances(), 0);
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

    void googlePipelineSpeaksSentenceBySentence() {
        FakeStt api;
        api.reply = googleAnswer(wavFile(0.4));
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setEngine("google");
        s.setGoogleKey("k");
        s.setGoogleEndpoint(api.base() + "/v1beta/interactions");
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

    void engineStatusesReportWhatIsConfigured() {
        SpeechOut s;
        QVariantMap all = s.engineStatuses();
        QVERIFY(all.contains(QStringLiteral("google")));
        QVERIFY(all.contains(QStringLiteral("deepgram")));
        QVERIFY(!all.contains(QStringLiteral("system")));
        QVERIFY(!all.contains(QStringLiteral("kokoro")));

        const QVariantMap google = all.value(QStringLiteral("google")).toMap();
        QVERIFY(!google.value(QStringLiteral("ready")).toBool());   // no key yet
        QVERIFY(!google.value(QStringLiteral("detail")).toString().isEmpty());

        const QVariantMap deepgram = all.value(QStringLiteral("deepgram")).toMap();
        QVERIFY(!deepgram.value(QStringLiteral("ready")).toBool());   // no key yet

        s.setGoogleKey("abc123");
        s.setGoogleVoice("Kore");
        all = s.engineStatuses();
        QVERIFY(all.value(QStringLiteral("google")).toMap().value(QStringLiteral("ready")).toBool());
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
        QCOMPARE(s.engine(), QString("google"));   // retired/unknown values land on google
        s.setEngine("system");
        QCOMPARE(s.engine(), QString("google"));
        s.setEngine("kokoro");
        QCOMPARE(s.engine(), QString("google"));
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
        FakeStt api;
        SpeechOut s;
        useGoogle(s, api);
        s.speakReport("```\nonly code\n```");
        s.speakReport("   ");
        QTest::qWait(150);
        QCOMPARE(api.requests, 0);
        QVERIFY(!s.speaking());
    }
};

QTEST_MAIN(TstSpeechOut)
#include "tst_speechout.moc"
