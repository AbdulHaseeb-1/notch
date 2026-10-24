// DeepgramSettings: the one Deepgram configuration (audit item: Deepgram was
// integrated three times, with settings that could drift).
//
// Covered here:
//  * the startup migration — a think configuration the user actually changed
//    in an older version becomes an explicit override; an untouched one
//    starts following Buddy; the sweep runs once and never again;
//  * the think resolution — override wins; otherwise Buddy's chat provider
//    (managed where Deepgram runs it, through Buddy's own endpoint where it
//    cannot); otherwise the historical built-in default;
//  * the sharing itself — one object feeding push-to-talk, the live agent
//    and spoken answers at once.
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "DeepgramAgent.h"
#include "DeepgramSettings.h"
#include "SpeechOut.h"
#include "VoiceInput.h"

namespace {

// Long enough, no spaces: passes deepgram::keyLooksValid.
const QString kKey = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

QString iniPath(const QTemporaryDir &dir)
{
    return dir.filePath(QStringLiteral("noch.ini"));
}

} // namespace

class tst_DeepgramSettings : public QObject
{
    Q_OBJECT

private slots:
    // ---- the migration ------------------------------------------------------

    void migrationLeavesAnUntouchedThinkFollowingBuddy()
    {
        QTemporaryDir dir;
        QSettings s(iniPath(dir), QSettings::IniFormat);
        migrateDeepgramSettings(s);
        QVERIFY(s.contains(QStringLiteral("ui/buddyLiveThinkOverride")));
        QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), false);
    }

    void migrationKeepsACustomizedThinkAsAnOverride()
    {
        QTemporaryDir dir;
        {   // a model the user picked under the old version
            QSettings s(iniPath(dir), QSettings::IniFormat);
            s.setValue(QStringLiteral("ui/buddyLiveModel"), QStringLiteral("claude-sonnet-5"));
            migrateDeepgramSettings(s);
            QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), true);
        }
        {   // only an endpoint
            QSettings s(iniPath(dir), QSettings::IniFormat);
            s.remove(QStringLiteral("ui/buddyLiveModel"));
            s.remove(QStringLiteral("ui/buddyLiveThinkOverride"));
            s.setValue(QStringLiteral("ui/buddyLiveEndpoint"), QStringLiteral("https://example.com/v1/chat/completions"));
            migrateDeepgramSettings(s);
            QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), true);
        }
        {   // only a temperature
            QSettings s(iniPath(dir), QSettings::IniFormat);
            s.remove(QStringLiteral("ui/buddyLiveEndpoint"));
            s.remove(QStringLiteral("ui/buddyLiveThinkOverride"));
            s.setValue(QStringLiteral("ui/buddyLiveTemperature"), 0.7);
            migrateDeepgramSettings(s);
            QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), true);
        }
    }

    void migrationRunsOnce()
    {
        QTemporaryDir dir;
        QSettings s(iniPath(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("ui/buddyLiveModel"), QStringLiteral("claude-sonnet-5"));
        migrateDeepgramSettings(s);
        QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), true);
        // The user later turns the override off (follows Buddy again) while
        // the old values are still in the file: a second sweep must not
        // resurrect the override.
        s.setValue(QStringLiteral("ui/buddyLiveThinkOverride"), false);
        migrateDeepgramSettings(s);
        QCOMPARE(s.value(QStringLiteral("ui/buddyLiveThinkOverride")).toBool(), false);
    }

    // ---- the think resolution ----------------------------------------------

    void anUnconfiguredBuddyKeepsTheBuiltInDefault()
    {
        DeepgramSettings s;
        const auto t = s.effectiveThink();
        QCOMPARE(t.provider, QStringLiteral("anthropic"));
        QCOMPARE(t.model, QStringLiteral("claude-haiku-5-5"));
        QCOMPARE(t.temperature, -1.0);
        QVERIFY(t.endpointUrl.isEmpty());
        QCOMPARE(s.thinkSource(), QStringLiteral("default"));
    }

    void aLocalBuddyModelCannotBeFollowed()
    {
        DeepgramSettings s;
        s.setBuddyProvider(QStringLiteral("ollama"));
        s.setBuddyModel(QStringLiteral("llama3.1"));
        s.setBuddyApiBase(QStringLiteral("http://localhost:11434"));   // Deepgram cannot call this
        QCOMPARE(s.effectiveThink().model, QStringLiteral("claude-haiku-5-5"));
        QCOMPARE(s.thinkSource(), QStringLiteral("default"));
    }

    void followsBuddyManagedProviders()
    {
        DeepgramSettings s;
        s.setBuddyProvider(QStringLiteral("anthropic"));
        s.setBuddyModel(QStringLiteral("claude-sonnet-5"));
        s.setBuddyTemperature(0.3);
        auto t = s.effectiveThink();
        QCOMPARE(t.provider, QStringLiteral("anthropic"));
        QCOMPARE(t.model, QStringLiteral("claude-sonnet-5"));
        QCOMPARE(t.temperature, 0.3);
        QVERIFY(t.endpointUrl.isEmpty());
        QCOMPARE(s.thinkSource(), QStringLiteral("buddy"));

        s.setBuddyProvider(QStringLiteral("openai"));
        s.setBuddyModel(QStringLiteral("gpt-5-mini"));
        t = s.effectiveThink();
        QCOMPARE(t.provider, QStringLiteral("open_ai"));
        QCOMPARE(t.model, QStringLiteral("gpt-5-mini"));
    }

    void followsBuddyThroughItsOwnEndpoint()
    {
        DeepgramSettings s;
        s.setBuddyProvider(QStringLiteral("openrouter"));
        s.setBuddyModel(QStringLiteral("some/model"));
        s.setBuddyApiBase(QStringLiteral("https://openrouter.ai/api/v1"));
        s.setBuddyApiKey(QStringLiteral("buddy-key"));
        const auto t = s.effectiveThink();
        QCOMPARE(t.provider, QStringLiteral("open_ai"));
        QCOMPARE(t.model, QStringLiteral("some/model"));
        QCOMPARE(t.endpointUrl, QStringLiteral("https://openrouter.ai/api/v1/chat/completions"));
        QCOMPARE(t.endpointKey, QStringLiteral("buddy-key"));
        QCOMPARE(s.thinkSource(), QStringLiteral("buddy"));
    }

    void theOverrideWinsAndSeedsFromWhatWasInEffect()
    {
        DeepgramSettings s;
        s.setBuddyProvider(QStringLiteral("anthropic"));
        s.setBuddyModel(QStringLiteral("claude-sonnet-5"));
        s.setThinkOverride(true);
        // Seeded from the effective choice, so nothing changes yet.
        QCOMPARE(s.thinkProvider(), QStringLiteral("anthropic"));
        QCOMPARE(s.thinkModel(), QStringLiteral("claude-sonnet-5"));
        QCOMPARE(s.thinkSource(), QStringLiteral("override"));
        // From here the override is its own: Buddy moving does not move it.
        s.setThinkModel(QStringLiteral("claude-haiku-4-5"));
        s.setBuddyModel(QStringLiteral("claude-sonnet-4-6"));
        QCOMPARE(s.effectiveThink().model, QStringLiteral("claude-haiku-4-5"));
    }

    // ---- the sharing ----------------------------------------------------------

    void keytermsParseOnceForEveryone()
    {
        const QStringList terms = DeepgramSettings::parseKeyterms(
            QStringLiteral(" Noch, buddy\nNoch ; Knee exercises "));
        QCOMPARE(terms, QStringList({ QStringLiteral("Noch"), QStringLiteral("buddy"),
                                      QStringLiteral("Knee exercises") }));
    }

    void oneObjectFeedsAllThreeConsumers()
    {
        DeepgramSettings s;
        s.setApiKey(kKey);
        s.setListenModel(QStringLiteral("nova-3"));
        s.setListenLanguage(QStringLiteral("en"));
        s.setKeyterms(QStringLiteral("Noch, Buddy"));
        s.setSpeakVoice(QStringLiteral("aura-2-zeus-en"));

        VoiceInput voice;
        voice.setDeepgramSettings(&s);
        QVERIFY2(voice.configured(), qPrintable(voice.problem()));

        DeepgramAgent agent;
        agent.setDeepgramSettings(&s);
        QCOMPARE(agent.config().apiKey, kKey);
        QCOMPARE(agent.config().listenModel, QStringLiteral("nova-3"));
        QCOMPARE(agent.config().keyterms, QStringList({ QStringLiteral("Noch"), QStringLiteral("Buddy") }));
        QCOMPARE(agent.config().speakModel, QStringLiteral("aura-2-zeus-en"));
        QCOMPARE(agent.config().thinkModel, QStringLiteral("claude-haiku-5-5"));

        SpeechOut speech;
        speech.setDeepgramSettings(&s);
        speech.setEngine(QStringLiteral("deepgram"));
        QVERIFY2(speech.available(), qPrintable(speech.problem()));
        QVERIFY(speech.engineStatuses().value(QStringLiteral("deepgram")).toMap()
                    .value(QStringLiteral("ready")).toBool());

        // One change reaches all three.
        s.setSpeakVoice(QString());
        QCOMPARE(agent.config().speakModel, QString());
        QVERIFY(!speech.available());
        s.setApiKey(QString());
        QVERIFY(!voice.configured());
        QVERIFY(!agent.problem().isEmpty());
    }
};

QTEST_MAIN(tst_DeepgramSettings)
#include "tst_deepgramsettings.moc"
