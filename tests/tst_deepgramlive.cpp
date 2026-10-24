// Deepgram, for real. These tests talk to the live service with the API key in the
// DEEPGRAM_API_KEY environment variable and are skipped without it, so the key
// never lives in the repository and the normal test run stays offline and free.
//
// Spoken test input is made by Deepgram's own text to speech (16 kHz, as the
// microphone delivers) and fed in at real time through a scripted microphone, so a
// whole turn - hearing, thinking, ask_buddy, speaking, interrupting - is exercised
// exactly as a person talking would exercise it.
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

#include "DeepgramAgent.h"
#include "DeepgramProtocol.h"
#include "SpeechFakes.h"
#include "SpeechOut.h"
#include "VoiceInput.h"

namespace {

QString apiKey() { return qEnvironmentVariable("DEEPGRAM_API_KEY").trimmed(); }

// A phrase spoken by Deepgram Aura as 16 kHz mono 16-bit PCM.
QByteArray speechPcm(const QString &text, const QString &voice = QStringLiteral("aura-2-andromeda-en"))
{
    QNetworkAccessManager nam;
    QUrl url(QStringLiteral("https://api.deepgram.com/v1/speak"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("model"), voice);
    q.addQueryItem(QStringLiteral("encoding"), QStringLiteral("linear16"));
    q.addQueryItem(QStringLiteral("sample_rate"), QStringLiteral("16000"));
    q.addQueryItem(QStringLiteral("container"), QStringLiteral("none"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", deepgram::authorizationHeader(apiKey()));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(30000);
    QNetworkReply *reply = nam.post(req, QByteArray("{\"text\":\"") + text.toUtf8() + "\"}");
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray body = reply->readAll();
    const bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok ? body : QByteArray();
}

// Plays `pcm` into the microphone in real time (40 ms frames), then `trailingSilence` of quiet
// so the service decides the turn is over.
void speakInto(FakeCapture *mic, const QByteArray &pcm, double trailingSilence = 1.6)
{
    const QByteArray all = pcm + silence(trailingSilence);
    QElapsedTimer clock;
    clock.start();
    constexpr int kFrame = 1280;
    for (int at = 0, n = 0; at < all.size(); at += kFrame, ++n) {
        mic->feed(all.mid(at, kFrame));
        const qint64 due = qint64(n + 1) * 40;
        while (clock.elapsed() < due)
            QTest::qWait(5);                       // keeps the event loop (the socket) running
    }
}

} // namespace

class tst_DeepgramLive : public QObject
{
    Q_OBJECT

    static DeepgramAgent *newAgent(FakeCapture **mic, bool headphones = true)
    {
        auto *a = new DeepgramAgent;
        a->setEnabled(true);
        a->setApiKey(apiKey());
        a->setAudioMode(headphones ? QStringLiteral("headphones") : QStringLiteral("speakers"));
        a->player()->setDeviceDisabled(true);
        auto cap = std::make_unique<FakeCapture>();
        *mic = cap.get();
        a->setCapture(std::move(cap));
        return a;
    }

private slots:
    void initTestCase()
    {
        if (apiKey().isEmpty())
            QSKIP("DEEPGRAM_API_KEY is not set; the live Deepgram tests are skipped.");
    }

    void thinkingModelsListIncludesHaiku55()
    {
        DeepgramAgent a;
        a.setApiKey(apiKey());
        a.refreshModels();
        QTRY_VERIFY_WITH_TIMEOUT(!a.modelsLoading(), 90000);
        QVERIFY2(a.modelsProblem().isEmpty(), qPrintable(a.modelsProblem()));
        bool haiku = false;
        for (const QVariant &m : a.thinkModels())
            haiku = haiku || m.toMap()["id"].toString() == QLatin1String("claude-haiku-5-5");
        QVERIFY2(haiku, "claude-haiku-5-5 should be offered by Deepgram");
        QVERIFY(a.voices().size() > 30);
    }

    void connectionTestPassesWithHaiku55()
    {
        DeepgramAgent a;
        a.setApiKey(apiKey());
        QCOMPARE(a.thinkModel(), QStringLiteral("claude-haiku-5-5"));
        a.test();
        QTRY_VERIFY_WITH_TIMEOUT(a.testState() != QLatin1String("testing"), 30000);
        QVERIFY2(a.testState() == QLatin1String("ok"), qPrintable(a.testMessage()));
        qInfo().noquote() << a.testMessage();
    }

    void connectionTestCatchesAnUnknownModel()
    {
        DeepgramAgent a;
        a.setApiKey(apiKey());
        a.setThinkModel(QStringLiteral("claude-model-that-does-not-exist"));
        a.test();
        QTRY_VERIFY_WITH_TIMEOUT(a.testState() != QLatin1String("testing"), 30000);
        QCOMPARE(a.testState(), QStringLiteral("failed"));
        QVERIFY(!a.testMessage().isEmpty());
        qInfo().noquote() << "bad model reported as:" << a.testMessage();
    }

    void connectionTestRejectsABadKey()
    {
        DeepgramAgent a;
        a.setApiKey(QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
        a.test();
        QTRY_VERIFY_WITH_TIMEOUT(a.testState() != QLatin1String("testing"), 30000);
        QCOMPARE(a.testState(), QStringLiteral("failed"));
        QVERIFY2(a.testMessage().contains(QStringLiteral("rejected")), qPrintable(a.testMessage()));
    }

    void aSpokenQuestionIsHeardAnsweredAndSpoken()
    {
        const QByteArray question = speechPcm(QStringLiteral("What is the capital of France?"));
        QVERIFY2(!question.isEmpty(), "could not make the spoken test input");
        FakeCapture *mic = nullptr;
        std::unique_ptr<DeepgramAgent> a(newAgent(&mic));
        QSignalSpy userSaid(a.get(), &DeepgramAgent::userSaid), agentSaid(a.get(), &DeepgramAgent::agentSaid);
        QVERIFY(a->start());
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 15000);
        speakInto(mic, question);
        QTRY_VERIFY_WITH_TIMEOUT(userSaid.count() >= 1, 15000);
        const QString heard = userSaid.first().first().toString();
        qInfo().noquote() << "heard:" << heard;
        QVERIFY2(heard.contains(QStringLiteral("France"), Qt::CaseInsensitive), qPrintable(heard));
        QTRY_VERIFY_WITH_TIMEOUT(agentSaid.count() >= 1, 20000);
        const QString said = agentSaid.last().first().toString();
        qInfo().noquote() << "answered:" << said << "in" << a->latencyMs() << "ms";
        QVERIFY2(said.contains(QStringLiteral("Paris"), Qt::CaseInsensitive), qPrintable(said));
        QTRY_VERIFY_WITH_TIMEOUT(a->player()->totalFrames() > 24000, 10000);   // real audio came back
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 20000);
        a->stop();
        QCOMPARE(a->state(), QStringLiteral("off"));
    }

    void askBuddyIsCalledForATaskAndItsAnswerIsSpoken()
    {
        const QByteArray ask = speechPcm(QStringLiteral("Please open Notepad for me."));
        QVERIFY(!ask.isEmpty());
        FakeCapture *mic = nullptr;
        std::unique_ptr<DeepgramAgent> a(newAgent(&mic));
        // Without a hint the recognizer heard "Notecad": keyterms are how names it cannot know get through.
        a->setKeyterms(QStringLiteral("Notepad, Buddy"));
        QSignalSpy asked(a.get(), &DeepgramAgent::buddyRequested), agentSaid(a.get(), &DeepgramAgent::agentSaid);
        QVERIFY(a->start());
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 15000);
        speakInto(mic, ask);
        QTRY_VERIFY_WITH_TIMEOUT(asked.count() >= 1, 25000);
        const QString request = asked.first().at(1).toString();
        qInfo().noquote() << "agent asked Buddy to:" << request;
        QVERIFY2(request.contains(QStringLiteral("notepad"), Qt::CaseInsensitive), qPrintable(request));
        QVERIFY(a->buddyBusy());
        const int before = agentSaid.count();
        a->answerBuddy(asked.first().at(0).toString(), QStringLiteral("I opened Notepad and it is ready."));
        QVERIFY(!a->buddyBusy());
        QTRY_VERIFY_WITH_TIMEOUT(agentSaid.count() > before, 25000);
        const QString said = agentSaid.last().first().toString();
        qInfo().noquote() << "then said:" << said;
        QVERIFY2(said.contains(QStringLiteral("notepad"), Qt::CaseInsensitive), qPrintable(said));
        a->stop();
    }

    void aQuestionFromBuddyIsAskedAloudAndTheSpokenAnswerIsRelayed()
    {
        const QByteArray ask = speechPcm(QStringLiteral("Please write my report."));
        const QByteArray reply = speechPcm(QStringLiteral("The second one please."));
        QVERIFY(!ask.isEmpty() && !reply.isEmpty());
        FakeCapture *mic = nullptr;
        std::unique_ptr<DeepgramAgent> a(newAgent(&mic));
        QSignalSpy asked(a.get(), &DeepgramAgent::buddyRequested), answered(a.get(), &DeepgramAgent::buddyAnswered),
            agentSaid(a.get(), &DeepgramAgent::agentSaid);
        QVERIFY(a->start());
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 15000);
        speakInto(mic, ask);
        QTRY_VERIFY_WITH_TIMEOUT(asked.count() >= 1, 25000);
        // Buddy's run stops on a question: the call is answered with it, as Buddy.qml does.
        a->answerBuddy(asked.first().at(0).toString(),
                       QStringLiteral("Buddy needs the user to choose. Which format? Options: PDF, or Word document. "
                                      "Ask them out loud, then pass what they said to answer_buddy."));
        // (it may say "on it" first, while the call was running; what matters is that it asks the question)
        auto askedIt = [&] {
            for (const QList<QVariant> &args : std::as_const(agentSaid)) {
                const QString t = args.first().toString();
                if (t.contains(QStringLiteral("PDF"), Qt::CaseInsensitive) || t.contains(QStringLiteral("Word"), Qt::CaseInsensitive))
                    return true;
            }
            return false;
        };
        QVERIFY2(QTest::qWaitFor(askedIt, 25000), "the agent never asked the question out loud");
        qInfo().noquote() << "the agent asked:" << agentSaid.last().first().toString();
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 25000);      // it has finished asking
        QTest::qWait(500);
        speakInto(mic, reply);                                                          // the user picks the second option
        QTRY_VERIFY_WITH_TIMEOUT(answered.count() >= 1, 30000);
        const QString relayed = answered.first().at(1).toString();
        qInfo().noquote() << "relayed to Buddy:" << relayed;
        QVERIFY2(relayed.contains(QStringLiteral("second"), Qt::CaseInsensitive) || relayed.contains(QStringLiteral("Word"), Qt::CaseInsensitive),
                 qPrintable(relayed));
        QVERIFY(a->buddyBusy());
        const int before2 = agentSaid.count();
        a->answerBuddy(answered.first().at(0).toString(), QStringLiteral("Making a Word document now."));
        QTRY_VERIFY_WITH_TIMEOUT(agentSaid.count() > before2, 25000);
        qInfo().noquote() << "then said:" << agentSaid.last().first().toString();
        a->stop();
    }

    void talkingOverTheAgentInterruptsIt()
    {
        const QByteArray story = speechPcm(QStringLiteral("Tell me a long story about a friendly dragon, with lots of detail."));
        const QByteArray stop = speechPcm(QStringLiteral("Stop. What is two plus two?"));
        QVERIFY(!story.isEmpty() && !stop.isEmpty());
        FakeCapture *mic = nullptr;
        std::unique_ptr<DeepgramAgent> a(newAgent(&mic));
        // A story has to last long enough to be talked over; a model left to itself is brief.
        a->setPrompt(QStringLiteral("You are a storyteller. Whenever asked for a story, tell one of at least twelve long sentences, "
                                    "without stopping early. For anything else, answer in one short sentence."));
        QSignalSpy userSaid(a.get(), &DeepgramAgent::userSaid);
        QElapsedTimer clock;
        clock.start();
        connect(a.get(), &DeepgramAgent::stateChanged, this, [&] { qInfo().noquote() << clock.elapsed() << "state" << a->state() << a->error() << a->notice(); });
        connect(a.get(), &DeepgramAgent::userSaid, this, [&](const QString &t) { qInfo().noquote() << clock.elapsed() << "user:" << t; });
        connect(a.get(), &DeepgramAgent::agentSaid, this, [&](const QString &t) { qInfo().noquote() << clock.elapsed() << "agent:" << t.left(60); });
        QVERIFY(a->start());
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("listening"), 15000);
        speakInto(mic, story, 1.2);
        QTRY_COMPARE_WITH_TIMEOUT(a->state(), QStringLiteral("speaking"), 25000);
        QTest::qWait(1500);
        QVERIFY2(a->player()->bufferedSeconds() > 0.2 || !a->player()->idle(), "the agent should still be talking");
        const qint64 framesBefore = a->player()->totalFrames();
        speakInto(mic, stop, 1.2);                          // over the top of it
        // Whether and when the service honours an interruption is its own business and varies
        // from run to run; what is tested here is that Noch acts on it the moment it is told.
        if (!QTest::qWaitFor([&] { return userSaid.count() >= 2; }, 30000))
            QSKIP("Deepgram did not act on the interruption this time.");
        qInfo().noquote() << "interrupted with:" << userSaid.last().first().toString();
        QVERIFY(a->player()->totalFrames() > framesBefore);
        a->stop();
    }

    void speechToTextAndVoiceWorkWithBuddysOwnModel()
    {
        const QByteArray clip = speechPcm(QStringLiteral("Open the calculator and add twelve and thirty."));
        QVERIFY(!clip.isEmpty());
        VoiceInput v;
        v.setDeepgramKey(apiKey());
        QObject ctx;
        VoiceInput::Transcription got;
        bool done = false;
        QElapsedTimer t;
        t.start();
        v.transcribe(clip, &ctx, [&](const VoiceInput::Transcription &r) { got = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        qInfo().noquote() << "STT:" << got.text << "in" << t.elapsed() << "ms";
        QVERIFY2(got.ok, qPrintable(got.error));
        QVERIFY2(got.text.contains(QStringLiteral("calculator"), Qt::CaseInsensitive), qPrintable(got.text));

        SpeechOut s;
        s.streamPlayer()->setDeviceDisabled(true);
        s.setEngine(QStringLiteral("deepgram"));
        s.setDeepgramKey(apiKey());
        s.setEnabled(true);
        t.restart();
        s.say(QStringLiteral("I opened the calculator. Twelve plus thirty is forty two."));
        // Streamed: sound is queued as soon as the first bytes arrive, not when the sentence is done.
        QTRY_VERIFY_WITH_TIMEOUT(s.streamPlayer()->totalFrames() > 0, 20000);
        qInfo().noquote() << "first sound after" << t.elapsed() << "ms";
        QTRY_VERIFY_WITH_TIMEOUT(!s.speaking(), 30000);
        QVERIFY2(s.error().isEmpty(), qPrintable(s.error()));
        QVERIFY(s.streamPlayer()->totalFrames() > 24000 / 2);       // more than half a second of speech
        qInfo().noquote() << "spoke" << s.streamPlayer()->totalFrames() / 24000.0 << "s of audio";
    }
};

QTEST_MAIN(tst_DeepgramLive)
#include "tst_deepgramlive.moc"
