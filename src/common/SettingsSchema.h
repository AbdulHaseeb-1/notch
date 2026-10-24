// The settings schema: every key Noch persists, declared once.
//
// The schema used to exist only as property declarations inside the QML
// Settings object in qml/Main.qml plus ad-hoc QSettings reads in C++
// (MusicPlayer's player/*, DockController's dock/*, Appearance's buddy/ring
// ...). There was no single place that could answer "what settings exist,
// what are their types and defaults, and which are secret". This table is
// that place.
//
// RULES for editing this table:
// - Key names and storage NEVER change here. A rename or a move to another
//   group is a migration, not a table edit, and existing users' INI values
//   must keep working untouched.
// - `defaultValue` is the value a fresh install sees: it must match what
//   the pre-registry QML declaration said. If a default ever needs to
//   change, that is a product decision, not a refactor.
// - `min`/`max` (when hasRange) and `values` are ADVISORY metadata taken
//   from the UI editors and the clamps at the use sites. Nothing enforces
//   them at the Settings object; a hand-edited INI value outside the range
//   still loads, exactly as before. They exist so pages, backup and
//   migrations can be generated instead of re-deriving the ranges.
// - `secret` is not written in the table: it is computed from
//   secretkeys::isSecretIniKey() when the table is built, so the registry
//   cannot drift from the canonical secret list in SecretKeys.h. Secret
//   entries are metadata only — the values live in the SecretStore vault,
//   never in QSettings (see AppSecrets).
#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "SecretKeys.h"

namespace noch::settingschema {

// String: free text. Enum: a String whose value is one of `values`.
// StringList: a list of strings. Bytes: raw bytes (not displayable text).
enum class Type { Bool, Int, Real, String, Enum, StringList, Bytes };

struct Entry {
    const char *key; // full settings key, "group/name" as QSettings sees it
    Type type;
    QVariant defaultValue;
    double min = 0;
    double max = 0;
    bool hasRange = false; // min/max are meaningful
    bool secret = false;   // set from SecretKeys.h at build time, never here
    QStringList values;    // Enum: allowed values. Int: values the UI offers.
};

// The full table, in the order the keys are presented in Settings (ui/* in
// Main.qml order, then the legacy secret keys, then the C++-side groups).
// `values` lists the finite set an editor offers where one exists;
// an empty list means free-form.
inline const QVector<Entry> &entries()
{
    static const QVector<Entry> table = [] {
        QVector<Entry> t = {
            // ---- ui: the QML Settings object (Main.qml cfg, same order) ----
            { "ui/sizeScale", Type::Real, QVariant(1.0), 0.7, 1.6, true, false, {} },
            { "ui/autoHide", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/hideDelay", Type::Int, QVariant(1200), 0, 0, false, false,
              { "300", "800", "1200", "1500", "3000" } },
            { "ui/focusMinutes", Type::Int, QVariant(25), 1, 180, true, false, {} },
            { "ui/hotkeyPalette", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/suppressInFullscreen", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyEnabled", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyRingStyle", Type::Enum, QVariant(QStringLiteral("neon")), 0, 0, false, false,
              { "neon", "aurora", "comet", "orbit", "dashes", "pulse", "prism", "halo" } },
            { "ui/buddyRingThickness", Type::Real, QVariant(1.0), 0.5, 2.0, true, false, {} },
            { "ui/buddyRingSpeed", Type::Real, QVariant(1.0), 0.3, 2.5, true, false, {} },
            { "ui/buddyRingShader", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyGlowStrength", Type::Real, QVariant(0.6), 0, 1, true, false, {} },
            { "ui/buddyGlowMode", Type::Enum, QVariant(QStringLiteral("bloom")), 0, 0, false, false,
              { "bloom", "floor", "off" } },
            { "ui/buddyGlowAlways", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyVoiceFx", Type::Enum, QVariant(QStringLiteral("full")), 0, 0, false, false,
              { "off", "rings", "wave", "full" } },
            { "ui/buddyX", Type::Real, QVariant(-1.0), 0, 0, false, false, {} },
            { "ui/buddyY", Type::Real, QVariant(-1.0), 0, 0, false, false, {} },
            { "ui/buddyPlan", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyHistory", Type::String, QVariant(QStringLiteral("[]")), 0, 0, false, false, {} },
            { "ui/buddyProvider", Type::Enum, QVariant(QStringLiteral("none")), 0, 0, false, false,
              { "none", "openai", "opencode", "ollama", "anthropic", "custom" } },
            { "ui/buddyApiBase", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyModel", Type::String, QVariant(QStringLiteral("gpt-4o-mini")), 0, 0, false, false, {} },
            { "ui/buddyTemperature", Type::Real, QVariant(0.7), 0, 1, true, false, {} },
            // Secret-bearing: the JSON stays (bases/models), but older files
            // embed each provider's key, so the whole value is classified
            // secret by SecretKeys.h. The flag below is computed from it.
            { "ui/buddyProviderConfigs", Type::String, QVariant(QStringLiteral("{}")), 0, 0, false, false, {} },
            { "ui/buddyAgentMode", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowBash", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowComputer", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyCuShowCursor", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyCuShowGlow", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyCuGlowIntensity", Type::Real, QVariant(0.6), 0.1, 1, true, false, {} },
            { "ui/buddyCuGlowStyle", Type::Enum, QVariant(QStringLiteral("soft")), 0, 0, false, false,
              { "soft", "prism", "pulse", "chase", "aurora" } },
            { "ui/buddyAllowEditor", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowCodeSearch", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowMemory", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowWebSearch", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowTodoTask", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyOfficialComputer", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyComputerDetail", Type::Enum, QVariant(QStringLiteral("sharp")), 0, 0, false, false,
              { "sharp", "standard" } },
            { "ui/buddyMaxSteps", Type::Int, QVariant(40), 0, 0, false, false,
              { "20", "40", "70", "100" } },
            { "ui/buddyAllowAskUser", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowScreenshots", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowMusic", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowConnectors", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyAllowSkills", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyDisabledSkills", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyVoiceEnabled", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyVoiceHotkey", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyVoiceAutoSend", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyWakeEnabled", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyWakePhrase", Type::String, QVariant(QStringLiteral("hey buddy")), 0, 0, false, false, {} },
            { "ui/buddyWakeThreshold", Type::Real, QVariant(0.5), 0.2, 0.9, true, false, {} },
            { "ui/buddyWakeWords", Type::String, QVariant(QStringLiteral("hey_buddy.onnx")), 0, 0, false, false, {} },
            { "ui/buddySpeakReports", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyTtsEngine", Type::Enum, QVariant(QStringLiteral("google")), 0, 0, false, false,
              { "google", "deepgram" } },
            { "ui/buddyTtsGoogleVoice", Type::String, QVariant(QStringLiteral("Kore")), 0, 0, false, false, {} },
            { "ui/buddyTtsSpeed", Type::Real, QVariant(1.0), 0.7, 1.5, true, false, {} },
            { "ui/buddyTtsSpeakAll", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyYtDlpPath", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyLiveEnabled", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyLiveListenModel", Type::String, QVariant(QStringLiteral("nova-3")), 0, 0, false, false, {} },
            { "ui/buddyLiveLanguage", Type::String, QVariant(QStringLiteral("en")), 0, 0, false, false, {} },
            { "ui/buddyLiveKeyterms", Type::String, QVariant(QStringLiteral("Buddy, Noch, Notepad")), 0, 0, false, false, {} },
            { "ui/buddyLiveEot", Type::Real, QVariant(0.7), 0.5, 1.0, true, false, {} },
            // Live provider/model/voice come from the Deepgram model catalog
            // at runtime (DeepgramAgent), so they are free strings here.
            { "ui/buddyLiveProvider", Type::String, QVariant(QStringLiteral("anthropic")), 0, 0, false, false, {} },
            { "ui/buddyLiveModel", Type::String, QVariant(QStringLiteral("claude-haiku-5-5")), 0, 0, false, false, {} },
            // -1 = the model's own default (newer models refuse the setting).
            { "ui/buddyLiveTemperature", Type::Real, QVariant(-1.0), -1, 2, true, false, {} },
            { "ui/buddyLivePrompt", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyLiveEndpoint", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyLiveThinkOverride", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "ui/buddyLiveAskBuddy", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyLiveVoice", Type::String, QVariant(QStringLiteral("aura-2-thalia-en")), 0, 0, false, false, {} },
            { "ui/buddyLiveSpeed", Type::Real, QVariant(1.0), 0.7, 1.5, true, false, {} },
            { "ui/buddyLiveVolume", Type::Real, QVariant(1.0), 0, 1.5, true, false, {} },
            { "ui/buddyLiveGreeting", Type::String, QVariant(QStringLiteral("I'm listening.")), 0, 0, false, false, {} },
            { "ui/buddyLiveAudioMode", Type::Enum, QVariant(QStringLiteral("speakers")), 0, 0, false, false,
              { "speakers", "headphones" } },
            { "ui/buddyLiveEchoCancel", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyLiveQuick", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyLiveThinkCue", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyLiveFollowUp", Type::Int, QVariant(30), 0, 0, false, false,
              { "0", "30", "60", "120", "300" } },
            { "ui/buddyPermissionMode", Type::Enum, QVariant(QStringLiteral("default")), 0, 0, false, false,
              { "default", "acceptEdits", "plan", "bypass" } },
            { "ui/buddyWorkspaceRoot", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyRedactSecrets", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyKeepTraces", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyStreamReplies", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyShowThinking", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyMemEnabled", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddyMemBackend", Type::Enum, QVariant(QStringLiteral("local")), 0, 0, false, false,
              { "local", "platform", "custom" } },
            { "ui/buddyMemUrl", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyMemUserId", Type::String, QVariant(QStringLiteral("noch")), 0, 0, false, false, {} },
            { "ui/buddyMemLlmModel", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyMemEmbedModel", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyMemEmbedBase", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddySfxEnabled", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "ui/buddySfxVolume", Type::Real, QVariant(0.75), 0.05, 1.0, true, false, {} },
            { "ui/buddySfxStyle", Type::Enum, QVariant(QStringLiteral("warm")), 0, 0, false, false,
              { "cute", "warm", "mellow" } },

            // ---- legacy secret keys: metadata only, values live in the vault ----
            // These were QML Settings properties before the SecretStore
            // migration; they survive only in old INI files (the migration
            // sweeps and clears them). They are registered so tooling can
            // classify them without a second list; the secret flag below is
            // computed from SecretKeys.h, not asserted by hand.
            { "ui/buddyApiKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyDgKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyTtsGoogleKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyLiveEndpointKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyMemKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },
            { "ui/buddyMemEmbedKey", Type::String, QVariant(QString()), 0, 0, false, false, {} },

            // ---- C++-side groups (read/written through QSettings directly) ----
            // MusicPlayer queue state, group "player" (MusicPlayer.cpp).
            { "player/volume", Type::Real, QVariant(0.8), 0, 1, true, false, {} },
            { "player/muted", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "player/repeatMode", Type::Int, QVariant(1), 0, 2, true, false, {} },
            // Legacy shim: read only when repeatMode is absent, written as
            // repeatMode > 0. Kept so old files still migrate.
            { "player/repeat", Type::Bool, QVariant(true), 0, 0, false, false, {} },
            { "player/shuffle", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            { "player/tracks", Type::StringList, QVariant(QStringList()), 0, 0, false, false, {} },
            { "player/index", Type::Int, QVariant(0), 0, 0, false, false, {} },
            { "player/position", Type::Int, QVariant(0), 0, 0, false, false, {} },
            // Dock state (DockController.cpp).
            { "dock/apps", Type::Bytes, QVariant(QByteArray()), 0, 0, false, false, {} },
            { "dock/initialized", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            // Clipboard history switch (ClipboardHistory.cpp).
            { "clipboard/enabled", Type::Bool, QVariant(false), 0, 0, false, false, {} },
            // The ring colour choice (Appearance.cpp).
            { "buddy/ring", Type::Enum, QVariant(QStringLiteral("aurora")), 0, 0, false, false,
              { "aurora", "sunset", "ocean", "mint", "candy", "mono", "accent" } },
            // Startup bookkeeping (main.cpp): the registry migration ran once.
            { "meta/migratedFromRegistry", Type::Bool, QVariant(false), 0, 0, false, false, {} },
        };
        // The secret flag comes from the one canonical list, so the two
        // can never disagree by a typo in this table.
        for (Entry &e : t)
            e.secret = secretkeys::isSecretIniKey(QString::fromLatin1(e.key));
        return t;
    }();
    return table;
}

// Look up an entry by full settings key ("ui/buddyMaxSteps"). Null when unknown.
inline const Entry *find(const QString &key)
{
    for (const Entry &e : entries()) {
        if (key == QLatin1String(e.key))
            return &e;
    }
    return nullptr;
}

// Look up a QML Settings property by its bare name ("buddyMaxSteps" ->
// "ui/buddyMaxSteps"). A name that already contains a slash is used as-is.
inline const Entry *findUi(const QString &name)
{
    if (name.contains(QLatin1Char('/')))
        return find(name);
    return find(QStringLiteral("ui/") + name);
}

// The fresh-install default for a key, or an invalid QVariant when unknown.
inline QVariant defaultValue(const QString &key)
{
    const Entry *e = find(key);
    return e ? e->defaultValue : QVariant();
}

// True when the key's value is a credential (metadata only; the value
// itself lives in the SecretStore). Agrees with SecretKeys.h by construction.
inline bool isSecret(const QString &key)
{
    const Entry *e = find(key);
    return e ? e->secret : secretkeys::isSecretIniKey(key);
}

// Every registered full key, in table order.
inline QStringList keys()
{
    QStringList out;
    out.reserve(entries().size());
    for (const Entry &e : entries())
        out << QString::fromLatin1(e.key);
    return out;
}

} // namespace noch::settingschema
