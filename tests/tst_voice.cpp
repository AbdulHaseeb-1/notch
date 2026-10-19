// Push-to-talk voice: audio conversion, the hold-to-talk key, and the
// recording -> transcription flow (fake microphone, fake speech-to-text server).
#include <QtTest>
#include <QAudioFormat>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtMath>

#include "../src/PushToTalkKey.h"
#include "../src/VoiceAudio.h"
#include "../src/VoiceInput.h"
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

    // A VoiceInput that is on, set up for the fake server and given the fake microphone.
    struct Rig {
        FakeStt stt;
        VoiceInput voice;
        FakeCapture *mic = new FakeCapture;
        Rig() {
            voice.setEnabled(true);
            voice.setApiBase(stt.base());
            voice.setApiKey("sk-test-key-0123456789");
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

    // ---- the address ----------------------------------------------------------------------

    void buildsTheTranscriptionUrl_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("url");
        QTest::newRow("host") << "https://api.openai.com" << "https://api.openai.com/v1/audio/transcriptions";
        QTest::newRow("trailing slash") << "https://api.openai.com/" << "https://api.openai.com/v1/audio/transcriptions";
        QTest::newRow("with v1") << "https://api.groq.com/openai/v1" << "https://api.groq.com/openai/v1/audio/transcriptions";
        QTest::newRow("already full") << "https://x.example/v1/audio/transcriptions" << "https://x.example/v1/audio/transcriptions";
        QTest::newRow("local http") << "http://localhost:8080" << "http://localhost:8080/v1/audio/transcriptions";
        QTest::newRow("loopback") << "http://127.0.0.1:9000/v1" << "http://127.0.0.1:9000/v1/audio/transcriptions";
    }
    void buildsTheTranscriptionUrl() {
        QFETCH(QString, base);
        QFETCH(QString, url);
        VoiceInput v;
        v.setApiBase(base);
        QCOMPARE(v.transcriptionUrl(), url);
        QVERIFY(v.addressProblem().isEmpty());
    }

    void yourVoiceOnlyTravelsOverHttpsOrToThisComputer_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<bool>("ok");
        QTest::newRow("https") << "https://stt.example.com" << true;
        QTest::newRow("local http") << "http://localhost:8000" << true;
        QTest::newRow("loopback http") << "http://127.0.0.1" << true;
        QTest::newRow("remote http") << "http://stt.example.com" << false;
        QTest::newRow("lan http") << "http://192.168.1.20:8000" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("junk") << "not an address" << false;
        QTest::newRow("file") << "file:///etc/passwd" << false;
        QTest::newRow("ftp") << "ftp://stt.example.com" << false;
    }
    void yourVoiceOnlyTravelsOverHttpsOrToThisComputer() {
        QFETCH(QString, base);
        QFETCH(bool, ok);
        VoiceInput v;
        v.setApiBase(base);
        QCOMPARE(v.addressProblem().isEmpty(), ok);
        QCOMPARE(!v.transcriptionUrl().isEmpty(), ok);
    }

    void needsAKeyUnlessItIsLocal() {
        VoiceInput v;
        v.setApiBase("https://api.openai.com");
        QVERIFY(!v.configured());
        v.setApiKey("sk-x");
        QVERIFY(v.configured());
        v.setApiKey("");
        v.setApiBase("http://localhost:8080");
        QVERIFY(v.configured());          // a local whisper server needs no key
        v.setModel("  ");
        QVERIFY(!v.configured());
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

    void refusesToRecordWithNowhereSafeToSendIt() {
        Rig r;
        r.voice.setApiBase("http://stt.example.com");
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QVERIFY(!r.voice.beginListening());
        QVERIFY(failed.first().first().toString().contains("https"));
        QCOMPARE(r.mic->starts, 0);
        r.voice.setApiBase("https://stt.example.com");
        r.voice.setApiKey("");
        QVERIFY(!r.voice.beginListening());
        QCOMPARE(r.mic->starts, 0);
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
        QSignalSpy states(&r.voice, &VoiceInput::stateChanged);
        QVERIFY(r.voice.beginListening());
        QCOMPARE(r.voice.state(), QStringLiteral("listening"));
        QVERIFY(r.voice.listening() && r.voice.busy());
        for (int i = 0; i < 10; ++i)
            r.mic->feed(tone(0.1));
        QVERIFY(r.voice.level() > 0.2);                    // the meter follows the voice
        QVERIFY(qAbs(r.voice.seconds() - 1.0) < 0.02);
        r.voice.endListening();
        QCOMPARE(r.voice.state(), QStringLiteral("transcribing"));
        QCOMPARE(r.mic->stops, 1);                         // the microphone is closed before the upload
        QCOMPARE(r.voice.level(), 0.0);
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QCOMPARE(words.first().first().toString(), QStringLiteral("hello world"));
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QVERIFY(r.voice.error().isEmpty());
        QCOMPARE(r.stt.requests, 1);
        Q_UNUSED(states)
    }

    void theRequestIsAnAuthenticatedMultipartUploadOfAWavFile() {
        Rig r;
        r.voice.setLanguage("de");
        r.voice.setModel("gpt-4o-mini-transcribe");
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        QVERIFY(r.voice.beginListening());
        r.mic->feed(silence(0.5));
        r.mic->feed(tone(1.0));
        r.mic->feed(silence(0.5));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QVERIFY(r.stt.requestLine.startsWith("POST /v1/audio/transcriptions"));
        QVERIFY(r.stt.headers.contains("authorization: bearer sk-test-key-0123456789"));
        QVERIFY(r.stt.headers.contains("content-type: multipart/form-data; boundary="));
        const QByteArray b = r.stt.body;
        QVERIFY(b.contains("name=\"model\"\r\n\r\ngpt-4o-mini-transcribe"));
        QVERIFY(b.contains("name=\"response_format\"\r\n\r\njson"));
        QVERIFY(b.contains("name=\"language\"\r\n\r\nde"));
        QVERIFY(b.contains("name=\"file\"; filename=\"speech.wav\""));
        QVERIFY(b.contains("audio/wav"));
        const int riff = b.indexOf("RIFF");
        QVERIFY(riff > 0);
        QCOMPARE(b.mid(riff + 8, 4), QByteArray("WAVE"));
        // The silence on both ends was trimmed before it was sent (2 s recorded, ~1.3 s sent).
        const int dataBytes = int(le32(b, riff + 40));
        QVERIFY2(dataBytes < 2 * voice::kBytesPerSecond * 0.75 && dataBytes > voice::kBytesPerSecond, qPrintable(QString::number(dataBytes)));
    }

    void noLanguageMeansTheServerDetectsIt() {
        Rig r;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QVERIFY(!r.stt.body.contains("name=\"language\""));
    }

    void aLocalServerNeedsNoKey() {
        Rig r;
        r.voice.setApiKey("");
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        QVERIFY(r.voice.beginListening());
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QVERIFY(!r.stt.headers.contains("authorization"));
    }

    void tooShortAClipIsNotSent() {
        Rig r;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(tone(0.1));
        r.voice.endListening();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("too short"));
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QCOMPARE(r.stt.requests, 0);
    }

    void aSilentClipIsNotSentAnywhere() {
        Rig r;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        r.voice.beginListening();
        r.mic->feed(silence(2.0));        // a muted microphone
        r.voice.endListening();
        QCOMPARE(failed.size(), 1);
        QVERIFY(failed.first().first().toString().contains("did not hear"));
        QCOMPARE(r.stt.requests, 0);
    }

    void serviceErrorsAreExplained_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("contains");
        QTest::newRow("bad key") << 401 << QByteArray("{\"error\":{\"message\":\"Incorrect API key provided: sk-...\"}}") << "key was refused";
        QTest::newRow("forbidden") << 403 << QByteArray("{}") << "key was refused";
        QTest::newRow("quota") << 429 << QByteArray("{\"error\":{\"message\":\"rate limit\"}}") << "busy or out of quota";
        QTest::newRow("their message") << 400 << QByteArray("{\"error\":{\"message\":\"Invalid file format.\"}}") << "Invalid file format.";
        QTest::newRow("string error") << 500 << QByteArray("{\"error\":\"model is loading\"}") << "model is loading";
        QTest::newRow("no body") << 502 << QByteArray("") << "HTTP 502";
    }
    void serviceErrorsAreExplained() {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, contains);
        Rig r;
        r.stt.status = status;
        r.stt.reply = body;
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY2(failed.first().first().toString().contains(contains), qPrintable(failed.first().first().toString()));
        QCOMPARE(words.size(), 0);
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QVERIFY(!r.voice.error().isEmpty());
        // And the next try works again once the service does.
        r.stt.status = 200;
        r.stt.reply = "{\"text\":\"fine now\"}";
        QVERIFY(r.voice.beginListening());
    }

    void anUnreachableServerIsSaidSo() {
        VoiceInput v;
        v.setEnabled(true);
        v.setApiBase("http://127.0.0.1:1");     // nothing listens there
        auto *mic = new FakeCapture;
        v.setCapture(std::unique_ptr<AudioCapture>(mic));
        QSignalSpy failed(&v, &VoiceInput::failed);
        v.beginListening();
        mic->feed(tone(1.0));
        v.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 8000);
        QVERIFY(failed.first().first().toString().contains("reach"));
    }

    void anAnswerWithNoWordsIsAFailure() {
        Rig r;
        r.stt.reply = "{\"text\":\"   \"}";
        QSignalSpy failed(&r.voice, &VoiceInput::failed);
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(words.size(), 0);
    }

    void serversThatAnswerWithPlainTextWork() {
        Rig r;
        r.stt.reply = "just the words\n";
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(words.size(), 1, 5000);
        QCOMPARE(words.first().first().toString(), QStringLiteral("just the words"));
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
        QCOMPARE(r.stt.requests, 0);
        QCOMPARE(words.size() + failed.size(), 0);
    }

    void cancelWhileTranscribingDropsTheLateAnswer() {
        Rig r;
        r.stt.delayMs = 400;
        QSignalSpy words(&r.voice, &VoiceInput::transcript);
        r.voice.beginListening();
        r.mic->feed(tone(1.0));
        r.voice.endListening();
        QTRY_COMPARE_WITH_TIMEOUT(r.stt.requests, 1, 3000);
        r.voice.cancel();
        QCOMPARE(r.voice.state(), QStringLiteral("idle"));
        QTest::qWait(700);
        QCOMPARE(words.size(), 0);          // the words that arrive afterwards are not used
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
        QCOMPARE(r.stt.requests, 0);
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
