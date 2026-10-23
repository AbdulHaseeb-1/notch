// The software echo canceller: a synthetic far end plays through a made-up room into a microphone;
// the canceller must take the echo out, leave a person talking over it alone, and cost nothing
// while nothing plays.

#include <QtTest>

#include <cmath>
#include <random>
#include <vector>

#include "EchoCanceller.h"

namespace {

constexpr int kRate = EchoCanceller::kRate;
constexpr int kFrame = 640;                      // 40 ms, like the live session

QByteArray toPcm(const std::vector<float> &v)
{
    QByteArray out(int(v.size()) * 2, Qt::Uninitialized);
    auto *s = reinterpret_cast<qint16 *>(out.data());
    for (size_t i = 0; i < v.size(); ++i)
        s[i] = qint16(std::lround(std::clamp(v[i], -1.0f, 1.0f) * 32767.0f));
    return out;
}

std::vector<float> fromPcm(const QByteArray &b)
{
    std::vector<float> v(size_t(b.size() / 2));
    const auto *s = reinterpret_cast<const qint16 *>(b.constData());
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = float(s[i]) / 32768.0f;
    return v;
}

// Speech-like far end: noise with a coloured part, rising and falling like syllables.
struct FarEnd
{
    std::mt19937 rng{ 7 };
    std::normal_distribution<float> nd{ 0.0f, 1.0f };
    float lp = 0.0f;
    long t = 0;
    float next()
    {
        const float w = nd(rng);
        lp = 0.8f * lp + 0.2f * w;
        const float env = 0.55f + 0.45f * std::sin(2.0f * float(M_PI) * 3.1f * float(t++) / kRate);
        return 0.16f * env * (0.6f * w + 1.6f * lp);
    }
};

// The room: a few reflections between the speakers and the microphone.
struct Room
{
    std::vector<float> history = std::vector<float>(4096, 0.0f);
    size_t pos = 0;
    std::mt19937 rng{ 11 };
    std::normal_distribution<float> noise{ 0.0f, 0.0015f };
    float echo(float x)
    {
        history[pos] = x;
        auto at = [&](size_t d) { return history[(pos + history.size() - d) % history.size()]; };
        const float y = 0.42f * at(480) - 0.24f * at(752) + 0.13f * at(1520) + 0.06f * at(2560);
        pos = (pos + 1) % history.size();
        return y + noise(rng);
    }
};

double power(const std::vector<float> &v, size_t from, size_t to)
{
    double p = 0.0;
    for (size_t i = from; i < to; ++i)
        p += double(v[i]) * v[i];
    return p / double(to - from);
}

// Power of a single tone in a signal (Goertzel).
double tonePower(const std::vector<float> &v, size_t from, size_t to, double hz)
{
    const double w = 2.0 * M_PI * hz / kRate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double s0 = v[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double mag2 = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * mag2 / (double(to - from) * double(to - from));
}

struct Run
{
    EchoCanceller aec;
    FarEnd far;
    Room room;
    std::vector<float> mic, out;

    // seconds of far end, with an optional near-end tone mixed into the microphone
    void play(double seconds, double toneHz = 0.0, float toneAmp = 0.0f)
    {
        const long frames = long(seconds * kRate) / kFrame;
        for (long f = 0; f < frames; ++f) {
            std::vector<float> ref(kFrame), m(kFrame);
            for (int i = 0; i < kFrame; ++i) {
                ref[i] = far.next();
                m[i] = room.echo(ref[i]);
                if (toneHz > 0.0)
                    m[i] += toneAmp * std::sin(2.0f * float(M_PI) * float(toneHz) * float(mic.size() + i) / kRate);
            }
            aec.pushReference(toPcm(ref));
            const QByteArray cleaned = aec.process(toPcm(m));
            const std::vector<float> o = fromPcm(cleaned);
            mic.insert(mic.end(), m.begin(), m.end());
            out.insert(out.end(), o.begin(), o.end());
        }
    }

    double erle(double lastSeconds) const
    {
        const size_t to = mic.size();
        const size_t from = to - size_t(lastSeconds * kRate);
        return 10.0 * std::log10(power(mic, from, to) / (power(out, from, to) + 1e-12));
    }
};

} // namespace

class tst_Echo : public QObject
{
    Q_OBJECT

private slots:
    void theEchoIsTakenOut()
    {
        Run r;
        r.play(25.0);
        qInfo() << "ERLE after 25 s:" << r.erle(4.0) << "dB, reported" << r.aec.erleDb();
        QVERIFY2(r.erle(4.0) > 15.0, "the echo should be at least 15 dB quieter");
        QVERIFY(r.aec.converged());
    }

    void aPersonTalkingOverItSurvives()
    {
        Run r;
        r.play(20.0);
        const size_t from = r.mic.size();
        r.play(3.0, 523.0, 0.08f);                           // 3 s of someone talking over the agent
        const size_t to = r.mic.size();
        const double before = tonePower(r.mic, from + 8000, to, 523.0);
        const double after = tonePower(r.out, from + 8000, to, 523.0);
        qInfo() << "near-end tone kept:" << after / before;
        QVERIFY2(after > 0.7 * before, "the voice over the echo must come through");
        // And the filter was not spoiled by it.
        r.play(6.0);
        QVERIFY2(r.erle(3.0) > 12.0, "cancellation must survive the double talk");
    }

    void silenceIsPassedUntouchedAndCostsNothing()
    {
        EchoCanceller aec;
        std::vector<float> m(kFrame);
        for (int i = 0; i < kFrame; ++i)
            m[i] = 0.1f * std::sin(2.0f * float(M_PI) * 300.0f * float(i) / kRate);
        const QByteArray in = toPcm(m);
        QCOMPARE(aec.process(in), in);
        QVERIFY(!aec.running());
    }

    void aFlushedReferenceIsForgotten()
    {
        EchoCanceller aec;
        FarEnd far;
        std::vector<float> ref(kFrame * 4);
        for (auto &v : ref)
            v = far.next();
        aec.pushReference(toPcm(ref));
        aec.clearReference();
        std::vector<float> m(kFrame, 0.05f);
        const QByteArray in = toPcm(m);
        QCOMPARE(aec.process(in), in);                       // nothing queued: untouched
    }

    void resetForgetsWhatWasLearned()
    {
        Run r;
        r.play(8.0);
        QVERIFY(r.aec.erleDb() > 3.0);
        r.aec.reset();
        QCOMPARE(r.aec.erleDb(), 0.0);
    }
};

QTEST_GUILESS_MAIN(tst_Echo)
#include "tst_echo.moc"
