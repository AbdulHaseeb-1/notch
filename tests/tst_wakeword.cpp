// Wake word: phrase matching, finding utterances in audio, spoken summaries, and
// the whole listen -> check -> command flow (fake microphone, fake speech server).
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QUrl>

#include "OrtRuntime.h"
#include "SpeechOut.h"
#include "WakeDetector.h"
#include "WakeWord.h"
#include "SpeechFakes.h"

namespace {

// Speech-like: a loud, varying tone.
QByteArray speech(double seconds) { return tone(seconds, 250, 0.35); }
QByteArray hum(double seconds, double amp) { return tone(seconds, 50, amp); }

// What the scripted speech-to-text engine answers.
QString reply(const QString &text) { return text; }

// 16 kHz mono 16-bit PCM out of a canonical 44-byte-header WAV fixture.
QByteArray fixturePcm(const QString &name)
{
    QFile f(QStringLiteral(NOCH_TEST_DATA_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    const QByteArray wav = f.readAll();
    const int at = wav.indexOf("data");
    return at < 0 ? QByteArray() : wav.mid(at + 8);
}

// Feeds audio the way a microphone delivers it: in 20 ms pieces, here at about
// ten times real time (a burst would look like a detector that has fallen behind).
void play(FakeCapture *mic, const QByteArray &pcm)
{
    constexpr int piece = 640;
    int n = 0;
    for (int at = 0; at < pcm.size(); at += piece) {
        mic->feed(pcm.mid(at, piece));
        if (++n % 4 == 0)
            QTest::qWait(8);
    }
}

// One utterance as the microphone would hear it: speech, then enough quiet to end it.
QByteArray utterance(double seconds = 1.0) { return speech(seconds) + silence(1.0); }

} // namespace

class TstWakeWord : public QObject
{
    Q_OBJECT

    struct Rig {
        VoiceInput voice;
        FakeTranscriber &stt;
        FakeCapture *voiceMic = new FakeCapture;
        static FakeTranscriber *install(VoiceInput &v) {
            auto *t = new FakeTranscriber;
            v.setTranscriber(std::unique_ptr<Transcriber>(t));
            return t;
        }
        WakeWord wake{ &voice };
        QTemporaryDir noModels;   // an empty folder: the phrase is found by the speech engine, whatever this machine has installed
        FakeCapture *mic = new FakeCapture;
        QSignalSpy woke{ &wake, &WakeWord::woke };
        QSignalSpy command{ &wake, &WakeWord::command };
        QSignalSpy timedOut{ &wake, &WakeWord::timedOut };
        QSignalSpy failed{ &wake, &WakeWord::failed };
        Rig(bool on = true) : stt(*install(voice)) {
            voice.setEnabled(true);
            voice.setCapture(std::unique_ptr<AudioCapture>(voiceMic));
            wake.setCapture(std::unique_ptr<AudioCapture>(mic));
            wake.setCooldownMs(0);
            wake.setModelFolder(noModels.path());
            if (on)
                wake.setEnabled(true);
        }
        void say(double seconds = 1.0) { mic->feed(utterance(seconds)); }
    };

private slots:
    // ---- the sustain vote ---------------------------------------------------------

    void detectorModelStatusListsItsFiles() {
        Rig r(false);
        const QVariantMap s = r.wake.detectorModelStatus();
        const QVariantList files = s.value(QStringLiteral("files")).toList();
        QCOMPARE(files.size(), 3);
        QStringList names;
        for (const QVariant &f : files) {
            const QVariantMap m = f.toMap();
            names << m.value(QStringLiteral("name")).toString();
            // Consistency check, not content: found must match the path.
            QCOMPARE(m.value(QStringLiteral("found")).toBool(),
                     !m.value(QStringLiteral("path")).toString().isEmpty()
                         && QFile::exists(m.value(QStringLiteral("path")).toString()));
        }
        QVERIFY(names.contains(QStringLiteral("melspectrogram.onnx")));
        QVERIFY(names.contains(QStringLiteral("embedding_model.onnx")));
    }

    void aLoneHotChunkIsNotAWake() {
        // One above-threshold score, then quiet: a phoneme coincidence, not a phrase.
        int streak = 0;
        QVERIFY(!wake::confirmSustained(streak, 0.9f, 0.5f, 2));
        QVERIFY(!wake::confirmSustained(streak, 0.1f, 0.5f, 2));   // streak resets
        QVERIFY(!wake::confirmSustained(streak, 0.9f, 0.5f, 2));   // one hot chunk is not enough
    }

    void twoConsecutiveHotChunksAreAWake() {
        int streak = 0;
        QVERIFY(!wake::confirmSustained(streak, 0.6f, 0.5f, 2));
        QVERIFY(wake::confirmSustained(streak, 0.7f, 0.5f, 2));    // fires exactly once
        QVERIFY(!wake::confirmSustained(streak, 0.8f, 0.5f, 2));   // needs a fresh streak
    }

    void aSubThresholdChunkBreaksTheStreak() {
        int streak = 0;
        QVERIFY(!wake::confirmSustained(streak, 0.6f, 0.5f, 2));
        QVERIFY(!wake::confirmSustained(streak, 0.49f, 0.5f, 2));  // just under: reset
        QVERIFY(!wake::confirmSustained(streak, 0.6f, 0.5f, 2));   // one again: not enough
    }

    // ---- the phrase ---------------------------------------------------------------

    void normalizesWords() {
        QCOMPARE(wake::normalizeWord("Búddy!"), QStringLiteral("buddy"));
        QCOMPARE(wake::normalizeWord("  HEY, "), QStringLiteral("hey"));
        QCOMPARE(wake::normalizeWord("—"), QString());
        QCOMPARE(wake::normalizeWord("R2-D2"), QStringLiteral("r2d2"));
    }

    void phraseValidation_data() {
        QTest::addColumn<QString>("phrase");
        QTest::addColumn<bool>("ok");
        QTest::newRow("default") << "hey buddy" << true;
        QTest::newRow("one word") << "jarvis" << true;
        QTest::newRow("four words") << "ok there noch buddy" << true;
        QTest::newRow("apostrophe") << "what's up buddy" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("blank") << "   " << false;
        QTest::newRow("too short") << "yo" << false;
        QTest::newRow("short words") << "a b c" << false;
        QTest::newRow("five words") << "one two three four five" << false;
        QTest::newRow("symbols") << "hey <buddy>" << false;
        QTest::newRow("too long") << QString(41, 'a') << false;
    }
    void phraseValidation() {
        QFETCH(QString, phrase);
        QFETCH(bool, ok);
        QCOMPARE(wake::phraseProblem(phrase).isEmpty(), ok);
    }

    void matches_data() {
        QTest::addColumn<QString>("said");
        QTest::addColumn<QString>("phrase");
        QTest::addColumn<bool>("found");
        QTest::addColumn<QString>("rest");
        const QString hb = "hey buddy";
        QTest::newRow("plain") << "Hey buddy" << hb << true << "";
        QTest::newRow("punctuation") << "Hey, Buddy!" << hb << true << "";
        QTest::newRow("with request") << "Hey Buddy open notepad" << hb << true << "open notepad";
        QTest::newRow("comma then request") << "Hey buddy, open notepad." << hb << true << "open notepad.";
        QTest::newRow("dash then request") << "hey buddy - what's the time?" << hb << true << "what's the time?";
        QTest::newRow("keeps their words") << "HEY BUDDY, CHECK my Email" << hb << true << "CHECK my Email";
        QTest::newRow("one word") << "heybuddy do it" << hb << true << "do it";
        QTest::newRow("one letter off in a long word") << "hey buddi what now" << hb << true << "what now";
        QTest::newRow("filler before") << "okay hey buddy what's up" << hb << true << "what's up";
        QTest::newRow("accents") << "hey búddy" << hb << true << "";
        QTest::newRow("single word phrase") << "Jarvis, status report" << "jarvis" << true << "status report";
        QTest::newRow("filler before single word") << "hey jarvis lights on" << "jarvis" << true << "lights on";
        QTest::newRow("long word two letters off") << "computerize open it" << "computer" << false << "";
        QTest::newRow("phrase typed with caps") << "hey buddy go" << "Hey  BUDDY" << true << "go";

        QTest::newRow("in the middle of a sentence") << "I told my buddy hey" << hb << false << "";
        QTest::newRow("late in the sentence") << "hey there buddy" << hb << false << "";
        QTest::newRow("wrong order") << "buddy hey" << hb << false << "";
        QTest::newRow("only half of it") << "hey bud" << hb << false << "";
        QTest::newRow("only hey") << "hey" << hb << false << "";
        QTest::newRow("short word must be exact") << "hay buddy" << hb << false << "";
        QTest::newRow("other name") << "hey bobby" << hb << false << "";
        QTest::newRow("single word after a non-filler") << "my buddy said" << "buddy" << false << "";
        QTest::newRow("empty") << "" << hb << false << "";
        QTest::newRow("empty phrase") << "hey buddy" << "" << false << "";
        QTest::newRow("punctuation only") << "... !!" << hb << false << "";
    }
    void matches() {
        QFETCH(QString, said);
        QFETCH(QString, phrase);
        QFETCH(bool, found);
        QFETCH(QString, rest);
        const wake::Match m = wake::match(said, phrase);
        QCOMPARE(m.found, found);
        QCOMPARE(m.rest, rest);
    }

    void spokenSummaryIsShortAndPlain() {
        QCOMPARE(wake::spokenSummary("Done. I created the file."), QStringLiteral("Done. I created the file."));
        QCOMPARE(wake::spokenSummary("**Done!** See [the docs](https://example.com/x) now."),
                 QStringLiteral("Done! See the docs now."));
        QCOMPARE(wake::spokenSummary("Open https://example.com/long/path now."), QStringLiteral("Open now."));
        const QString withCode = wake::spokenSummary("Here is the fix:\n```cpp\nint x = 1;\n```\nIt works now.");
        QVERIFY2(!withCode.contains("int x"), qPrintable(withCode));
        QVERIFY(withCode.contains("It works now."));
        const QString list = wake::spokenSummary("Results:\n- one\n- two\n1. three");
        QVERIFY2(!list.contains("- ") && !list.contains("1."), qPrintable(list));
        QVERIFY(list.contains("one") && list.contains("three"));
        QCOMPARE(wake::spokenSummary("  \n  "), QString());
        QCOMPARE(wake::spokenSummary("```\nonly code\n```"), QString());
    }

    void spokenSummaryCutsOnASentence() {
        const QString text = "First sentence is here. " + QString("Second sentence goes on and on. ").repeated(20);
        const QString s = wake::spokenSummary(text, 100);
        QVERIFY(s.size() <= 100);
        QVERIFY2(s.endsWith('.'), qPrintable(s));
        QVERIFY(s.startsWith("First sentence is here."));
    }

    void spokenSummaryCutsALongSentenceAtAWord() {
        const QString s = wake::spokenSummary(QString("word ").repeated(200), 60);
        QVERIFY(s.size() <= 61);
        QVERIFY(s.endsWith(QChar(0x2026)));
        QVERIFY(!s.contains("wor…"));
    }

    // ---- finding utterances ---------------------------------------------------------

    void silenceMakesNoUtterance() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        QVERIFY(!seg.feed(silence(5), &out));
        QVERIFY(out.isEmpty());
    }

    void aClickIsNotAWord() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        seg.feed(silence(1) + speech(0.1) + silence(2), &out);
        QVERIFY(out.isEmpty());
    }

    void oneUtteranceComesOutWholeWithItsLeadIn() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        const bool started = seg.feed(silence(1) + speech(1.0) + silence(1.5), &out);
        QVERIFY(started);
        QCOMPARE(out.size(), 1);
        const double secs = voice::seconds(out.first());
        QVERIFY2(secs >= 1.0 && secs <= 1.0 + 0.3 + 0.25 + 0.05, qPrintable(QString::number(secs)));   // lead-in and a short tail
        QVERIFY(voice::peak(out.first()) > 0.3);
    }

    void aShortPauseDoesNotSplitASentence() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        seg.feed(speech(0.8) + silence(0.4) + speech(0.8) + silence(1.5), &out);
        QCOMPARE(out.size(), 1);
    }

    void aLongPauseSplitsTwoUtterances() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        seg.feed(speech(0.8) + silence(1.2) + speech(0.8) + silence(1.5), &out);
        QCOMPARE(out.size(), 2);
    }

    void endlessSpeechIsCutAtTheMaximum() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        seg.feed(speech(20), &out);
        QVERIFY(out.size() >= 2);
        for (const QByteArray &u : out)
            QVERIFY(voice::seconds(u) <= 8.1);
    }

    void aSteadyHumIsLearnedAsTheRoom() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        // A fan louder than the minimum: after a moment it is "the room", not speech.
        seg.feed(hum(0.3, 0.06) + hum(6, 0.06), &out);
        const int fromHum = int(out.size());
        QVERIFY2(fromHum <= 1, "at most the first fraction of a second may count");
        out.clear();
        seg.feed(hum(1, 0.06) + speech(1.0) + hum(1.5, 0.06), &out);
        QCOMPARE(out.size(), 1);   // speech over the hum is still heard
    }

    void chunkSizeDoesNotChangeTheResult() {
        const QByteArray all = silence(0.5) + speech(1.0) + silence(1.2) + speech(0.9) + silence(1.5);
        wake::Segmenter whole;
        QList<QByteArray> a;
        whole.feed(all, &a);
        wake::Segmenter pieces;
        QList<QByteArray> b;
        for (int i = 0; i < all.size(); i += 333)   // odd, not frame-aligned, not even sample-aligned
            pieces.feed(all.mid(i, 333), &b);
        QCOMPARE(b.size(), a.size());
        for (int i = 0; i < a.size(); ++i)
            QCOMPARE(b.at(i).size(), a.at(i).size());
    }

    void resetForgetsAHalfHeardUtterance() {
        wake::Segmenter seg;
        QList<QByteArray> out;
        seg.feed(speech(0.5), &out);
        QVERIFY(seg.inUtterance());
        seg.reset();
        QVERIFY(!seg.inUtterance());
        seg.feed(silence(2), &out);
        QVERIFY(out.isEmpty());
    }

    // ---- the whole flow -------------------------------------------------------------

    void offByDefaultAndNothingHappens() {
        Rig r(false);
        QCOMPARE(r.wake.state(), QStringLiteral("off"));
        QVERIFY(!r.wake.active());
        QCOMPARE(r.mic->starts, 0);
        r.say();
        QTest::qWait(150);
        QCOMPARE(r.stt.requests.load(), 0);
    }

    void listensOnceEnabledAndSaysSo() {
        Rig r;
        QCOMPARE(r.mic->starts, 1);
        QVERIFY(r.wake.active());
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
        r.wake.setEnabled(false);
        QCOMPARE(r.mic->stops, 1);
        QVERIFY(!r.wake.active());
        QCOMPARE(r.wake.state(), QStringLiteral("off"));
    }

    void phraseWithARequestRunsIt() {
        Rig r;
        r.stt.reply = reply("Hey Buddy, open notepad");
        r.say();
        QTRY_COMPARE(r.command.count(), 1);
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("open notepad."));
        QCOMPARE(r.woke.count(), 1);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void whatIsTranscribedIsJustTheUtterance() {
        Rig r;
        r.stt.reply = reply("hey buddy go");
        r.say(1.0);
        QTRY_COMPARE(r.stt.requests.load(), 1);
        const int bytes = r.stt.pcm().size();
        QVERIFY(bytes > 16000);                    // about a second of audio, not empty
        QVERIFY(bytes < 16000 * 2 * 3);            // and not everything since the microphone opened
    }

    void phraseAloneThenTheRequestInTheNextBreath() {
        Rig r;
        r.stt.replies = { reply("Hey buddy."), reply("what is the weather today") };
        r.say(0.8);
        QTRY_COMPARE(r.woke.count(), 1);
        QCOMPARE(r.command.count(), 0);
        QCOMPARE(r.wake.state(), QStringLiteral("awaiting"));
        r.mic->feed(speech(1.5) + silence(1.5));   // a request may be followed by a longer pause than a phrase
        QTRY_COMPARE(r.command.count(), 1);
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("What is the weather today."));
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void theRequestAfterTheBeepMayPauseLongerThanASentence() {
        Rig r;
        r.stt.replies = { reply("hey buddy"), reply("open the calendar and then the mail") };
        r.say(0.8);
        QTRY_COMPARE(r.wake.state(), QStringLiteral("awaiting"));
        // 1 s of quiet in the middle would split a normal utterance; a request is allowed longer.
        r.mic->feed(speech(0.8) + silence(1.0) + speech(0.8) + silence(1.5));
        QTRY_COMPARE(r.command.count(), 1);
        QCOMPARE(r.stt.requests.load(), 2);   // one request, not two halves
    }

    void phraseAloneAndThenNothingTimesOut() {
        Rig r;
        r.wake.setAwaitStartMs(80);
        r.stt.reply = reply("hey buddy");
        r.say();
        QTRY_COMPARE(r.woke.count(), 1);
        QTRY_COMPARE(r.timedOut.count(), 1);
        QCOMPARE(r.command.count(), 0);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void startingToSpeakHoldsOffTheTimeout() {
        Rig r;
        r.wake.setAwaitStartMs(150);
        r.stt.replies = { reply("hey buddy"), reply("do the thing") };
        r.say(0.6);
        QTRY_COMPARE(r.wake.state(), QStringLiteral("awaiting"));
        r.mic->feed(speech(0.5));      // speaking has begun, but has not ended yet
        QTest::qWait(300);             // longer than the wait
        QCOMPARE(r.timedOut.count(), 0);
        r.mic->feed(silence(1.5));
        QTRY_COMPARE(r.command.count(), 1);
    }

    void ordinaryTalkIsForgotten() {
        Rig r;
        r.stt.reply = reply("I was telling my buddy that hey buddy is a good greeting");
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 1);
        QTest::qWait(100);
        QCOMPARE(r.woke.count(), 0);
        QCOMPARE(r.command.count(), 0);
        QCOMPARE(r.failed.count(), 0);
        QVERIFY(r.wake.error().isEmpty());
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void silenceAndRoomNoiseNeverLeaveTheComputer() {
        Rig r;
        r.mic->feed(silence(5));
        r.mic->feed(hum(5, 0.01));
        r.mic->feed(speech(0.1) + silence(1.5));   // a click
        QTest::qWait(150);
        QCOMPARE(r.stt.requests.load(), 0);
    }

    void aSecondUtteranceWhileOneIsBeingCheckedIsDroppedNotQueued() {
        Rig r;
        r.stt.delayMs = 300;
        r.stt.reply = reply("nothing to see");
        r.say();
        r.say();
        r.say();
        QTest::qWait(600);
        QCOMPARE(r.stt.requests.load(), 1);
        QCOMPARE(r.wake.droppedUtterances(), 2);
    }

    void everySentenceIsCheckedNothingIsCapped() {
        // The old speech-service check had to cap itself at 12 a minute, which made it
        // deaf to the phrase in a talkative room. On this computer there is no cap.
        Rig r;
        r.stt.reply = reply("just talking");
        const int tries = 16;
        for (int i = 0; i < tries; ++i) {
            const int before = r.stt.requests.load();
            r.say(0.6);
            QTRY_COMPARE(r.stt.requests.load(), before + 1);
        }
        QCOMPARE(r.stt.requests.load(), tries);
        QCOMPARE(r.wake.droppedUtterances(), 0);
    }

    void aSecondWakeRightAfterTheFirstIsIgnored() {
        Rig r;
        r.wake.setCooldownMs(600);
        r.stt.reply = reply("hey buddy open notepad");
        r.say();
        QTRY_COMPARE(r.command.count(), 1);
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 2);
        QTest::qWait(100);
        QCOMPARE(r.command.count(), 1);
        QTest::qWait(600);
        r.say();
        QTRY_COMPARE(r.command.count(), 2);
    }

    void aVeryLongRequestIsCapped() {
        Rig r;
        r.stt.reply = reply("hey buddy " + QString(5000, 'x'));
        r.say();
        QTRY_COMPARE(r.command.count(), 1);
        QCOMPARE(r.command.first().first().toString().size(), WakeWord::kMaxCommandChars);
    }

    void changingThePhraseTakesEffect() {
        Rig r;
        r.wake.setPhrase("computer");
        r.stt.reply = reply("Computer, lights on");
        r.say();
        QTRY_COMPARE(r.command.count(), 1);
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("lights on."));
        r.stt.reply = reply("hey buddy lights off");
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 2);
        QTest::qWait(80);
        QCOMPARE(r.command.count(), 1);   // the old phrase no longer works
    }

    // ---- blocking -----------------------------------------------------------------------

    void needsVoiceInputFirst() {
        Rig r(false);
        r.voice.setEnabled(false);
        r.wake.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        QVERIFY(r.wake.blockedReason().contains("voice input", Qt::CaseInsensitive));
        QCOMPARE(r.mic->starts, 0);
        r.voice.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
        QCOMPARE(r.mic->starts, 1);
    }

    void needsTheSpeechServiceSetUp() {
        Rig r(false);
        r.stt.set([](FakeTranscriber &t) { t.problemText = QStringLiteral("Add your Deepgram API key to use it for speech to text."); });
        r.wake.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        // The plain setup problem is the reason — not a refusal to use a cloud engine.
        QCOMPARE(r.wake.blockedReason(), QStringLiteral("Add your Deepgram API key to use it for speech to text."));
        QCOMPARE(r.mic->starts, 0);                  // the microphone never opens for something that cannot work
    }

    void theSpeechEngineIsNotRefused() {
        Rig r;
        QVERIFY(r.wake.blockedReason().isEmpty());
        QVERIFY(r.wake.active());
        QCOMPARE(r.voice.engine(), QStringLiteral("Fake"));
    }

    void aCloudEngineTranscribesWhatFollowsThePhrase() {
        // Deepgram is asynchronous and in the cloud; the wake word used to
        // refuse such an engine outright. Now the follow-up just works.
        Rig r(false);
        auto *cloud = new AsyncFakeTranscriber;
        cloud->reply = QStringLiteral("hey buddy open notepad");
        r.voice.setTranscriber(std::unique_ptr<Transcriber>(cloud));
        r.wake.setEnabled(true);
        QCOMPARE(r.wake.blockedReason(), QString());
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
        r.say();
        QTRY_COMPARE_WITH_TIMEOUT(r.command.count(), 1, 5000);
        // The command is what followed the phrase in the same utterance.
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("open notepad."));
        QCOMPARE(cloud->requests.load(), 1);
    }

    void badPhraseBlocks() {
        Rig r(false);
        r.wake.setPhrase("yo");
        r.wake.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        QCOMPARE(r.mic->starts, 0);
        r.wake.setPhrase("hey buddy");
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    // ---- sharing the microphone ---------------------------------------------------------

    void pausesWhilePushToTalkIsInUse() {
        Rig r;
        QCOMPARE(r.mic->starts, 1);
        QVERIFY(r.voice.beginListening());
        QCOMPARE(r.mic->stops, 1);
        QCOMPARE(r.wake.state(), QStringLiteral("paused"));
        QVERIFY(!r.wake.active());
        r.voice.cancel();
        QCOMPARE(r.mic->starts, 2);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void pausesWhileBuddySpeaks() {
        Rig r;
        r.wake.setPaused(true);
        QCOMPARE(r.mic->stops, 1);
        QCOMPARE(r.wake.state(), QStringLiteral("paused"));
        r.mic->feed(utterance());     // nothing is listening
        QTest::qWait(100);
        QCOMPARE(r.stt.requests.load(), 0);
        r.wake.setPaused(false);
        QCOMPARE(r.mic->starts, 2);
    }

    void turningItOffAbandonsWhatWasBeingChecked() {
        Rig r;
        r.stt.delayMs = 250;
        r.stt.reply = reply("hey buddy open notepad");
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 1);
        r.wake.setEnabled(false);
        QTest::qWait(500);
        QCOMPARE(r.command.count(), 0);
        QCOMPARE(r.woke.count(), 0);
    }

    // ---- trouble -------------------------------------------------------------------------

    void noMicrophoneSaysSoAndDoesNotRetryInALoop() {
        Rig r(false);
        r.mic->startOk = false;
        r.wake.setEnabled(true);
        QCOMPARE(r.mic->starts, 1);
        QCOMPARE(r.failed.count(), 1);
        QVERIFY(r.wake.error().contains("microphone", Qt::CaseInsensitive));
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        r.voice.setEnabled(false);             // the voice settings changed: one more try
        r.voice.setEnabled(true);
        QCOMPARE(r.mic->starts, 2);
        r.mic->startOk = true;
        r.voice.setEnabled(false);
        r.voice.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void theMicrophoneDyingStopsListening() {
        Rig r;
        r.mic->breakDown("Microphone unplugged");
        QCOMPARE(r.failed.count(), 1);
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        QVERIFY(!r.wake.active());
    }

    void aFailingEngineClosesTheMicrophoneUntilTheSettingsChange() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.errorText = QStringLiteral("Deepgram stopped while writing."); });
        r.say();
        QTRY_COMPARE(r.failed.count(), 1);
        QVERIFY(r.wake.error().contains("Deepgram"));
        QCOMPARE(r.wake.state(), QStringLiteral("blocked"));
        QVERIFY(!r.wake.active());                 // no microphone left open on something that cannot work
        r.say();                                    // nothing hears this
        QTest::qWait(100);
        QCOMPARE(r.stt.requests.load(), 1);
        r.stt.set([](FakeTranscriber &t) { t.errorText.clear(); });
        r.voice.setEnabled(false);                  // a change in the voice settings tries again
        r.voice.setEnabled(true);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
        QVERIFY(r.wake.active());
    }

    void noWordsInTheAnswerIsNotAnError() {
        Rig r;
        r.stt.reply = reply("");
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 1);
        QTest::qWait(80);
        QCOMPARE(r.failed.count(), 0);
        QVERIFY(r.wake.active());
    }

    void noWordsAfterThePhraseCountsAsTimingOut() {
        Rig r;
        r.stt.replies = { reply("hey buddy"), reply("") };
        r.say();
        QTRY_COMPARE(r.wake.state(), QStringLiteral("awaiting"));
        r.mic->feed(speech(1.0) + silence(1.5));
        QTRY_COMPARE(r.timedOut.count(), 1);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void cancelAwaitingGoesBackToWaitingForThePhrase() {
        Rig r;
        r.stt.reply = reply("hey buddy");
        r.say();
        QTRY_COMPARE(r.wake.state(), QStringLiteral("awaiting"));
        r.wake.cancelAwaiting();
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void whatWasHeardIsNeverKept() {
        // The only things that come out are the signals; no property holds a transcript.
        Rig r;
        r.stt.reply = reply("some private conversation about a surprise party");
        r.say();
        QTRY_COMPARE(r.stt.requests.load(), 1);
        QTest::qWait(80);
        const QMetaObject *mo = r.wake.metaObject();
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QVariant v = mo->property(i).read(&r.wake);
            QVERIFY2(!v.toString().contains("party"), mo->property(i).name());
        }
    }

    // ---- the local detector (needs ONNX Runtime; skipped without it) ---------------------

    void localDetectorWakesOnThePhraseAndTranscribesOnlyTheRequest() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        Rig r(false);
        r.stt.reply = reply("open notepad");
        r.wake.setModelFolder(QStringLiteral(NOCH_WAKEWORD_DIR));
        r.wake.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("local"), 20000);
        QTRY_VERIFY(r.mic->open);
        QCOMPARE(r.wake.blockedReason(), QString());   // no consent needed: nothing is sent while it listens

        play(r.mic, silence(1.0) + fixturePcm("hey_buddy_tts.wav"));
        QTRY_COMPARE_WITH_TIMEOUT(r.woke.count(), 1, 10000);
        QCOMPARE(r.wake.state(), QStringLiteral("awaiting"));
        QCOMPARE(r.stt.requests.load(), 0);              // the phrase itself was never transcribed

        play(r.mic, speech(1.0) + silence(1.6));  // "open notepad", then the pause that ends a request
        QTRY_COMPARE_WITH_TIMEOUT(r.command.count(), 1, 10000);
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("Open notepad."));
        QCOMPARE(r.stt.requests.load(), 1);              // only the request
    }

    void localDetectorIgnoresOrdinarySpeechWithoutSendingIt() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        Rig r(false);
        r.wake.setModelFolder(QStringLiteral(NOCH_WAKEWORD_DIR));
        r.wake.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("local"), 20000);
        QTRY_VERIFY(r.mic->open);
        // More sentences in a minute than the speech fallback's cap would allow.
        for (int i = 0; i < 20; ++i)
            play(r.mic, fixturePcm("other_phrase_tts.wav"));
        QTest::qWait(1500);
        QCOMPARE(r.woke.count(), 0);
        QCOMPARE(r.stt.requests.load(), 0);
        QCOMPARE(r.wake.state(), QStringLiteral("listening"));
    }

    void thresholdIsKeptInASaneRange() {
        Rig r(false);
        r.wake.setThreshold(0.99);
        QCOMPARE(r.wake.threshold(), 0.9);
        r.wake.setThreshold(0.01);
        QCOMPARE(r.wake.threshold(), 0.2);
        r.wake.setThreshold(0.6);
        QCOMPARE(r.wake.threshold(), 0.6);
    }

    void localRequestRepeatingThePhraseIsNotTakenAsPartOfIt() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        Rig r(false);
        r.stt.reply = reply("Hey buddy, open notepad");
        r.wake.setModelFolder(QStringLiteral(NOCH_WAKEWORD_DIR));
        r.wake.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("local"), 20000);
        QTRY_VERIFY(r.mic->open);
        play(r.mic, silence(1.0) + fixturePcm("hey_buddy_tts.wav"));
        QTRY_COMPARE_WITH_TIMEOUT(r.woke.count(), 1, 10000);
        play(r.mic, speech(1.0) + silence(1.6));
        QTRY_COMPARE_WITH_TIMEOUT(r.command.count(), 1, 10000);
        QCOMPARE(r.command.first().first().toString(), QStringLiteral("open notepad."));
    }

    // ---- many phrases: finding them ---------------------------------------------------

    void phraseNamesComeFromTheFileName() {
        QCOMPARE(wake::phraseDisplayName("hey_buddy.onnx"), QStringLiteral("Hey Buddy"));
        QCOMPARE(wake::phraseDisplayName("alexa.onnx"), QStringLiteral("Alexa"));
        QCOMPARE(wake::phraseDisplayName("hey_max.onnx"), QStringLiteral("Hey Max"));
        QCOMPARE(wake::phraseDisplayName("ok-computer.onnx"), QStringLiteral("Ok Computer"));
    }

    void discoveryListsBundledAndUserPhrases() {
        QTemporaryDir bundled, user;
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/melspectrogram.onnx", bundled.filePath("melspectrogram.onnx")));
        QVERIFY(QFile::copy(res + "/embedding_model.onnx", bundled.filePath("embedding_model.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", bundled.filePath("hey_buddy.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", bundled.filePath("alexa.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", user.filePath("hey_max.onnx")));
        const QList<wake::PhraseModel> found = wake::discoverPhraseModels(user.path(), bundled.path());
        QCOMPARE(found.size(), 3);   // the two shared feature models are not phrases
        QCOMPARE(found[0].fileName, QStringLiteral("alexa.onnx"));
        QVERIFY(found[0].builtin && !found[0].userAdded);
        QCOMPARE(found[1].fileName, QStringLiteral("hey_buddy.onnx"));
        QCOMPARE(found[1].name, QStringLiteral("Hey Buddy"));
        QVERIFY(found[1].builtin && !found[1].userAdded);
        QCOMPARE(found[2].fileName, QStringLiteral("hey_max.onnx"));
        QCOMPARE(found[2].name, QStringLiteral("Hey Max"));
        QVERIFY(found[2].userAdded && !found[2].builtin);
        QCOMPARE(found[2].path, user.filePath("hey_max.onnx"));
    }

    void aUserCopyShadowsTheBundledPhrase() {
        QTemporaryDir bundled, user;
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", bundled.filePath("hey_buddy.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", user.filePath("hey_buddy.onnx")));
        const QList<wake::PhraseModel> found = wake::discoverPhraseModels(user.path(), bundled.path());
        QCOMPARE(found.size(), 1);   // one phrase, two sources
        QVERIFY(found[0].builtin && found[0].userAdded);
        QCOMPARE(found[0].path, user.filePath("hey_buddy.onnx"));   // the user's copy is in effect
    }

    void modelStatusListsThePhrases() {
        QTemporaryDir models;
        Rig r(false);
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", models.filePath("hey_max.onnx")));
        r.wake.setModelFolder(models.path());
        const QVariantList phrases = r.wake.detectorModelStatus().value(QStringLiteral("phrases")).toList();
        QCOMPARE(phrases.size(), 1);
        const QVariantMap m = phrases.first().toMap();
        QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Hey Max"));
        QCOMPARE(m.value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QVERIFY(!m.value(QStringLiteral("enabled")).toBool());   // not switched on yet
    }

    // ---- many phrases: adding and removing ---------------------------------------------

    void addedWakeWordAppearsSwitchedOn() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        QTemporaryDir models, staging;
        Rig r(false);
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        // The real "Hey Buddy" classifier renamed is the uploaded fixture.
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", staging.filePath("hey_max.onnx")));
        r.wake.setModelFolder(models.path());
        QCOMPARE(r.wake.addWakeWordModel(staging.filePath("hey_max.onnx")), QString());
        QVERIFY(QFile::exists(models.filePath("hey_max.onnx")));   // copied into the models folder
        bool seen = false;
        for (const QVariant &v : r.wake.phrases()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() != QStringLiteral("hey_max.onnx"))
                continue;
            seen = true;
            QCOMPARE(m.value(QStringLiteral("name")).toString(), QStringLiteral("Hey Max"));
            QCOMPARE(m.value(QStringLiteral("source")).toString(), QStringLiteral("user"));
            QVERIFY(m.value(QStringLiteral("enabled")).toBool());   // on at once
        }
        QVERIFY(seen);
        QVERIFY(r.wake.wakeWords().contains(QStringLiteral("hey_max.onnx")));
        // A file:// URL, as the settings file dialog hands over, works too.
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", staging.filePath("hey_ada.onnx")));
        QCOMPARE(r.wake.addWakeWordModel(QUrl::fromLocalFile(staging.filePath("hey_ada.onnx")).toString()), QString());
        QVERIFY(r.wake.wakeWords().contains(QStringLiteral("hey_ada.onnx")));
        // And a phrase the user added can be removed again, file and all.
        QCOMPARE(r.wake.removeWakeWordModel(QStringLiteral("hey_max.onnx")), QString());
        QVERIFY(!QFile::exists(models.filePath("hey_max.onnx")));
        QVERIFY(!r.wake.wakeWords().contains(QStringLiteral("hey_max.onnx")));
    }

    void builtinPhraseCannotBeRemovedOnlySwitchedOff() {
        QTemporaryDir bundled, user;
        Rig r(false);
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", bundled.filePath("hey_buddy.onnx")));
        r.wake.setBundledDir(bundled.path());
        r.wake.setModelFolder(user.path());
        const QString err = r.wake.removeWakeWordModel(QStringLiteral("hey_buddy.onnx"));
        QVERIFY2(!err.isEmpty(), "a built-in phrase must not be removable");
        QVERIFY(err.contains(QStringLiteral("built in"), Qt::CaseInsensitive));
        QVERIFY(QFile::exists(bundled.filePath("hey_buddy.onnx")));
        QVERIFY(!r.wake.removeWakeWordModel(QStringLiteral("not_there.onnx")).isEmpty());
    }

    void addRejectsFilesThatAreNotWakeWords() {
        QTemporaryDir models, staging;
        Rig r(false);
        r.wake.setModelFolder(models.path());
        // Missing entirely.
        QVERIFY(!r.wake.addWakeWordModel(staging.filePath("nope.onnx")).isEmpty());
        // Not an .onnx.
        QFile txt(staging.filePath("hey_max.txt"));
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("hello");
        txt.close();
        QVERIFY(!r.wake.addWakeWordModel(staging.filePath("hey_max.txt")).isEmpty());
        // One of the shared pipeline models is not a phrase.
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/melspectrogram.onnx", staging.filePath("melspectrogram.onnx")));
        const QString pipeline = r.wake.addWakeWordModel(staging.filePath("melspectrogram.onnx"));
        QVERIFY2(pipeline.contains(QStringLiteral("shared"), Qt::CaseInsensitive), qPrintable(pipeline));
        // Garbage with the right suffix is caught by really loading it.
        QString why;
        if (ortrt::load({}, &why)) {
            QFile junk(staging.filePath("junk.onnx"));
            QVERIFY(junk.open(QIODevice::WriteOnly));
            junk.write("not a model");
            junk.close();
            QVERIFY(!r.wake.addWakeWordModel(staging.filePath("junk.onnx")).isEmpty());
            // The embedding model loads as ONNX but is not a phrase classifier.
            QVERIFY(QFile::copy(res + "/embedding_model.onnx", staging.filePath("hey_embed.onnx")));
            QVERIFY(!r.wake.addWakeWordModel(staging.filePath("hey_embed.onnx")).isEmpty());
        }
        // Nothing was copied in or listed.
        QVERIFY(r.wake.phrases().isEmpty());
        QVERIFY(QDir(models.path()).entryList({ QStringLiteral("*.onnx") }, QDir::Files).isEmpty());
    }

    // ---- many phrases: the enabled set drives the detector ----------------------------

    void onlyTheSwitchedOnPhrasesAreListenedFor() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        QTemporaryDir models;
        Rig r(false);
        r.stt.reply = reply("");   // the speech fallback hears nothing: only the detector can wake it
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/melspectrogram.onnx", models.filePath("melspectrogram.onnx")));
        QVERIFY(QFile::copy(res + "/embedding_model.onnx", models.filePath("embedding_model.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", models.filePath("hey_buddy.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", models.filePath("hey_max.onnx")));
        r.wake.setModelFolder(models.path());
        // "Hey Max" is the same classifier renamed: it must be the one that fires.
        r.wake.setWakeWords({ QStringLiteral("hey_max.onnx") });
        r.wake.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("local"), 20000);
        QTRY_VERIFY(r.mic->open);
        QSignalSpy wokePhrase{ &r.wake, &WakeWord::wokePhrase };
        play(r.mic, silence(1.0) + fixturePcm("hey_buddy_tts.wav"));
        QTRY_COMPARE_WITH_TIMEOUT(r.woke.count(), 1, 10000);
        QCOMPARE(wokePhrase.count(), 1);
        QCOMPARE(wokePhrase.first().at(0).toString(), QStringLiteral("hey_max.onnx"));
        QCOMPARE(wokePhrase.first().at(1).toString(), QStringLiteral("Hey Max"));
        // Switch every phrase off: the local detector stands down and the
        // phrase in the recording no longer wakes anything.
        r.wake.cancelAwaiting();
        r.wake.setWakeWords({});
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("speech"), 20000);
        play(r.mic, fixturePcm("hey_buddy_tts.wav"));
        QTest::qWait(1200);
        QCOMPARE(r.woke.count(), 1);
    }

    void twoPhrasesCanBeOnAtOnce() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        QTemporaryDir models;
        Rig r(false);
        r.stt.reply = reply("");
        const QString res = QStringLiteral(NOCH_WAKEWORD_DIR);
        QVERIFY(QFile::copy(res + "/melspectrogram.onnx", models.filePath("melspectrogram.onnx")));
        QVERIFY(QFile::copy(res + "/embedding_model.onnx", models.filePath("embedding_model.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", models.filePath("hey_buddy.onnx")));
        QVERIFY(QFile::copy(res + "/hey_buddy.onnx", models.filePath("hey_max.onnx")));
        r.wake.setModelFolder(models.path());
        r.wake.setWakeWords({ QStringLiteral("hey_buddy.onnx"), QStringLiteral("hey_max.onnx") });
        r.wake.setEnabled(true);
        QTRY_COMPARE_WITH_TIMEOUT(r.wake.detector(), QStringLiteral("local"), 20000);
        QTRY_VERIFY(r.mic->open);
        QSignalSpy wokePhrase{ &r.wake, &WakeWord::wokePhrase };
        play(r.mic, silence(1.0) + fixturePcm("hey_buddy_tts.wav"));
        QTRY_COMPARE_WITH_TIMEOUT(r.woke.count(), 1, 10000);
        QCOMPARE(wokePhrase.count(), 1);
        const QString id = wokePhrase.first().at(0).toString();
        QVERIFY2(id == QStringLiteral("hey_buddy.onnx") || id == QStringLiteral("hey_max.onnx"), qPrintable(id));
        QCOMPARE(wokePhrase.first().at(1).toString(), wake::phraseDisplayName(id));
    }

};

QTEST_MAIN(TstWakeWord)
#include "tst_wakeword.moc"
