#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// ProviderProfile — the one table describing every chat provider Buddy can
// talk to: where it lives, which wire dialect it speaks, how it
// authenticates, and what it can do. Before this table existed, the provider
// was a bare QString and each of these facts was re-derived as string
// comparisons scattered across the BuddyProvider implementation files (plus
// a second, drift-prone copy of the endpoint map in SettingsPanel.qml).
//
// Everything provider-specific in the request/response path consults this
// table through profileFor(). Settings projects the same table to QML via
// BuddyProvider::providerProfiles(), so the C++ copy is the only copy.
//
// The table is pure data on purpose: per-model quirks (Ollama tool support,
// Anthropic computer-tool versions, vision by model name) stay with the code
// that already owns them — the profile decides the provider-level shape,
// never the model-level one.

namespace provider {

// The wire protocol a provider speaks. Response parsing, payload shape,
// tool-call encoding and streaming format all follow from this.
enum class Dialect {
    None,              // "none": no provider configured
    Anthropic,         // Anthropic Messages API (/v1/messages)
    OpenAiCompatible,  // OpenAI Chat Completions (/v1/chat/completions)
    Ollama,            // Ollama native API (/api/chat)
};

// How the API key is presented on a request.
enum class AuthStyle {
    None,              // no credentials are ever sent
    Bearer,            // Authorization: Bearer <key>, only when a key is set
    AnthropicHeaders,  // x-api-key: <key> + anthropic-version: 2023-06-01
};

struct ProviderProfile {
    QString id;
    QString displayName;
    QString defaultBase;          // used when the user has not set a base URL
    QStringList chatPath;         // path segments of the chat endpoint, joined onto the base
    QStringList modelsPath;       // path segments of the model-list endpoint
    Dialect dialect = Dialect::None;
    AuthStyle auth = AuthStyle::None;

    // Provider-level capabilities. Model-level capabilities (vision for
    // non-Anthropic providers, Ollama tool support) are still decided from
    // the model name by the code that owns those lists.
    bool supportsVision = false;       // every model here takes images (Anthropic); elsewhere the model name decides
    bool supportsNativeTools = false;  // the dialect has a native tool-calling wire format
    bool supportsStreaming = false;    // replies can stream over SSE (Ollama streams NDJSON instead: buffered)
    bool supportsPromptCaching = false; // cache_control breakpoints on the prompt prefix
    bool streamUsageOption = false;    // accepts stream_options.include_usage (the real OpenAI API only)

    // Key handling. These two intentionally differ for OpenCode Zen:
    // a run does not hard-require a key (needsApiKey), but Settings still
    // presents the key as expected (the free tier is key-gated upstream).
    bool requiresApiKey = false;  // a run/probe refuses to start without a key
    bool settingsNeedsKey = false; // Settings placeholder/hint: a key is expected
    bool showApiKeyField = true;  // Settings shows the API-key field at all

    QString keyHint;              // Settings hint under the key field (empty = generic)
    QStringList presetModels;     // Settings model presets, best first
};

// The profile for a provider id. Unknown ids resolve to the "custom"
// profile: an arbitrary OpenAI-compatible endpoint is the base case, so an
// unrecognized provider string degrades exactly the way the old if-chains
// did (their fall-through was the OpenAI shape).
const ProviderProfile &profileFor(const QString &id);

// Every registered profile, in Settings display order. Includes "none";
// projections that list selectable providers skip it (see
// BuddyProvider::providerProfiles()).
const QList<ProviderProfile> &allProfiles();

// The profile as a settings map: {id, label, base, needsKey, showKey,
// keyHint, models, chatPath} — the shape SettingsPanel.qml consumes.
QVariantMap profileVariant(const ProviderProfile &profile);

} // namespace provider
