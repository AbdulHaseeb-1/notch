// Push-to-talk voice: audio conversion, the hold-to-talk key, and the
// recording -> transcription flow (fake microphone, scripted speech-to-text engine).
#include <QtTest>
#include <QAudioFormat>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtMath>

#include "OrtRuntime.h"
#include "PushToTalkKey.h"
#include "VoiceAudio.h"
#include "VoiceInput.h"
#include "SpeechFakes.h"

namespace {

QAudioFormat format(int rate, int channels, QAudioFormat::SampleFormat f)
{
    QAudioFormat fmt;
    fmt.setSampleRate(rate);
    fmt.setChannelCount(channels);
    fmt.setSampleFormat(f);
    return fmt;
}

// Counts rising zero crossings: the frequency of a clean tone.
double frequencyOf(const QByteArray &pcm16, int rate)
{
    const auto *s = reinterpret_cast<const qint16 *>(pcm16.constData());
    const int n = int(pcm16.size() / 2);
    int crossings = 0;
    for (int i = 1; i < n; ++i)
        crossings += s[i - 1] < 0 && s[i] >= 0;
    return crossings * double(rate) / n;
}

quint32 le32(const QByteArray &b, int at) { return qFromLittleEndian<quint32>(b.constData() + at); }
quint16 le16(const QByteArray &b, int at) { return qFromLittleEndian<quint16>(b.constData() + at); }

} // namespace

class TstVoice : public QObject
{
    Q_OBJECT

    // A VoiceInput that is on, given the scripted engine and the fake microphone.
    struct Rig {
        VoiceInput voice;
        FakeTranscriber &stt;
        FakeCapture *mic = new FakeCapture;
        static FakeTranscriber *install(VoiceInput &v) {
            auto *t = new FakeTranscriber;
            v.setTranscriber(std::unique_ptr<Transcriber>(t));
            return t;
        }
        Rig() : stt(*install(voice)) {
            voice.setEnabled(true);
            voice.setCapture(std::unique_ptr<AudioCapture>(mic));
        }
    };

private slots:
    // ---- audio conversion --------------------------------------------------------------

    void sixteenKilohertzMonoPassesThroughUntouched() {
        const QByteArray in = tone(0.2);
        QCOMPARE(voice::toMono16k(in, format(16000, 1, QAudioFormat::Int16)), in);
    }

    void stereoIsAveraged() {
        QByteArray in;
        for (qint16 l : { qint16(1000), qint16(-2000) }) {
            const qint16 r = qint16(3000);
            in.append(reinterpret_cast<const char *>(&l), 2);
            in.append(reinterpret_cast<const char *>(&r), 2);
        }
        const QByteArray out = voice::toMono16k(in, format(16000, 2, QAudioFormat::Int16));
        QCOMPARE(out.size(), 4);
        const auto *s = reinterpret_cast<const qint16 *>(out.constData());
        QCOMPARE(int(s[0]), 2000);
        QCOMPARE(int(s[1]), 500);
    }

    void everySampleFormatIsRead() {
        // The same half-scale sample in each format comes out as the same 16-bit value.
        const qint16 want = 16384;
        {
            const quint8 v = 192;   // unsigned 8-bit: 128 is zero
            QByteArray in(reinterpret_cast<const char *>(&v), 1);
            const QByteArray out = voice::toMono16k(in, format(16000, 1, QAudioFormat::UInt8));
            QVERIFY(qAbs(int(*reinterpret_cast<const qint16 *>(out.constData())) - want) <= 1);
        }
        {
            const qint32 v = 1073741824;
            QByteArray in(reinterpret_cast<const char *>(&v), 4);
            const QByteArray out = voice::toMono16k(in, format(16000, 1, QAudioFormat::Int32));
            QVERIFY(qAbs(int(*reinterpret_cast<const qint16 *>(out.constData())) - want) <= 1);
        }
        {
            const float v = 0.5f;
            QByteArray in(reinterpret_cast<const char *>(&v), 4);
            const QByteArray out = voice::toMono16k(in, format(16000, 1, QAudioFormat::Float));
            QVERIFY(qAbs(int(*reinterpret_cast<const qint16 *>(out.constData())) - want) <= 1);
        }
    }

    void floatSamplesOutOfRangeOrBrokenDoNotWrap() {
        const float vals[] = { 3.0f, -3.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
        QByteArray in(reinterpret_cast<const char *>(vals), sizeof(vals));
        const QByteArray out = voice::toMono16k(in, format(16000, 1, QAudioFormat::Float));
        const auto *s = reinterpret_cast<const qint16 *>(out.constData());
        QCOMPARE(int(s[0]), 32767);
        QCOMPARE(int(s[1]), -32767);   // clamped to full scale, not wrapped round to the other sign
        QCOMPARE(int(s[2]), 0);
        QCOMPARE(int(s[3]), 0);
    }

    void downsamplingKeepsTheTone_data() {
        QTest::addColumn<int>("rate");
        QTest::addColumn<double>("expectedSeconds");
        QTest::newRow("48 kHz") << 48000 << 1.0;
        QTest::newRow("44.1 kHz") << 44100 << 1.0;
        QTest::newRow("32 kHz") << 32000 << 1.0;
    }
    void downsamplingKeepsTheTone() {
        QFETCH(int, rate);
        const QByteArray in = tone(1.0, 400, 0.4, rate);
        const QByteArray out = voice::toMono16k(in, format(rate, 1, QAudioFormat::Int16));
        QVERIFY(qAbs(voice::seconds(out) - 1.0) < 0.01);
        QVERIFY2(qAbs(frequencyOf(out, voice::kSampleRate) - 400) < 12, qPrintable(QString::number(frequencyOf(out, voice::kSampleRate))));
        QVERIFY(voice::level(out) > 0.3);   // and it did not get quieter on the way
    }

    void upsamplingDoublesTheLength() {
        const QByteArray in = tone(0.5, 200, 0.3, 8000);
        const QByteArray out = voice::toMono16k(in, format(8000, 1, QAudioFormat::Int16));
        QVERIFY(qAbs(voice::seconds(out) - 0.5) < 0.01);
        QVERIFY(qAbs(frequencyOf(out, voice::kSampleRate) - 200) < 10);
    }

    void unreadableInputGivesNothing() {
        QVERIFY(voice::toMono16k(QByteArray(), format(16000, 1, QAudioFormat::Int16)).isEmpty());
        QVERIFY(voice::toMono16k(tone(0.1), format(16000, 1, QAudioFormat::Unknown)).isEmpty());
        QVERIFY(voice::toMono16k(tone(0.1), format(0, 1, QAudioFormat::Int16)).isEmpty());
        QVERIFY(voice::toMono16k(tone(0.1), format(16000, 0, QAudioFormat::Int16)).isEmpty());
        // Less than one whole frame: nothing, and no crash.
        QVERIFY(voice::toMono16k(QByteArray(3, 'x'), format(16000, 2, QAudioFormat::Int16)).isEmpty());
    }

    void aWavFileHasARealHeader() {
        const QByteArray pcm = tone(0.1);
        const QByteArray wav = voice::encodeWav(pcm);
        QCOMPARE(wav.size(), 44 + pcm.size());
        QCOMPARE(wav.left(4), QByteArray("RIFF"));
        QCOMPARE(int(le32(wav, 4)), 36 + pcm.size());
        QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(wav.mid(12, 4), QByteArray("fmt "));
        QCOMPARE(int(le32(wav, 16)), 16);
        QCOMPARE(int(le16(wav, 20)), 1);                       // PCM
        QCOMPARE(int(le16(wav, 22)), 1);                       // mono
        QCOMPARE(int(le32(wav, 24)), 16000);
        QCOMPARE(int(le32(wav, 28)), 32000);                   // byte rate
        QCOMPARE(int(le16(wav, 32)), 2);                       // block align
        QCOMPARE(int(le16(wav, 34)), 16);                      // bits
        QCOMPARE(wav.mid(36, 4), QByteArray("data"));
        QCOMPARE(int(le32(wav, 40)), pcm.size());
        QCOMPARE(wav.mid(44), pcm);
        QCOMPARE(voice::encodeWav(QByteArray()).size(), 44);   // an empty clip is still a valid file
    }

    void levelsAndSilence() {
        QCOMPARE(voice::level(QByteArray()), 0.0);
        QCOMPARE(voice::level(silence(0.1)), 0.0);
        QCOMPARE(voice::seconds(silence(1.0)), 1.0);
        const double speechLike = voice::level(tone(0.2, 300, 0.08));
        QVERIFY(speechLike > 0.15 && speechLike < 0.6);
        QVERIFY(voice::level(tone(0.2, 300, 1.0)) > 0.9);
        QVERIFY(voice::peak(tone(0.2, 300, 0.5)) > 0.45);
        QVERIFY(voice::isSilent(QByteArray()));
        QVERIFY(voice::isSilent(silence(1)));
        QVERIFY(voice::isSilent(tone(1, 300, 0.004)));          // hiss on an unplugged mic
        QVERIFY(!voice::isSilent(tone(1, 300, 0.05)));
    }

    void silenceIsTrimmedFromBothEndsWithABitLeftOver() {
        const QByteArray clip = silence(1.0) + tone(0.5) + silence(1.0);
        const QByteArray trimmed = voice::trimSilence(clip);
        const double s = voice::seconds(trimmed);
        QVERIFY2(s > 0.5 + 0.2 && s < 0.5 + 0.45, qPrintable(QString::number(s)));   // the words and ~0.15 s each side
        QVERIFY(voice::peak(trimmed.left(32)) < 0.01);                                 // starts in the quiet lead-in
        QVERIFY(!voice::isSilent(trimmed));
        // Nothing to cut: all silence, no silence, and clips too short to judge stay as they are.
        QCOMPARE(voice::trimSilence(silence(1.0)), silence(1.0));
        const QByteArray wordsOnly = tone(0.6);
        QVERIFY(qAbs(voice::seconds(voice::trimSilence(wordsOnly)) - 0.6) < 0.03);
        QCOMPARE(voice::trimSilence(QByteArray(10, 'x')), QByteArray(10, 'x'));
    }

    // ---- the hold-to-talk key -----------------------------------------------------------

    void holdingTheKeyFiresPressedThenReleased() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        int down = 8;   // the key stays down for 8 polls
        key.setKeyProbe([&down] { return down-- > 0; });
        QSignalSpy pressed(&key, &PushToTalkKey::pressed);
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.press();
        QCOMPARE(pressed.size(), 1);
        QVERIFY(key.held());
        QCOMPARE(released.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(released.size(), 1, 2000);
        QVERIFY(!key.held());
        QCOMPARE(pressed.size(), 1);
    }

    void aFlickerDoesNotEndTheSentence() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        int poll = 0;
        // down, down, UP once, down, down, then really up
        key.setKeyProbe([&poll] { const int n = poll++; return n < 2 || (n >= 3 && n < 5); });
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.press();
        QTRY_COMPARE_WITH_TIMEOUT(released.size(), 1, 2000);
        QVERIFY2(poll >= 7, qPrintable(QString::number(poll)));   // it waited past the one missed poll
    }

    void aMissedKeyUpStillEndsAtTheMaximum() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        key.setMaxHoldMs(120);
        key.setKeyProbe([] { return true; });   // the key-up never comes
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.press();
        QTRY_COMPARE_WITH_TIMEOUT(released.size(), 1, 2000);
        QVERIFY(!key.held());
    }

    void withoutAWayToSeeTheKeyAPressIsATap() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.press();
        QTRY_COMPARE_WITH_TIMEOUT(released.size(), 1, 2000);
    }

    void aDisabledKeyDoesNothing() {
        PushToTalkKey key;
        key.setKeyProbe([] { return true; });
        QSignalSpy pressed(&key, &PushToTalkKey::pressed);
        key.press();
        QCOMPARE(pressed.size(), 0);
        QVERIFY(!key.held());
    }

    void aSecondPressWhileHeldIsIgnored() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        key.setKeyProbe([] { return true; });
        QSignalSpy pressed(&key, &PushToTalkKey::pressed);
        key.press();
        key.press();
        QCOMPARE(pressed.size(), 1);
        key.release();
    }

    void turningTheKeyOffWhileHeldLetsGo() {
        PushToTalkKey key;
        key.setEnabled(true);
        key.setPollIntervalMs(5);
        key.setKeyProbe([] { return true; });
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.press();
        key.setEnabled(false);
        QCOMPARE(released.size(), 1);
        QVERIFY(!key.held());
    }

    void releaseWithoutPressIsHarmless() {
        PushToTalkKey key;
        key.setEnabled(true);
        QSignalSpy released(&key, &PushToTalkKey::released);
        key.release();
        QCOMPARE(released.size(), 0);
    }

    // ---- recording and transcribing ---------------------------------------------------------------

    void isOffUntilTheUserTurnsItOn() {
        Rig r;
        r.voice.setEnabled(false);
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QVERIFY(!r.voice.beginListening());
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("Settings"));
        QCOMPARE(r.mic->starts, 0);                        // the microphone was never touched
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
    }

    void refusesToRecordWhenTheModelIsMissing() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.problemText = QStringLiteral("The speech model is missing."); });
        QVERIFY(!r.voice.configured());
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QVERIFY(!r.voice.beginListening());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("The speech model is missing."));
        QCOMPARE(r.mic->starts, 0);                        // nothing is recorded that cannot be used
        r.stt.set([](FakeTranscriber &t) { t.problemText.clear(); });
        QVERIFY(r.voice.configured());
        QVERIFY(r.voice.beginListening());
        r.voice.cancel();
    }

    void theModelLoadsInTheBackgroundWhenVoiceIsTurnedOn() {
        Rig r;
        QTRY_VERIFY_WITH_TIMEOUT(r.voice.ready(), 3000);
        QVERIFY(r.voice.configured());
        QCOMPARE(r.voice.engine(), QStringLiteral("Fake"));
    }

    void aModelThatWillNotLoadIsReportedAndStopsRecording() {
        VoiceInput v;
        auto *t = new FakeTranscriber;
        t->prepareError = QStringLiteral("The speech model did not load.");
        v.setTranscriber(std::unique_ptr<Transcriber>(t));
        auto *mic = new FakeCapture;
        v.setCapture(std::unique_ptr<AudioCapture>(mic));
        v.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(!v.configured(), 3000);
        QCOMPARE(v.problem(), QStringLiteral("The speech model did not load."));
        QVERIFY(!v.ready());
        QSignalSpy failed(&v, &VoiceInput::failed);
        QVERIFY(!v.beginListening());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(mic->starts, 0);
    }

    void aMissingMicrophoneIsSaidClearly() {
        Rig r;
        r.mic->startOk = false;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QVERIFY(!r.voice.beginListening());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("No microphone was found."));
        QCOMPARE(r.voice.error(), QStringLiteral("No microphone was found."));
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        r.voice.clearError();
        QVERIFY(r.voice.error().isEmpty());
    }

    void holdSpeakReleaseGivesTheWords() {
        Rig r;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        QVERIFY(r.voice.beginListening());
        QCOMPARE(r.voice.state(), QStringLiteral("listening"));
        QVERIFY(r.voice.listening() && r.voice.busy());
        for (int i = 0; i < 10; ++i)
            r.mic->feed(tone(0.1));
        QVERIFY(r.voice.level() > 0.2);                    // the meter follows the voice
        QVERIFY(qAbs(r.voice.seconds() - 1.0) < 0.02);
        r.voice.endListening();
        QCOMPARE(r.voice.state(), QStringLiteral("transcribing"));
        QCOMPARE(r.mic->stops, 1);                         // the microphone is closed first
        QCOMPARE(r.voice.level(), 0.0);
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QCOMPARE(words.first().first().toString(), QStringLiteral("hello world"));
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QVERIFY(r.voice.error().isEmpty());
        QCOMPARE(r.stt.requests.load(), 1);
    }

    void onlyTheSpokenPartIsTranscribed() {
        Rig r;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        QVERIFY(r.voice.beginListening());
        r.mic->feed(silence(0.5));
        r.mic->feed(tone(1.0));
        r.mic->feed(silence(0.5));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        // The silence on both ends was trimmed (2 s recorded, ~1.3 s handed over), as plain PCM.
        const int bytes = r.stt.pcm().size();
        QVERIFY2(bytes < 2 * voice::kBytesPerSecond * 0.75 && bytes > voice::kBytesPerSecond, qPrintable(QString::number(bytes)));
    }

    void tooShortAClipIsNotTranscribed() {
        Rig r;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(tone(0.1));
        r.voice.endListening();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("too short"));
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QCOMPARE(r.stt.requests.load(), 0);
    }

    void aSilentClipIsNotTranscribed() {
        Rig r;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(silence(2.0));        // a muted microphone
        r.voice.endListening();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("did not hear"));
        QCOMPARE(r.stt.requests.load(), 0);
    }

    void aFailingEngineIsExplainedAndTheNextTryWorks() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.errorText = QStringLiteral("Moonshine stopped while writing."); });
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Moonshine stopped while writing."));
        QCOMPARE(words.size(), 0);
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QVERIFY(!r.voice.error().isEmpty());
        r.stt.set([](FakeTranscriber &t) { t.errorText.clear(); });
        QVERIFY(r.voice.beginListening());
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QVERIFY(r.voice.error().isEmpty());            // success clears the old error
    }

    void anAnswerWithNoWordsIsAFailure() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.reply = QStringLiteral("   "); });
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(failed.first().first().toString().contains("make out any words"));
        QCOMPARE(words.size(), 0);
    }

    void whitespaceInTheAnswerIsTidied() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.reply = QStringLiteral("  open   notepad \n"); });
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QCOMPARE(words.first().first().toString(), QStringLiteral("open notepad"));
    }

    void cancelWhileListeningDiscardsEverything() {
        Rig r;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.cancel();
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QCOMPARE(r.mic->stops, 1);
        QCOMPARE(r.voice.seconds(), 0.0);
        r.voice.endListening();             // the key-up that follows is a no-op
        QTest::qWait(150);
        QCOMPARE(r.stt.requests.load(), 0);
        QCOMPARE(words.size() + failed.size(), 0);
    }

    void cancelWhileTranscribingDropsTheLateAnswer() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.delayMs = 400; });
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(r.stt.requests.load(), 1, 3000);
        r.voice.cancel();
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QTest::qWait(800);
        QCOMPARE(words.size(), 0);          // the words that arrive afterwards are not used
    }

    void anAbortedRequestIsNeverAnswered() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.delayMs = 150; t.replies = { QStringLiteral("first"), QStringLiteral("second") }; });
        QObject context;
        QStringList got;
        const quint64 a = r.voice.transcribe(tone(1.0), &context, [&](const VoiceInput::Transcription &t) { got << t.text; });
        const quint64 b = r.voice.transcribe(tone(1.0), &context, [&](const VoiceInput::Transcription &t) { got << t.text; });
        QVERIFY(a != b && a != 0 && b != 0);
        r.voice.abort(a);
        QTRY_COMPARE_WITH_TIMEOUT(got.size(), 1, 5000);
        QCOMPARE(got.first(), QStringLiteral("second"));   // requests run in order; the aborted one is just dropped
        QTest::qWait(200);
        QCOMPARE(got.size(), 1);
    }

    void aDestroyedContextIsNeverCalled() {
        Rig r;
        r.stt.set([](FakeTranscriber &t) { t.delayMs = 100; });
        int calls = 0;
        {
            QObject context;
            r.voice.transcribe(tone(1.0), &context, [&](const VoiceInput::Transcription &) { ++calls; });
        }
        QTRY_COMPARE_WITH_TIMEOUT(r.stt.requests.load(), 1, 3000);
        QTest::qWait(300);
        QCOMPARE(calls, 0);
    }

    void turningVoiceOffClosesTheMicrophoneAtOnce() {
        Rig r;
        r.voice.beginListening();
        QVERIFY(r.mic->open);
        r.voice.setEnabled(false);
        QVERIFY(!r.mic->open);
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
    }

    void recordingStopsByItselfAtTheMaximum() {
        Rig r;
        r.voice.setMaxSeconds(1);
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.5));
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 6000);   // ended without a key-up
        QVERIFY(!r.mic->open);
    }

    void aMicrophoneThatDiesMidSentenceEndsCleanly() {
        Rig r;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(tone(0.5));
        r.mic->breakDown("The microphone was unplugged.");
        QCOMPARE(failed.size(), 1);
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QCOMPARE(r.stt.requests.load(), 0);
        QVERIFY(!r.mic->open);
    }

    void startingTwiceDoesNotRestartTheRecording() {
        Rig r;
        QVERIFY(r.voice.beginListening());
        r.mic->feed(tone(0.5));
        QVERIFY(!r.voice.beginListening());
        QCOMPARE(r.mic->starts, 1);
        QVERIFY(r.voice.seconds() > 0.45);   // what was said so far is still there
        r.voice.cancel();
    }

    void audioThatArrivesWhenNotListeningIsIgnored() {
        Rig r;
        r.mic->feed(tone(1.0));
        QCOMPARE(r.voice.seconds(), 0.0);
        QCOMPARE(r.voice.level(), 0.0);
    }

    void itCanBeUsedAgainAndAgain() {
        Rig r;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(r.voice.beginListening());
            r.mic->feed(tone(1.0));
            r.voice.endListening();
            QTRY_COMPARE_WITH_TIMEOUT(words.size(), i + 1, 5000);
        }
        QCOMPARE(r.mic->starts, 3);
        QCOMPARE(r.mic->stops, 3);
    }

    // The real engine, end to end: speech in, words out, nothing leaves the process.
    // Needs ONNX Runtime (NOCH_ORT_LIBRARY) and the model (NOCH_MOONSHINE_DIR).
    void realMoonshineTurnsRealSpeechIntoWords() {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        if (qEnvironmentVariable("NOCH_MOONSHINE_DIR").isEmpty())
            QSKIP("Set NOCH_MOONSHINE_DIR to a folder with encoder.onnx and decoder.onnx to run this.");
        VoiceInput v;                                   // the default engine
        auto *mic = new FakeCapture;
        v.setCapture(std::unique_ptr<AudioCapture>(mic));
        v.setEnabled(true);
        QVERIFY2(v.configured(), qPrintable(v.problem()));
        QTRY_VERIFY_WITH_TIMEOUT(v.ready(), 20000);
        QSignalSpy words(&v, &VoiceInput::transcript);
        QSignalSpy failed(&v, &VoiceInput::failed);
        QFile f(QStringLiteral(NOCH_TEST_DATA_DIR "/screenshot_tts.wav"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray wav = f.readAll();
        QVERIFY(v.beginListening());
        mic->feed(wav.mid(wav.indexOf("data") + 8));
        v.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size() + failed.size(), 1, 20000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(words.first().first().toString(), QStringLiteral("Take a screenshot and show me my screen."));
    }

    void theMeterRisesFastAndFallsSlowly() {
        Rig r;
        r.voice.beginListening();
        r.mic->feed(tone(0.05, 300, 0.5));
        const double loud = r.voice.level();
        r.mic->feed(silence(0.05));
        const double after = r.voice.level();
        QVERIFY(loud > 0.4);
        QVERIFY(after < loud && after > loud * 0.5);   // it decays, it does not drop to zero
        r.voice.cancel();
    }
};

QTEST_MAIN(TstVoice)
#include "tst_voice.moc"
