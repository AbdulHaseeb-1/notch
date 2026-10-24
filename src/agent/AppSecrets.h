// AppSecrets: the QML-facing door to the SecretStore for Noch's own API keys.
//
// The chat provider key, the Deepgram key, the Google spoken-answers key, the
// live agent's endpoint key and the Mem0 keys used to be plain properties in
// the QML settings store, i.e. plain text in noch.ini (and in every state
// snapshot). They now live in the same DPAPI-backed SecretStore the connectors
// use for their tokens, under "app/..." keys (the connector token keys are
// "gmail.*", "github.*", "mcp.*", so nothing can collide). Settings pages
// read/write through this bridge; the values never touch QSettings again.
//
// The C++ consumers (BuddyProvider, SpeechOut, VoiceInput, DeepgramAgent)
// keep receiving their keys through their existing Q_PROPERTYs — only the
// QML bindings' source changed, from the settings store to this bridge.
#pragma once

#include <QObject>
#include <QString>

#include "SecretStore.h"

class QSettings;

namespace appsecrets {

// Store keys for the fixed (not per-provider) app secrets.
inline QString deepgramStoreKey() { return QStringLiteral("app/deepgram"); }
inline QString googleTtsStoreKey() { return QStringLiteral("app/googleTts"); }
inline QString liveEndpointStoreKey() { return QStringLiteral("app/liveEndpoint"); }
inline QString memStoreKey() { return QStringLiteral("app/mem"); }
inline QString memEmbedStoreKey() { return QStringLiteral("app/memEmbed"); }
// The chat key of the provider that was current in an old settings file but
// cannot be attributed to a provider (kept, never sent anywhere).
inline QString legacyChatStoreKey() { return QStringLiteral("app/chatKeyLegacy"); }
QString providerStoreKey(const QString &provider);

// One startup sweep: move every credential still sitting in plain settings
// (the keys listed in src/common/SecretKeys.h, plus the per-provider keys
// embedded in the provider-configs JSON) into `store`, and clear the plain
// values. Only fires on values that are actually there, so it is safe to run
// on every start: a key the user later clears in the store stays cleared,
// because the settings value it would migrate from is already gone. A plain
// value is only cleared once the store reads the value back, so a secret is
// never lost to a store that could not take it.
void migrateLegacySecrets(QSettings &settings, SecretStore &store);

} // namespace appsecrets

class AppSecrets : public QObject
{
    Q_OBJECT
    // The fixed keys, one property each so QML bindings track changes.
    Q_PROPERTY(QString deepgramKey READ deepgramKey WRITE setDeepgramKey NOTIFY changed)
    Q_PROPERTY(QString googleTtsKey READ googleTtsKey WRITE setGoogleTtsKey NOTIFY changed)
    Q_PROPERTY(QString liveEndpointKey READ liveEndpointKey WRITE setLiveEndpointKey NOTIFY changed)
    Q_PROPERTY(QString memKey READ memKey WRITE setMemKey NOTIFY changed)
    Q_PROPERTY(QString memEmbedKey READ memEmbedKey WRITE setMemEmbedKey NOTIFY changed)
    // Bumps on every change: bindings that call providerKey() (a plain
    // function, which QML cannot track) list it as a dependency so they
    // re-read the vault when it changes.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // `store` is the connectors' store (owned by ConnectorRegistry); it must
    // outlive the bridge. A null store falls back to a private memory-only
    // one, so QML never meets a dead bridge.
    explicit AppSecrets(SecretStore *store, QObject *parent = nullptr);

    QString deepgramKey() const { return read(appsecrets::deepgramStoreKey()); }
    void setDeepgramKey(const QString &value) { write(appsecrets::deepgramStoreKey(), value); }
    QString googleTtsKey() const { return read(appsecrets::googleTtsStoreKey()); }
    void setGoogleTtsKey(const QString &value) { write(appsecrets::googleTtsStoreKey(), value); }
    QString liveEndpointKey() const { return read(appsecrets::liveEndpointStoreKey()); }
    void setLiveEndpointKey(const QString &value) { write(appsecrets::liveEndpointStoreKey(), value); }
    QString memKey() const { return read(appsecrets::memStoreKey()); }
    void setMemKey(const QString &value) { write(appsecrets::memStoreKey(), value); }
    QString memEmbedKey() const { return read(appsecrets::memEmbedStoreKey()); }
    void setMemEmbedKey(const QString &value) { write(appsecrets::memEmbedStoreKey(), value); }

    int revision() const { return m_revision; }

    // Per-provider chat keys ("openai", "anthropic", ...), in and out of the
    // store. An empty provider or key clears/reads nothing.
    Q_INVOKABLE QString providerKey(const QString &provider) const;
    Q_INVOKABLE void setProviderKey(const QString &provider, const QString &key);
    Q_INVOKABLE bool hasProviderKey(const QString &provider) const
    {
        return !providerKey(provider).isEmpty();
    }

signals:
    void changed();

private:
    QString read(const QString &storeKey) const;
    void write(const QString &storeKey, const QString &value);

    SecretStore *m_store = nullptr;   // not owned (ConnectorRegistry's)
    SecretStore m_fallback;           // used only when constructed with null
    int m_revision = 0;
};
