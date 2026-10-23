// Secrets for connectors (OAuth tokens, client secrets, MCP tokens).
//
// Never plain text on disk and never in QSettings. On Windows each value is
// encrypted with DPAPI (CryptProtectData, current-user scope): the file is useless
// on another account or computer. Every value is bound to its key through the
// entropy argument, so a blob copied over another key does not decrypt. Without
// a storage folder, or where DPAPI does not exist, values live in memory only and
// persistent() says so (the user then signs in again next time).
#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace secrets {

// DPAPI wrappers. Empty on failure (or off Windows).
QByteArray protect(const QByteArray &plain, const QByteArray &entropy);
QByteArray unprotect(const QByteArray &blob, const QByteArray &entropy);
// True when values can be encrypted at rest on this system.
bool encryptionAvailable();

} // namespace secrets

class SecretStore
{
public:
    // `dir` empty = memory only.
    explicit SecretStore(const QString &dir = QString());

    void setDir(const QString &dir);
    QString dir() const { return m_dir; }
    // Values survive a restart (a folder is set and encryption works).
    bool persistent() const;

    // An empty value removes the key. False when the value could not be kept on disk
    // (it is still held in memory for this run).
    bool set(const QString &key, const QString &value);
    QString get(const QString &key) const;
    bool contains(const QString &key) const { return !get(key).isEmpty(); }
    void remove(const QString &key) { set(key, QString()); }
    // Removes every key that starts with `prefix`.
    void removePrefix(const QString &prefix);

private:
    QString fileFor(const QString &key) const;
    QString m_dir;
    mutable QHash<QString, QString> m_cache;   // decrypted values read so far / written this run
};
