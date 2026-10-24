// Unit tests for the ProviderProfile table: every registered provider id
// resolves to the profile the app has always behaved with (dialect,
// endpoints, auth, capabilities), and unknown ids degrade to the
// OpenAI-compatible "custom" base case the old if-chain fall-through gave.
#include <QtTest>

#include "ProviderProfile.h"

using provider::AuthStyle;
using provider::Dialect;
using provider::ProviderProfile;
using provider::profileFor;

class TstProviderProfile : public QObject
{
    Q_OBJECT
private slots:
    void registeredIds()
    {
        // The full registered set, in the table's display order.
        QStringList ids;
        for (const ProviderProfile &p : provider::allProfiles())
            ids << p.id;
        QCOMPARE(ids, QStringList({ QStringLiteral("none"), QStringLiteral("openai"),
                                    QStringLiteral("opencode"), QStringLiteral("ollama"),
                                    QStringLiteral("anthropic"), QStringLiteral("custom") }));
    }

    void anthropicProfile()
    {
        const ProviderProfile &p = profileFor(QStringLiteral("anthropic"));
        QCOMPARE(p.displayName, QStringLiteral("Anthropic"));
        QCOMPARE(p.defaultBase, QStringLiteral("https://api.anthropic.com"));
        QCOMPARE(p.chatPath, QStringList({ QStringLiteral("v1"), QStringLiteral("messages") }));
        QCOMPARE(p.modelsPath, QStringList({ QStringLiteral("v1"), QStringLiteral("models") }));
        QCOMPARE(p.dialect, Dialect::Anthropic);
        QCOMPARE(p.auth, AuthStyle::AnthropicHeaders);
        QVERIFY(p.supportsVision);
        QVERIFY(p.supportsNativeTools);
        QVERIFY(p.supportsStreaming);
        QVERIFY(p.supportsPromptCaching);
        QVERIFY(!p.streamUsageOption);
        QVERIFY(p.requiresApiKey);
        QVERIFY(p.settingsNeedsKey);
        QVERIFY(p.showApiKeyField);
        QVERIFY(!p.presetModels.isEmpty());
    }

    void openAiProfile()
    {
        const ProviderProfile &p = profileFor(QStringLiteral("openai"));
        QCOMPARE(p.displayName, QStringLiteral("OpenAI"));
        QCOMPARE(p.defaultBase, QStringLiteral("https://api.openai.com"));
        QCOMPARE(p.chatPath, QStringList({ QStringLiteral("v1"), QStringLiteral("chat"),
                                           QStringLiteral("completions") }));
        QCOMPARE(p.modelsPath, QStringList({ QStringLiteral("v1"), QStringLiteral("models") }));
        QCOMPARE(p.dialect, Dialect::OpenAiCompatible);
        QCOMPARE(p.auth, AuthStyle::Bearer);
        QVERIFY(!p.supportsVision);          // per-model: the model name decides
        QVERIFY(p.supportsNativeTools);
        QVERIFY(p.supportsStreaming);
        QVERIFY(!p.supportsPromptCaching);
        QVERIFY(p.streamUsageOption);        // include_usage is OpenAI-only
        QVERIFY(p.requiresApiKey);
        QVERIFY(p.settingsNeedsKey);
        QVERIFY(p.showApiKeyField);
    }

    void ollamaProfile()
    {
        const ProviderProfile &p = profileFor(QStringLiteral("ollama"));
        QCOMPARE(p.displayName, QStringLiteral("Ollama"));
        QCOMPARE(p.defaultBase, QStringLiteral("http://localhost:11434"));
        QCOMPARE(p.chatPath, QStringList({ QStringLiteral("api"), QStringLiteral("chat") }));
        QCOMPARE(p.modelsPath, QStringList({ QStringLiteral("api"), QStringLiteral("tags") }));
        QCOMPARE(p.dialect, Dialect::Ollama);
        QCOMPARE(p.auth, AuthStyle::Bearer);
        QVERIFY(p.supportsNativeTools);
        QVERIFY(!p.supportsStreaming);       // NDJSON stream: replies stay buffered
        QVERIFY(!p.requiresApiKey);
        QVERIFY(!p.settingsNeedsKey);
        QVERIFY(!p.showApiKeyField);         // Settings hides the key field
    }

    void opencodeProfile()
    {
        const ProviderProfile &p = profileFor(QStringLiteral("opencode"));
        QCOMPARE(p.displayName, QStringLiteral("OpenCode"));
        QCOMPARE(p.defaultBase, QStringLiteral("https://opencode.ai/zen"));
        QCOMPARE(p.chatPath, QStringList({ QStringLiteral("v1"), QStringLiteral("chat"),
                                           QStringLiteral("completions") }));
        QCOMPARE(p.dialect, Dialect::OpenAiCompatible);
        QCOMPARE(p.auth, AuthStyle::Bearer);
        QVERIFY(p.supportsStreaming);
        QVERIFY(!p.streamUsageOption);
        // The deliberate split: runs do not hard-require a key, Settings
        // still presents one as expected.
        QVERIFY(!p.requiresApiKey);
        QVERIFY(p.settingsNeedsKey);
        QVERIFY(!p.keyHint.isEmpty());
    }

    void customAndUnknownResolveToOpenAiShape()
    {
        const ProviderProfile &custom = profileFor(QStringLiteral("custom"));
        QCOMPARE(custom.displayName, QStringLiteral("Custom"));
        QCOMPARE(custom.defaultBase, QString());
        QCOMPARE(custom.dialect, Dialect::OpenAiCompatible);
        QCOMPARE(custom.auth, AuthStyle::Bearer);
        QVERIFY(custom.presetModels.isEmpty());

        // An id nobody registered (a provider string from an old settings
        // file, a typo) gets the same OpenAI-compatible fall-through the
        // if-chains had.
        const ProviderProfile &unknown = profileFor(QStringLiteral("some-future-provider"));
        QCOMPARE(unknown.id, QStringLiteral("custom"));
        QCOMPARE(unknown.dialect, custom.dialect);
        QCOMPARE(unknown.chatPath, custom.chatPath);
    }

    void noneProfile()
    {
        const ProviderProfile &p = profileFor(QStringLiteral("none"));
        QCOMPARE(p.dialect, Dialect::None);
        QCOMPARE(p.auth, AuthStyle::None);
        QVERIFY(!p.supportsStreaming);
        QVERIFY(!p.supportsNativeTools);
        QVERIFY(!p.requiresApiKey);
    }

    void settingsProjection()
    {
        // The map SettingsPanel.qml consumes: same values the JS table had.
        const QVariantMap m = provider::profileVariant(profileFor(QStringLiteral("anthropic")));
        QCOMPARE(m.value(QStringLiteral("id")).toString(), QStringLiteral("anthropic"));
        QCOMPARE(m.value(QStringLiteral("label")).toString(), QStringLiteral("Anthropic"));
        QCOMPARE(m.value(QStringLiteral("base")).toString(), QStringLiteral("https://api.anthropic.com"));
        QCOMPARE(m.value(QStringLiteral("chatPath")).toString(), QStringLiteral("/v1/messages"));
        QCOMPARE(m.value(QStringLiteral("needsKey")).toBool(), true);
        QCOMPARE(m.value(QStringLiteral("showKey")).toBool(), true);
        QVERIFY(!m.value(QStringLiteral("models")).toStringList().isEmpty());

        const QVariantMap o = provider::profileVariant(profileFor(QStringLiteral("ollama")));
        QCOMPARE(o.value(QStringLiteral("chatPath")).toString(), QStringLiteral("/api/chat"));
        QCOMPARE(o.value(QStringLiteral("needsKey")).toBool(), false);
        QCOMPARE(o.value(QStringLiteral("showKey")).toBool(), false);
    }
};

QTEST_MAIN(TstProviderProfile)
#include "tst_providerprofile.moc"
