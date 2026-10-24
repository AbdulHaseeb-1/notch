// Deepgram as plain speech-to-text and text-to-speech, with Buddy's own model doing the
// thinking: the request Noch sends, what it does with the answer, and every failure.
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include "SpeechFakes.h"
#include "SpeechOut.h"
#include "VoiceInput.h"

namespace {

const QString kKey = QStringLiteral("test_key_0123456789abcdef0123456789abcd");

// What /v1/listen answers.
QByteArray listenAnswer(const QString &words)
{
    return QJsonDocument(QJsonObject{ { "results", QJsonObject{ { "channels", QJsonArray{ QJsonObject{ { "alternatives", QJsonArray{
        QJsonObject{ { "transcript", words } } } } } } } } } }).toJson(QJsonDocument::Compact);
}

// A WAV file of a tone, with the streaming header Deepgram really sends (data size 0x7fff0000).
QByteArray deepgramWav(double seconds, int rate = 24000, bool streamingHeader = true)
{
    const int n = int(seconds * rate);
    QByteArray pcm(n * 2, Qt::Uninitialized);
    auto *p = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i)
        p[i] = qint16(8000 * std::sin(2 * M_PI * 300 * i / rate));
    QByteArray h;
    QDataStream d(&h, QIODevice::WriteOnly);
    d.setByteOrder(QDataStream::LittleEndian);
    d.writeRawData("RIFF", 4);
    d << quint32(streamingHeader ? 0x7fff0024u : quint32(36 + pcm.size()));
    d.writeRawData("WAVEfmt ", 8);
    d << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    d.writeRawData("data", 4);
    d << quint32(streamingHeader ? 0x7fff0000u : quint32(pcm.size()));
    return h + pcm;
}

// Raw 24 kHz mono samples, as /v1/speak returns them with container=none.
QByteArray deepgramPcm(double seconds, int rate = 24000)
{
    const int n = int(seconds * rate);
    QByteArray pcm(n * 2, Qt::Uninitialized);
    auto *p = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < n; ++i)
        p[i] = qint16(8000 * std::sin(2 * M_PI * 300 * i / rate));
    return pcm;
}

} // namespace

class tst_DeepgramRest : public QObject
{
    Q_OBJECT

private slots:
    // ---- speech to text ----------------------------------------------------------------------------

    void transcribesAClipAndSendsWhatDeepgramNeeds()
    {
        FakeStt api;
        api.reply = listenAnswer(QStringLiteral("hello  buddy open notepad"));
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        v.deepgramSettings()->setListenModel(QStringLiteral("nova-3"));
        v.deepgramSettings()->setListenLanguage(QStringLiteral("en"));
        v.deepgramSettings()->setKeyterms(QStringLiteral("Noch, Buddy"));
        QVERIFY(v.configured());
        QVERIFY(v.ready());                       // nothing to load for a network engine
        QVERIFY(v.engine().contains(QStringLiteral("Deepgram")));

        const QByteArray clip = tone(1.0);
        QObject ctx;
        VoiceInput::Transcription got;
        bool done = false;
        v.transcribe(clip, &ctx, [&](const VoiceInput::Transcription &t) { got = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(got.ok, qPrintable(got.error));
        QCOMPARE(got.text, QStringLiteral("Hello buddy open notepad."));   // repaired like any transcript
        QVERIFY(api.requestLine.startsWith("POST /v1/listen?"));
        const QUrlQuery q{ QUrl(QString::fromLatin1(api.requestLine.split(' ').value(1))).query() };
        QCOMPARE(q.queryItemValue("model"), QStringLiteral("nova-3"));
        QCOMPARE(q.queryItemValue("language"), QStringLiteral("en"));
        QCOMPARE(q.queryItemValue("smart_format"), QStringLiteral("true"));
        QCOMPARE(q.allQueryItemValues("keyterm"), (QStringList{ "Noch", "Buddy" }));
        QVERIFY2(api.headers.contains("authorization: token " + kKey.toLatin1()), api.headers.constData());
        QVERIFY(api.headers.contains("content-type: audio/wav"));
        // The body is a WAV file around the recorded audio.
        QVERIFY(api.body.startsWith("RIFF"));
        QVERIFY(api.body.size() > clip.size());
        QVERIFY(!api.requestLine.contains(kKey.toLatin1()));                  // the key never goes in the address
    }

    void pushToTalkWorksEndToEndWithDeepgram()
    {
        FakeStt api;
        api.reply = listenAnswer(QStringLiteral("what time is it"));
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        v.setEnabled(true);
        auto cap = std::make_unique<FakeCapture>();
        FakeCapture *mic = cap.get();
        v.setCapture(std::move(cap));
        QSignalSpy words(&v, &VoiceInput::transcript), failed(&v, &VoiceInput::failed);
        QVERIFY(v.beginListening());
        mic->feed(tone(1.0));
        v.endListening();
        QCOMPARE(v.state(), QStringLiteral("transcribing"));
        QTRY_COMPARE_WITH_TIMEOUT(words.count(), 1, 5000);
        QCOMPARE(words.first().first().toString(), QStringLiteral("What time is it."));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(v.state(), QStringLiteral("idle"));
    }

    void noWordsIsNotAnError()
    {
        FakeStt api;
        api.reply = listenAnswer(QString());
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        QObject ctx;
        VoiceInput::Transcription got;
        bool done = false;
        v.transcribe(tone(0.5), &ctx, [&](const VoiceInput::Transcription &t) { got = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!got.ok);
        QVERIFY(got.empty);                        // "heard nothing", which the wake word relies on
    }

    void failuresAreExplained_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected");
        QTest::newRow("bad key") << 401 << QByteArray("{\"err_code\":\"INVALID_AUTH\",\"err_msg\":\"Invalid credentials.\"}") << QStringLiteral("rejected the API key");
        QTest::newRow("forbidden") << 403 << QByteArray("{}") << QStringLiteral("rejected the API key");
        QTest::newRow("no credit") << 402 << QByteArray("{}") << QStringLiteral("out of credit");
        QTest::newRow("rate limited") << 429 << QByteArray("{}") << QStringLiteral("limiting");
        QTest::newRow("their message") << 400 << QByteArray("{\"err_msg\":\"Bad audio.\"}") << QStringLiteral("Bad audio.");
        QTest::newRow("server error") << 500 << QByteArray("oops") << QStringLiteral("HTTP 500");
    }
    void failuresAreExplained()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, expected);
        FakeStt api;
        api.status = status;
        api.reply = body;
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        QObject ctx;
        VoiceInput::Transcription got;
        bool done = false;
        v.transcribe(tone(0.5), &ctx, [&](const VoiceInput::Transcription &t) { got = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!got.ok);
        QVERIFY(!got.empty);
        QVERIFY2(got.error.contains(expected), qPrintable(got.error));
    }

    void unreachableServiceIsReported()
    {
        VoiceInput v;
        v.setDeepgramApiBase(QStringLiteral("http://127.0.0.1:1"));
        v.deepgramSettings()->setApiKey(kKey);
        QObject ctx;
        VoiceInput::Transcription got;
        bool done = false;
        v.transcribe(tone(0.5), &ctx, [&](const VoiceInput::Transcription &t) { got = t; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
        QVERIFY(!got.ok);
        QVERIFY2(got.error.contains(QStringLiteral("Deepgram")), qPrintable(got.error));
    }

    void missingKeyMeansNotConfigured()
    {
        VoiceInput v;
        QVERIFY(!v.configured());
        QVERIFY(v.problem().contains(QStringLiteral("API key")));
        v.setEnabled(true);
        QSignalSpy failed(&v, &VoiceInput::failed);
        QVERIFY(!v.beginListening());              // never opens the microphone for an engine that cannot work
        QCOMPARE(failed.count(), 1);
        v.deepgramSettings()->setApiKey(QStringLiteral("not a key"));
        QVERIFY(!v.configured());
        v.deepgramSettings()->setApiKey(kKey);
        QVERIFY(v.configured());
    }

    void fluxModelsFallBackToNovaForClips()
    {
        FakeStt api;
        api.reply = listenAnswer(QStringLiteral("ok"));
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        v.deepgramSettings()->setListenModel(QStringLiteral("flux-general-en"));
        QObject ctx;
        bool done = false;
        v.transcribe(tone(0.5), &ctx, [&](const VoiceInput::Transcription &) { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(api.requestLine.contains("model=nova-3"));          // Flux only streams
    }

    void changingTheSettingsAbandonsTheRequestInFlight()
    {
        FakeStt api;
        api.reply = listenAnswer(QStringLiteral("late words"));
        api.delayMs = 300;
        VoiceInput v;
        v.setDeepgramApiBase(api.base());
        v.deepgramSettings()->setApiKey(kKey);
        QObject ctx;
        bool done = false;
        v.transcribe(tone(0.5), &ctx, [&](const VoiceInput::Transcription &) { done = true; });
        v.deepgramSettings()->setListenModel(QStringLiteral("nova-2"));   // a settings change rebuilds the engine
        QTest::qWait(700);
        QVERIFY(!done);                              // an answer to a request nobody is waiting for is dropped
    }

    // ---- text to speech --------------------------------------------------------------------------------

    void speaksThroughDeepgram()
    {
        FakeStt api;
        api.reply = deepgramPcm(0.4);
        SpeechOut s;
        s.streamPlayer()->setDeviceDisabled(true);
        s.setDeepgramApiBase(api.base());
        s.setEngine(QStringLiteral("deepgram"));
        s.deepgramSettings()->setApiKey(kKey);
        s.deepgramSettings()->setSpeakVoice(QStringLiteral("aura-2-zeus-en"));
        s.setSpeed(1.25);
        s.setEnabled(true);
        QVERIFY(s.available());
        QSignalSpy speaking(&s, &SpeechOut::speakingChanged);
        s.say(QStringLiteral("Hello there"));
        QVERIFY(s.speaking());
        QTRY_COMPARE_WITH_TIMEOUT(s.streamPlayer()->totalFrames(), qint64(9600), 5000);   // 0.4 s at 24 kHz
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
        QVERIFY(speaking.count() >= 2);
        QVERIFY2(s.error().isEmpty(), qPrintable(s.error()));
        QVERIFY(api.requestLine.startsWith("POST /v1/speak?"));
        const QUrlQuery q{ QUrl(QString::fromLatin1(api.requestLine.split(' ').value(1))).query() };
        QCOMPARE(q.queryItemValue("model"), QStringLiteral("aura-2-zeus-en"));
        QCOMPARE(q.queryItemValue("encoding"), QStringLiteral("linear16"));
        QCOMPARE(q.queryItemValue("container"), QStringLiteral("none"));      // raw samples, playable as they arrive
        QCOMPARE(q.queryItemValue("sample_rate"), QStringLiteral("24000"));
        QCOMPARE(q.queryItemValue("speed"), QStringLiteral("1.25"));
        QVERIFY2(api.headers.contains("authorization: token " + kKey.toLatin1()), api.headers.constData());
        QVERIFY(!api.requestLine.contains(kKey.toLatin1()));
        QCOMPARE(QJsonDocument::fromJson(api.body).object()["text"].toString(), QStringLiteral("Hello there"));
    }

    void longRepliesAreSpokenSentenceBySentence()
    {
        FakeStt api;
        api.reply = deepgramPcm(0.3);
        SpeechOut s;
        s.streamPlayer()->setDeviceDisabled(true);
        s.setDeepgramApiBase(api.base());
        s.setEngine(QStringLiteral("deepgram"));
        s.deepgramSettings()->setApiKey(kKey);
        s.setEnabled(true);
        const QString reply = QStringLiteral("I opened Notepad for you and put the cursor in the first line. It is ready whenever you want to start typing. Is there anything else you need me to do today?");
        const int chunks = int(SpeechOut::splitSpeechChunks(reply).size());
        QVERIFY(chunks >= 2);                     // spoken in pieces, so the first sound comes after one sentence
        s.say(reply);
        QTRY_COMPARE_WITH_TIMEOUT(s.streamPlayer()->totalFrames(), qint64(chunks) * 7200, 8000);   // every sentence, in order
        QCOMPARE(api.requests, chunks);
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 3000);
    }

    void aRejectedKeyIsReportedAndNothingIsSpoken()
    {
        FakeStt api;
        api.status = 401;
        api.reply = "{\"err_code\":\"INVALID_AUTH\",\"err_msg\":\"Invalid credentials.\"}";
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setDeepgramApiBase(api.base());
        s.setEngine(QStringLiteral("deepgram"));
        s.deepgramSettings()->setApiKey(kKey);
        s.setEnabled(true);
        s.say(QStringLiteral("Please read me"));
        // No fallback voice any more: the failure is reported and that is all.
        QTRY_VERIFY_WITH_TIMEOUT(!s.error().isEmpty() && !s.speaking(), 5000);
        QVERIFY2(s.error().contains(QStringLiteral("rejected the API key")), qPrintable(s.error()));
        QCOMPARE(s.streamPlayer()->totalFrames(), qint64(0));
    }

    void outOfCreditIsSaid()
    {
        FakeStt api;
        api.status = 402;
        api.reply = "{}";
        SpeechOut s;
        s.player()->setDeviceDisabled(true);
        s.setDeepgramApiBase(api.base());
        s.setEngine(QStringLiteral("deepgram"));
        s.deepgramSettings()->setApiKey(kKey);
        s.setEnabled(true);
        s.say(QStringLiteral("Hello"));
        QTRY_VERIFY_WITH_TIMEOUT(!s.error().isEmpty(), 5000);
        QVERIFY(s.error().contains(QStringLiteral("out of credit")));
    }

    void withoutAKeyNothingIsSpoken()
    {
        SpeechOut s;
        s.setEngine(QStringLiteral("deepgram"));
        s.setEnabled(true);
        QVERIFY(!s.available());
        QVERIFY(s.problem().contains(QStringLiteral("API key")));
        s.say(QStringLiteral("Hello"));
        QVERIFY(!s.error().isEmpty());       // the problem is the answer; nothing is spoken
        QVERIFY(!s.speaking());
        s.deepgramSettings()->setApiKey(QStringLiteral("nonsense key"));
        QVERIFY(s.problem().contains(QStringLiteral("does not look")));
    }

    void engineStatusListsDeepgram()
    {
        SpeechOut s;
        QVariantMap all = s.engineStatuses();
        QVERIFY(all.contains(QStringLiteral("deepgram")));
        QVERIFY(!all.value(QStringLiteral("deepgram")).toMap().value(QStringLiteral("ready")).toBool());
        s.deepgramSettings()->setApiKey(kKey);
        s.deepgramSettings()->setSpeakVoice(QStringLiteral("aura-2-thalia-en"));
        all = s.engineStatuses();
        const QVariantMap dg = all.value(QStringLiteral("deepgram")).toMap();
        QVERIFY(dg.value(QStringLiteral("ready")).toBool());
        QVERIFY(dg.value(QStringLiteral("detail")).toString().contains(QStringLiteral("Thalia")));
        s.setEngine(QStringLiteral("nonsense"));
        QCOMPARE(s.engine(), QStringLiteral("google"));
        s.setEngine(QStringLiteral("deepgram"));
        QCOMPARE(s.engine(), QStringLiteral("deepgram"));
    }

    void stoppingCancelsTheRequestAndTheSound()
    {
        FakeStt api;
        api.reply = deepgramPcm(0.4);
        api.delayMs = 400;
        SpeechOut s;
        s.streamPlayer()->setDeviceDisabled(true);
        s.setDeepgramApiBase(api.base());
        s.setEngine(QStringLiteral("deepgram"));
        s.deepgramSettings()->setApiKey(kKey);
        s.setEnabled(true);
        s.say(QStringLiteral("A long sentence that should never be heard."));
        QVERIFY(s.speaking());
        s.stop();
        QVERIFY(!s.speaking());
        QTest::qWait(700);
        QCOMPARE(s.streamPlayer()->totalFrames(), qint64(0));
    }
};

QTEST_MAIN(tst_DeepgramRest)
#include "tst_deepgramrest.moc"
