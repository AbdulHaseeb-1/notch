// Buddy sound effects: synthesized buffers, pacing, mute and volume.
// The audio device is never opened (NOCH_NO_AUDIO / setDeviceDisabled).
#include <QtTest>
#include <cmath>

#include "../src/BuddySfx.h"

class TstBuddySfx : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("NOCH_NO_AUDIO", "1"); }

    void buffers_data()
    {
        QTest::addColumn<QString>("cue");
        QTest::addColumn<double>("pitch");
        for (const QString &c : BuddySfx::cueNames())
            for (double p : { 0.841, 1.0, 1.189 })
                QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(c).arg(p))) << c << p;
    }
    void buffers()
    {
        QFETCH(QString, cue);
        QFETCH(double, pitch);
        const QVector<float> b = BuddySfx::renderCue(cue, pitch);
        QVERIFY(b.size() >= 2 * 1000);           // > ~23 ms
        QCOMPARE(b.size() % 2, 0);              // whole stereo frames
        QVERIFY(b.size() / 2 < BuddySfx::kSynthRate * 3);   // < 3 s
        float peak = 0;
        for (float s : b) {
            QVERIFY(std::isfinite(s));
            peak = std::max(peak, std::fabs(s));
        }
        QVERIFY2(peak <= 0.5f, "cue level must leave headroom (no clipping)");
        QVERIFY2(peak > 0.05f, "cue must be audible");
        // Starts and ends at silence: no click at either edge.
        QVERIFY(std::fabs(b[0]) < 1e-4f && std::fabs(b[1]) < 1e-4f);
        QVERIFY(std::fabs(b[b.size() - 1]) < 1e-4f && std::fabs(b[b.size() - 2]) < 1e-4f);
        // No sample-to-sample jump big enough to be a pop.
        float maxStep = 0;
        for (int i = 2; i < b.size(); ++i)
            maxStep = std::max(maxStep, std::fabs(b[i] - b[i - 2]));
        QVERIFY2(maxStep < 0.25f, qPrintable(QString::number(maxStep)));
        // The last 10 ms fade monotonically in envelope (quiet tail).
        const int tail = BuddySfx::kSynthRate / 100;
        float tailPeak = 0;
        for (int i = b.size() - 2 * tail; i < b.size(); ++i)
            tailPeak = std::max(tailPeak, std::fabs(b[i]));
        QVERIFY(tailPeak < peak * 0.5f);
    }

    void resample()
    {
        const QVector<float> b = BuddySfx::renderCue(QStringLiteral("reply"), 1.0);
        const QVector<float> r = BuddySfx::resampleStereo(b, 44100, 48000);
        const double ratio = double(r.size()) / b.size();
        QVERIFY(std::fabs(ratio - 48000.0 / 44100.0) < 0.001);
        for (float s : r)
            QVERIFY(std::isfinite(s) && std::fabs(s) <= 0.5f);
        QCOMPARE(BuddySfx::resampleStereo(b, 44100, 44100).size(), b.size());
    }

    void pacing()
    {
        BuddySfx sfx;
        sfx.setDeviceDisabled(true);
        QVERIFY(sfx.admit(QStringLiteral("pop"), 1000));
        QVERIFY(!sfx.admit(QStringLiteral("pop"), 1050));   // spam within 110 ms
        QVERIFY(sfx.admit(QStringLiteral("wake"), 1050));   // other cues unaffected
        QVERIFY(sfx.admit(QStringLiteral("pop"), 1400));
        // Long cues never block a repeat for more than 350 ms.
        QVERIFY(sfx.admit(QStringLiteral("reply"), 5000));
        QVERIFY(!sfx.admit(QStringLiteral("reply"), 5200));
        QVERIFY(sfx.admit(QStringLiteral("reply"), 5360));
    }

    void speakRunStops()
    {
        BuddySfx sfx;
        sfx.setDeviceDisabled(true);
        QElapsedTimer t; t.start();
        int played = 0;
        // Blip attempts every 30 ms for 4 s, as a long streamed answer would.
        while (t.elapsed() < 4000) {
            if (sfx.play(QStringLiteral("speak")))
                ++played;
            QTest::qWait(30);
        }
        QVERIFY2(played >= 3, qPrintable(QString::number(played)));
        QVERIFY2(played <= 2400 / 110 + 1, qPrintable(QString::number(played)));
    }

    void muteAndVolume()
    {
        BuddySfx sfx;
        sfx.setDeviceDisabled(true);
        QVERIFY(sfx.play(QStringLiteral("happy")));
        sfx.setEnabled(false);
        QVERIFY(!sfx.play(QStringLiteral("wake")));
        sfx.setEnabled(true);
        sfx.setVolume(0.0);
        QVERIFY(!sfx.play(QStringLiteral("wake")));
        sfx.setVolume(std::nan(""));
        QCOMPARE(sfx.volume(), 0.0);
        sfx.setVolume(2.0);
        QCOMPARE(sfx.volume(), 1.0);
        QVERIFY(sfx.play(QStringLiteral("open")));   // alias of wake
        QVERIFY(!sfx.play(QStringLiteral("nope"))); // unknown cue is ignored
        QVERIFY(sfx.outputGain() > 0.99);
    }

    void styleRebuildKeepsWorking()
    {
        BuddySfx sfx;
        sfx.setDeviceDisabled(true);
        sfx.setStyle(QStringLiteral("cute"));
        QCOMPARE(sfx.style(), QStringLiteral("cute"));
        sfx.setStyle(QStringLiteral("bogus"));
        QCOMPARE(sfx.style(), QStringLiteral("warm"));
        QVERIFY(sfx.play(QStringLiteral("error")));
        QCOMPARE(sfx.voicesStarted(), 1);
    }
};

QTEST_MAIN(TstBuddySfx)
#include "tst_buddysfx.moc"
