// The QSettings keys that hold credentials.
//
// API keys used to live in the plain INI settings file (and were copied into
// the noch-state.json snapshot). They now live in the SecretStore (DPAPI,
// see src/agent/SecretStore.h); these names are the single source of truth
// for what counts as a secret in settings, shared by the startup migration
// (src/agent/AppSecrets.cpp, which sweeps legacy values into the store) and
// the state snapshot (DockController, which never writes them out).
#pragma once

#include <QString>
#include <QStringList>

namespace noch::secretkeys {

// The INI keys whose values are credentials, in "group/name" QSettings form.
inline const QStringList &iniKeys()
{
    static const QStringList keys = {
        QStringLiteral("ui/buddyApiKey"),           // chat provider key (current provider)
        QStringLiteral("ui/buddyDgKey"),            // Deepgram (STT, TTS and live agent)
        QStringLiteral("ui/buddyTtsGoogleKey"),     // Google spoken answers
        QStringLiteral("ui/buddyLiveEndpointKey"), // live agent's own "think" endpoint
        QStringLiteral("ui/buddyMemKey"),          // Mem0 service key
        QStringLiteral("ui/buddyMemEmbedKey"),      // Mem0 embedder key
    };
    return keys;
}

// Per-provider chat configs as JSON. The JSON itself stays in settings
// (bases, models), but older files embed each provider's API key in it,
// so the whole value is treated as secret-bearing: the migration sweeps
// the keys into the store and rewrites it key-free, and snapshots omit it
// until then rather than risk copying a key out.
inline QString providerConfigsIniKey()
{
    return QStringLiteral("ui/buddyProviderConfigs");
}

// True when a settings key must never be copied into a snapshot or backup.
inline bool isSecretIniKey(const QString &key)
{
    return key == providerConfigsIniKey() || iniKeys().contains(key);
}

} // namespace noch::secretkeys
