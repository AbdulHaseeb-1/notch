#include "ProviderProfile.h"

#include <QVariantList>

namespace provider {
namespace {

const QList<ProviderProfile> &table()
{
    // One row per provider, in Settings display order. The values are the
    // behavior the app has always had, collected from the if-chains this
    // table replaces (defaultBaseFor/chatPathFor/modelsPathFor, buildRequest,
    // needsApiKey, wantsStreaming and the SettingsPanel providerInfo map) —
    // changing a value here changes behavior and must be deliberate.
    // Aggregate field order: id, displayName, defaultBase, chatPath,
    // modelsPath, dialect, auth, supportsVision, supportsNativeTools,
    // supportsStreaming, supportsPromptCaching, streamUsageOption,
    // requiresApiKey, settingsNeedsKey, showApiKeyField, keyHint,
    // presetModels. tests/tst_providerprofile.cpp pins every field.
    static const QList<ProviderProfile> profiles{
        {
            QStringLiteral("none"),
            QString(),
            QString(),
            { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") },
            { QStringLiteral("v1"), QStringLiteral("models") },
            Dialect::None,
            AuthStyle::None,
            false, false, false, false, false,
            false, false, false,
            QString(),
            {},
        },
        {
            QStringLiteral("openai"),
            QStringLiteral("OpenAI"),
            QStringLiteral("https://api.openai.com"),
            { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") },
            { QStringLiteral("v1"), QStringLiteral("models") },
            Dialect::OpenAiCompatible,
            AuthStyle::Bearer,
            false, true, true, false, true,
            true, true, true,
            QString(),
            { QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o"),
              QStringLiteral("gpt-4.1-mini"), QStringLiteral("gpt-4.1"),
              QStringLiteral("o4-mini") },
        },
        {
            QStringLiteral("opencode"),
            QStringLiteral("OpenCode"),
            QStringLiteral("https://opencode.ai/zen"),
            { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") },
            { QStringLiteral("v1"), QStringLiteral("models") },
            Dialect::OpenAiCompatible,
            AuthStyle::Bearer,
            false, true, true, false, false,
            false, true, true,
            QStringLiteral("Free from opencode.ai (email sign-up, no card). The *-free models cost nothing."),
            // OpenCode Zen's free models (verified against /zen/v1/models,
            // Oct 2026); "Test connection" refreshes this list from the server.
            { QStringLiteral("glm-5.3-flash-free"), QStringLiteral("deepseek-v4.1-flash-free"),
              QStringLiteral("big-pickle"), QStringLiteral("beam-free"),
              QStringLiteral("mimo-v2.6-flash-free"), QStringLiteral("nemotron-3.5-super-vl-free"),
              QStringLiteral("nemotron-3.5-lightning-free"), QStringLiteral("minimax-m3.1-flash-free"),
              QStringLiteral("jev-1.13-free"), QStringLiteral("longcat-2.5-preview-free") },
        },
        {
            QStringLiteral("ollama"),
            QStringLiteral("Ollama"),
            QStringLiteral("http://localhost:11434"),
            { QStringLiteral("api"), QStringLiteral("chat") },
            { QStringLiteral("api"), QStringLiteral("tags") },
            Dialect::Ollama,
            AuthStyle::Bearer,
            false, true, false, false, false,
            false, false, false,
            QString(),
            { QStringLiteral("llama3.2"), QStringLiteral("llama3.1"),
              QStringLiteral("qwen2.5"), QStringLiteral("gemma3") },
        },
        {
            QStringLiteral("anthropic"),
            QStringLiteral("Anthropic"),
            QStringLiteral("https://api.anthropic.com"),
            { QStringLiteral("v1"), QStringLiteral("messages") },
            { QStringLiteral("v1"), QStringLiteral("models") },
            Dialect::Anthropic,
            AuthStyle::AnthropicHeaders,
            true, true, true, true, false,
            true, true, true,
            QString(),
            { QStringLiteral("claude-sonnet-4-6"), QStringLiteral("claude-opus-4-6"),
              QStringLiteral("claude-sonnet-4-5"), QStringLiteral("claude-opus-4-1"),
              QStringLiteral("claude-3-7-sonnet-latest"), QStringLiteral("claude-3-5-haiku-latest") },
        },
        {
            QStringLiteral("custom"),
            QStringLiteral("Custom"),
            QString(),
            { QStringLiteral("v1"), QStringLiteral("chat"), QStringLiteral("completions") },
            { QStringLiteral("v1"), QStringLiteral("models") },
            Dialect::OpenAiCompatible,
            AuthStyle::Bearer,
            false, true, true, false, false,
            false, false, true,
            QString(),
            {},
        },
    };
    return profiles;
}

} // namespace

const ProviderProfile &profileFor(const QString &id)
{
    const QList<ProviderProfile> &all = table();
    for (const ProviderProfile &p : all) {
        if (p.id == id)
            return p;
    }
    return all.last(); // "custom": the OpenAI-compatible base case
}

const QList<ProviderProfile> &allProfiles()
{
    return table();
}

QVariantMap profileVariant(const ProviderProfile &profile)
{
    QVariantMap m;
    m[QStringLiteral("id")] = profile.id;
    m[QStringLiteral("label")] = profile.displayName;
    m[QStringLiteral("base")] = profile.defaultBase;
    m[QStringLiteral("needsKey")] = profile.settingsNeedsKey;
    m[QStringLiteral("showKey")] = profile.showApiKeyField;
    m[QStringLiteral("keyHint")] = profile.keyHint;
    m[QStringLiteral("models")] = profile.presetModels;
    // The chat path as a suffix ("/v1/messages"), for the Settings endpoint
    // hint when the live provider object is not around to ask.
    m[QStringLiteral("chatPath")] = profile.chatPath.isEmpty()
        ? QString()
        : QStringLiteral("/") + profile.chatPath.join(QLatin1Char('/'));
    return m;
}

} // namespace provider
