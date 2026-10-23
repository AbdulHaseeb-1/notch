#include "SecretStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace secrets {

namespace {
const QByteArray kEntropyPrefix = QByteArrayLiteral("noch-connectors-v1:");
}

#ifdef Q_OS_WIN

QByteArray protect(const QByteArray &plain, const QByteArray &entropy)
{
    DATA_BLOB in{ DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData())) };
    DATA_BLOB ent{ DWORD(entropy.size()), reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData())) };
    DATA_BLOB out{ 0, nullptr };
    if (!CryptProtectData(&in, L"Noch connector secret", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return QByteArray();
    const QByteArray blob(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return blob;
}

QByteArray unprotect(const QByteArray &blob, const QByteArray &entropy)
{
    if (blob.isEmpty())
        return QByteArray();
    DATA_BLOB in{ DWORD(blob.size()), reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData())) };
    DATA_BLOB ent{ DWORD(entropy.size()), reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData())) };
    DATA_BLOB out{ 0, nullptr };
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return QByteArray();
    const QByteArray plain(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

bool encryptionAvailable() { return true; }

#else

QByteArray protect(const QByteArray &, const QByteArray &) { return QByteArray(); }
QByteArray unprotect(const QByteArray &, const QByteArray &) { return QByteArray(); }
bool encryptionAvailable() { return false; }

#endif

} // namespace secrets

SecretStore::SecretStore(const QString &dir)
    : m_dir(dir)
{
}

void SecretStore::setDir(const QString &dir)
{
    if (dir == m_dir)
        return;
    m_dir = dir;
    m_cache.clear();
}

bool SecretStore::persistent() const
{
    return !m_dir.isEmpty() && secrets::encryptionAvailable();
}

QString SecretStore::fileFor(const QString &key) const
{
    // The key names the file, so keep it to a safe alphabet; anything else is hashed in.
    static const QRegularExpression safe(QStringLiteral("[^A-Za-z0-9._-]"));
    QString name = key;
    name.replace(safe, QStringLiteral("_"));
    name += QLatin1Char('-')
          + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    return QDir(m_dir).filePath(name.left(80) + QStringLiteral(".sec"));
}

bool SecretStore::set(const QString &key, const QString &value)
{
    if (key.isEmpty())
        return false;
    if (value.isEmpty()) {
        m_cache.remove(key);
        if (!m_dir.isEmpty())
            QFile::remove(fileFor(key));
        return true;
    }
    m_cache.insert(key, value);
    if (!persistent())
        return false;
    QDir().mkpath(m_dir);
    const QByteArray blob = secrets::protect(value.toUtf8(), secrets::kEntropyPrefix + key.toUtf8());
    if (blob.isEmpty())
        return false;
    QFile f(fileFor(key));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const bool ok = f.write(blob) == blob.size();
    f.close();
    return ok;
}

QString SecretStore::get(const QString &key) const
{
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd())
        return it.value();
    if (!persistent())
        return QString();
    QFile f(fileFor(key));
    if (!f.open(QIODevice::ReadOnly) || f.size() > (1 << 20))
        return QString();
    const QByteArray plain = secrets::unprotect(f.readAll(), secrets::kEntropyPrefix + key.toUtf8());
    if (plain.isEmpty())
        return QString();
    const QString value = QString::fromUtf8(plain);
    m_cache.insert(key, value);
    return value;
}

void SecretStore::removePrefix(const QString &prefix)
{
    const QStringList keys = m_cache.keys();
    for (const QString &k : keys)
        if (k.startsWith(prefix))
            m_cache.remove(k);
    // Files are named after their key, so match on the sanitised prefix.
    if (m_dir.isEmpty())
        return;
    static const QRegularExpression safe(QStringLiteral("[^A-Za-z0-9._-]"));
    QString p = prefix;
    p.replace(safe, QStringLiteral("_"));
    const QStringList files = QDir(m_dir).entryList({ p + QStringLiteral("*.sec") }, QDir::Files);
    for (const QString &f : files)
        QFile::remove(QDir(m_dir).filePath(f));
}
