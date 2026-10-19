// Moonshine Tiny speech to text, run through ONNX Runtime.
//
// Needs the ONNX Runtime library (NOCH_ORT_LIBRARY, a full path) and the model
// files (NOCH_MOONSHINE_DIR, a folder with encoder.onnx and decoder.onnx); the
// tests skip without them.
#include <QtTest>
#include <QElapsedTimer>

#include "Moonshine.h"
#include "OrtRuntime.h"

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray wavPcm(const QString &name)
{
    const QByteArray wav = readFile(QStringLiteral(NOCH_TEST_DATA_DIR "/") + name);
    const int at = wav.indexOf("data");
    return at < 0 ? QByteArray() : wav.mid(at + 8);
}

} // namespace

class TstMoonshine : public QObject
{
    Q_OBJECT

    Moonshine m;

private slots:
    void initTestCase()
    {
        QString why;
        if (!ortrt::load({}, &why))
            QSKIP(qPrintable("ONNX Runtime is not available: " + why));
        const QString dir = qEnvironmentVariable("NOCH_MOONSHINE_DIR");
        if (dir.isEmpty() || !QFileInfo::exists(dir + "/encoder.onnx"))
            QSKIP("Set NOCH_MOONSHINE_DIR to a folder with encoder.onnx and decoder.onnx to run these.");
        QVERIFY2(m.load(readFile(dir + "/encoder.onnx"), readFile(dir + "/decoder.onnx"),
                        readFile(QStringLiteral(NOCH_MOONSHINE_VOCAB)), &why), qPrintable(why));
    }

    void readsSpeech_data()
    {
        QTest::addColumn<QString>("wav");
        QTest::addColumn<QString>("expected");
        // The reference implementation (Python, the same models) wrote these.
        QTest::newRow("screenshot") << "screenshot_tts.wav" << "Take a screenshot and show me my screen.";
        QTest::newRow("weather") << "weather_tts.wav" << "What is the weather like today?";
    }
    void readsSpeech()
    {
        QFETCH(QString, wav);
        QFETCH(QString, expected);
        QString why;
        const QByteArray pcm = wavPcm(wav);
        QElapsedTimer clock;
        clock.start();
        const QString text = m.transcribe(pcm, &why);
        const double ms = clock.nsecsElapsed() / 1e6;
        const double seconds = double(pcm.size()) / 2 / Moonshine::kSampleRate;
        qInfo().noquote() << QStringLiteral("moonshine: %1 s of speech in %2 ms (%3x real time)")
                                 .arg(seconds, 0, 'f', 1).arg(ms, 0, 'f', 0).arg(seconds * 1000 / ms, 0, 'f', 1);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        QCOMPARE(text, expected);
    }

    void tooShortToBeSpeechIsEmptyNotAnError()
    {
        QString why;
        QCOMPARE(m.transcribe(QByteArray(100, '\1'), &why), QString());
        QVERIFY(why.isEmpty());
        QCOMPARE(m.transcribe(QByteArray(), &why), QString());
        QVERIFY(why.isEmpty());
    }

    void quietAudioProducesNoInventedParagraph()
    {
        QString why;
        const QString text = m.transcribe(QByteArray(Moonshine::kSampleRate * 2 * 3, '\0'), &why);
        QVERIFY(why.isEmpty());
        QVERIFY2(text.size() < 40, qPrintable(text));   // the token bound keeps a hallucination short
    }

    void detokenizeHandlesSpacesBytesAndSpecials()
    {
        // ids come from the real vocabulary: pick them by piece.
        auto id = [this](const QString &piece) {
            const QJsonArray a = QJsonDocument::fromJson(readFile(QStringLiteral(NOCH_MOONSHINE_VOCAB))).array();
            for (int i = 0; i < a.size(); ++i)
                if (a.at(i).toString() == piece)
                    return i;
            return -1;
        };
        const int hello = id(QString::fromUtf8("▁Hello"));
        const int comma = id(QStringLiteral(","));
        const int e = id(QStringLiteral("<0xC3>")), acute = id(QStringLiteral("<0xA9>"));
        QVERIFY(hello > 0 && comma > 0 && e > 0 && acute > 0);
        QCOMPARE(m.detokenize({ 1, hello, comma, 2 }), QStringLiteral("Hello,"));          // <s> and </s> dropped
        QCOMPARE(m.detokenize({ hello, id(QString::fromUtf8("▁car")), e, acute }),
                 QString::fromUtf8("Hello car\u00e9"));                                       // two bytes make one letter
        QCOMPARE(m.detokenize({}), QString());
    }
};

QTEST_GUILESS_MAIN(TstMoonshine)
#include "tst_moonshine.moc"
