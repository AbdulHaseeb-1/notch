// Local wake-word detection: openWakeWord's three models, run through ONNX
// Runtime, must give the scores the reference implementation gives.
//
// Needs the ONNX Runtime library. Set NOCH_ORT_LIBRARY to its full path (or put
// onnxruntime next to the test); without it the tests skip instead of failing.
#include <QtTest>

#include <cmath>
#include <vector>

#include "OrtRuntime.h"
#include "WakeDetector.h"

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// 16 kHz mono 16-bit PCM out of a canonical 44-byte-header WAV file.
QByteArray wavPcm(const QString &path)
{
    const QByteArray wav = readFile(path);
    int dataAt = wav.indexOf("data");
    return dataAt < 0 ? QByteArray() : wav.mid(dataAt + 8);
}

// Scores of the reference implementation (openwakeword/utils.py streaming
// features + the same model), per 80 ms step, for the two fixture clips.
const std::vector<float> kHeyBuddyRef = {
    0.00049f, 0.00050f, 0.00051f, 0.00053f, 0.00058f, 0.00063f, 0.00076f, 0.00061f, 0.00057f, 0.00056f, 0.00059f,
    0.00086f, 0.00791f, 0.15353f, 0.63016f, 0.87591f, 0.96873f, 0.97851f, 0.88958f, 0.79345f, 0.23462f, 0.00081f
};

} // namespace

class TstWakeDetector : public QObject
{
    Q_OBJECT

    WakeDetector::Models models;
    bool ready = false;

private slots:
    void initTestCase()
    {
        QString why;
        if (!ortrt::load({ QCoreApplication::applicationDirPath() }, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        const QString dir = QStringLiteral(NOCH_WAKEWORD_DIR);
        models.melspectrogram = readFile(dir + "/melspectrogram.onnx");
        models.embedding = readFile(dir + "/embedding_model.onnx");
        models.phrases = { { QStringLiteral("Hey Buddy"), readFile(dir + "/hey_buddy.onnx") } };
        QVERIFY(!models.melspectrogram.isEmpty() && !models.embedding.isEmpty()
                && !models.phrases.first().model.isEmpty());
        ready = true;
    }

    void rejectsAModelThatIsNotAWakeWordModel()
    {
        WakeDetector d;
        WakeDetector::Models bad = models;
        bad.phrases = { { QStringLiteral("Hey Buddy"), models.embedding } };   // wrong input shape
        QString why;
        QVERIFY(!d.load(bad, &why));
        QVERIFY(!why.isEmpty());
        QVERIFY(!d.loaded());
        bad.phrases = { { QStringLiteral("Hey Buddy"), QByteArray("not a model") } };
        QVERIFY(!d.load(bad, &why));
    }

    void twoPhrasesShareTheSameFeatures()
    {
        // The same classifier under two names stands in for two different
        // phrases: both must be scored from the one mel/embedding stream,
        // step for step, so a caller can tell which phrase fired.
        WakeDetector::Models two = models;
        two.phrases.append({ QStringLiteral("Hey Max"), models.phrases.first().model });
        WakeDetector d;
        QVERIFY(d.load(two, nullptr));
        QCOMPARE(d.phraseCount(), 2);
        QCOMPARE(d.phraseNames(), QStringList({ QStringLiteral("Hey Buddy"), QStringLiteral("Hey Max") }));
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/hey_buddy_tts.wav"));
        QVERIFY(!pcm.isEmpty());
        std::vector<std::vector<float>> perPhrase;
        const float best = d.feed(pcm, nullptr, &perPhrase);
        QCOMPARE(perPhrase.size(), size_t(2));
        QCOMPARE(perPhrase[0].size(), kHeyBuddyRef.size());
        QCOMPARE(perPhrase[1].size(), kHeyBuddyRef.size());
        for (size_t i = 0; i < kHeyBuddyRef.size(); ++i) {
            QVERIFY2(std::fabs(perPhrase[0][i] - kHeyBuddyRef[i]) < 0.01f,
                     qPrintable(QStringLiteral("phrase 0, step %1").arg(i)));
            QVERIFY2(std::fabs(perPhrase[1][i] - perPhrase[0][i]) < 1e-4f,
                     qPrintable(QStringLiteral("phrase 1 must score the same steps, step %1").arg(i)));
        }
        QVERIFY(best > 0.9f);
        // And other speech wakes neither of them.
        WakeDetector d2;
        QVERIFY(d2.load(two, nullptr));
        std::vector<std::vector<float>> pp2;
        QVERIFY(d2.feed(wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/other_phrase_tts.wav")), nullptr, &pp2) < 0.1f);
        QCOMPARE(pp2.size(), size_t(2));
    }

    void aBadPhraseInTheSetIsRejectedAndNamed()
    {
        WakeDetector::Models bad = models;
        bad.phrases.append({ QStringLiteral("Broken"), models.embedding });   // not a classifier
        WakeDetector d;
        QString why;
        QVERIFY(!d.load(bad, &why));
        QVERIFY2(why.contains(QStringLiteral("Broken")), qPrintable(why));
        QVERIFY(!d.loaded());
    }

    void quietRoomScoresNothing()
    {
        WakeDetector d;
        QVERIFY(d.load(models, nullptr));
        std::vector<float> scores;
        d.feed(QByteArray(WakeDetector::kChunkSamples * 2 * 40, '\0'), &scores);
        QCOMPARE(int(scores.size()), 40);
        for (float s : scores)
            QVERIFY2(s < 0.05f, "silence must not look like the wake word");
    }

    void matchesTheReferenceImplementation()
    {
        WakeDetector d;
        QVERIFY(d.load(models, nullptr));
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/hey_buddy_tts.wav"));
        QVERIFY(!pcm.isEmpty());
        std::vector<float> scores;
        const float best = d.feed(pcm, &scores);
        QCOMPARE(scores.size(), kHeyBuddyRef.size());
        for (size_t i = 0; i < scores.size(); ++i)
            QVERIFY2(std::fabs(scores[i] - kHeyBuddyRef[i]) < 0.01f,
                     qPrintable(QStringLiteral("step %1: %2 vs reference %3").arg(i).arg(scores[i]).arg(kHeyBuddyRef[i])));
        QVERIFY(best > 0.9f);
    }

    void otherSpeechDoesNotWakeIt()
    {
        WakeDetector d;
        QVERIFY(d.load(models, nullptr));
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/other_phrase_tts.wav"));
        QVERIFY(!pcm.isEmpty());
        QVERIFY(d.feed(pcm) < 0.1f);
    }

    void chunkSizeDoesNotMatter()
    {
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/hey_buddy_tts.wav"));
        WakeDetector whole;
        WakeDetector pieces;
        QVERIFY(whole.load(models, nullptr));
        QVERIFY(pieces.load(models, nullptr));
        std::vector<float> a, b;
        whole.feed(pcm, &a);
        // Odd sizes, as a microphone delivers them (and one odd byte count's worth of samples).
        const int sizes[] = { 320, 946, 2, 4096, 100, 1500 };
        int at = 0, n = 0;
        while (at < pcm.size()) {
            const int len = qMin<int>(sizes[n++ % 6] & ~1, pcm.size() - at);
            pieces.feed(pcm.mid(at, len), &b);
            at += len;
        }
        QCOMPARE(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i)
            QVERIFY(std::fabs(a[i] - b[i]) < 1e-4f);
    }

    // The detector runs all day beside the user's other work: one 80 ms step must
    // cost a small fraction of 80 ms. The bound is generous (a debug build on a
    // busy CI machine) but would catch an accidental per-step reload or copy.
    void aStepCostsAFractionOfRealTime()
    {
        WakeDetector d;
        QVERIFY(d.load(models, nullptr));
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/hey_buddy_tts.wav"));
        constexpr int kRounds = 8;
        QElapsedTimer t;
        t.start();
        int steps = 0;
        for (int i = 0; i < kRounds; ++i) {
            std::vector<float> scores;
            d.feed(pcm, &scores);
            steps += int(scores.size());
        }
        const double perStepMs = double(t.nsecsElapsed()) / 1e6 / steps;
        qInfo().noquote() << QStringLiteral("detector: %1 ms per 80 ms step (%2 steps)").arg(perStepMs, 0, 'f', 2).arg(steps);
        QVERIFY2(perStepMs < 20.0, qPrintable(QStringLiteral("%1 ms per step").arg(perStepMs)));
    }

    void resetForgetsWhatWasHeard()
    {
        WakeDetector d;
        QVERIFY(d.load(models, nullptr));
        const QByteArray pcm = wavPcm(QStringLiteral(NOCH_TEST_DATA_DIR "/hey_buddy_tts.wav"));
        QVERIFY(d.feed(pcm) > 0.9f);
        d.reset();
        QVERIFY(d.feed(QByteArray(WakeDetector::kChunkSamples * 2 * 5, '\0')) < 0.05f);
        QVERIFY(d.feed(pcm) > 0.9f);       // and it still hears it afterwards
    }
};

QTEST_GUILESS_MAIN(TstWakeDetector)
#include "tst_wakedetector.moc"
