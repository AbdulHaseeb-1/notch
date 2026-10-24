#include "AppSecrets.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

#include "SecretKeys.h"

namespace appsecrets {

QString providerStoreKey(const QString &provider)
{
    // Provider ids come from the settings UI's fixed list, but keep the
    // composed key tidy for anything unexpected (SecretStore additionally
    // sanitises it for the file name).
    QString id = provider.trimmed();
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    return QStringLiteral("app/provider/") + id;
}

namespace {

// Moves one plain value into the store. Returns true when the value is in
// the store afterwards (read back), which is the only moment the plain
// copy may be cleared.
bool sweep(QSettings &settings, const QString &iniKey, SecretStore &store, const QString &storeKey)
{
    const QString value = settings.value(iniKey).toString();
    if (value.isEmpty())
        return false;
    // The vault is the source of truth once it holds a value. A settings
    // file restored from an older snapshot must not replace it.
    const QString existing = store.get(storeKey);
    if (!existing.isEmpty()) {
        settings.remove(iniKey);
        return true;
    }
    store.set(storeKey, value);
    if (store.get(storeKey) != value)
        return false;   // the store could not take it: keep the plain copy
    settings.remove(iniKey);
    return true;
}

} // namespace

void migrateLegacySecrets(QSettings &settings, SecretStore &store)
{
    bool touched = false;

    // The fixed keys: INI name -> store key (the flat chat key is handled
    // below, because which provider it belongs to matters).
    const struct { const char *ini; QString store; } fixed[] = {
        { "ui/buddyDgKey", deepgramStoreKey() },
        { "ui/buddyTtsGoogleKey", googleTtsStoreKey() },
        { "ui/buddyLiveEndpointKey", liveEndpointStoreKey() },
        { "ui/buddyMemKey", memStoreKey() },
        { "ui/buddyMemEmbedKey", memEmbedStoreKey() },
    };
    for (const auto &f : fixed)
        touched = sweep(settings, QString::fromLatin1(f.ini), store, f.store) || touched;

    // The per-provider chat configs: the JSON keeps bases and models, but
    // old files embed each provider's key in it. Sweep the keys out by
    // provider id and write the JSON back key-free.
    const QString configsKey = noch::secretkeys::providerConfigsIniKey();
    const QString configsJson = settings.value(configsKey).toString();
    if (!configsJson.isEmpty()) {
        QJsonObject root = QJsonDocument::fromJson(configsJson.toUtf8()).object();
        bool jsonChanged = false;
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (!it.value().isObject())
                continue;
            QJsonObject entry = it.value().toObject();
            if (!entry.contains(QStringLiteral("key")))
                continue;
            const QString key = entry.value(QStringLiteral("key")).toString();
            // Per entry: the field is stripped only once this entry's key
            // is safely in the store (or there was no key to keep). A key
            // the vault already holds is kept as-is.
            bool landed = key.isEmpty();
            if (!key.isEmpty()) {
                const QString storeKey = providerStoreKey(it.key());
                const QString existing = store.get(storeKey);
                if (!existing.isEmpty()) {
                    landed = true;
                } else {
                    store.set(storeKey, key);
                    landed = store.get(storeKey) == key;
                }
            }
            if (landed) {
                entry.remove(QStringLiteral("key"));
                it.value() = entry;
                jsonChanged = true;
            }
        }
        if (jsonChanged) {
            settings.setValue(configsKey,
                              QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
            touched = true;
        }
    }

    // The flat chat key belongs to the provider that was current when the
    // old version last ran. Attribute it when that provider is known and
    // has no key yet (the JSON sweep above is the fuller record); otherwise
    // park it where nothing sends it, rather than guess a provider and
    // risk mailing one service's key to another.
    const QString chatIni = QStringLiteral("ui/buddyApiKey");
    const QString chatKey = settings.value(chatIni).toString();
    if (!chatKey.isEmpty()) {
        const QString provider = settings.value(QStringLiteral("ui/buddyProvider")).toString();
        QString target;
        if (!provider.isEmpty() && provider != QLatin1String("none")
            && store.get(providerStoreKey(provider)).isEmpty())
            target = providerStoreKey(provider);
        else if (!provider.isEmpty() && provider != QLatin1String("none")
                 && store.get(providerStoreKey(provider)) == chatKey)
            target = providerStoreKey(provider);   // already there (JSON sweep): just clear
        else
            target = legacyChatStoreKey();
        const QString existing = store.get(target);
        if (!existing.isEmpty()) {
            settings.remove(chatIni);
            touched = true;
        } else {
            store.set(target, chatKey);
            if (store.get(target) == chatKey) {
                settings.remove(chatIni);
                touched = true;
            }
        }
    }

    if (touched)
        settings.sync();
}

} // namespace appsecrets

AppSecrets::AppSecrets(SecretStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store ? store : &m_fallback)
{
}

QString AppSecrets::read(const QString &storeKey) const
{
    return m_store->get(storeKey);
}

void AppSecrets::write(const QString &storeKey, const QString &value)
{
    if (m_store->get(storeKey) == value)
        return;   // no change: no disk write, no binding churn
    const bool stored = m_store->set(storeKey, value);
    // A persistent store that refused the write still holds the previous
    // value. Leave the bindings where they are so the UI does not pretend
    // the new key was saved.
    if (!stored && m_store->persistent() && m_store->get(storeKey) != value) {
        qWarning("noch: could not save a secret to the vault. The previous value was kept.");
        return;
    }
    if (m_store->get(storeKey) != value)
        return;
    ++m_revision;
    emit changed();
}

QString AppSecrets::providerKey(const QString &provider) const
{
    if (provider.trimmed().isEmpty())
        return QString();
    return read(appsecrets::providerStoreKey(provider));
}

void AppSecrets::setProviderKey(const QString &provider, const QString &key)
{
    if (provider.trimmed().isEmpty())
        return;
    write(appsecrets::providerStoreKey(provider), key);
}
