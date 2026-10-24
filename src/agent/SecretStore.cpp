#include "SecretStore.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>

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
    // DPAPI can fail for a moment at logon, before the user profile's master
    // key is ready (launch-at-startup). A bad blob fails immediately; those
    // profile errors are retried so a sign-in is not reported as missing.
    for (int attempt = 0; attempt < 3; ++attempt) {
        DATA_BLOB in{ DWORD(blob.size()), reinterpret_cast<BYTE *>(const_cast<char *>(blob.constData())) };
        DATA_BLOB ent{ DWORD(entropy.size()), reinterpret_cast<BYTE *>(const_cast<char *>(entropy.constData())) };
        DATA_BLOB out{ 0, nullptr };
        if (CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            const QByteArray plain(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
            SecureZeroMemory(out.pbData, out.cbData);
            LocalFree(out.pbData);
            return plain;
        }
        const DWORD err = GetLastError();
        const bool retry = err == 0x8009000Bu || err == 0x80090016u || err == ERROR_BUSY;
        if (!retry || attempt == 2)
            return QByteArray();
        QThread::msleep(50);
    }
    return QByteArray();
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

namespace {

QString backupPath(const QString &path)
{
    return path + QStringLiteral(".bak");
}

QByteArray entropyFor(const QString &key)
{
    return secrets::kEntropyPrefix + key.toUtf8();
}

QString unlockFile(const QString &path, const QString &key)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() <= 0 || f.size() > (1 << 20))
        return QString();
    const QByteArray plain = secrets::unprotect(f.readAll(), entropyFor(key));
    if (plain.isEmpty())
        return QString();
    return QString::fromUtf8(plain);
}

bool writeAtomic(const QString &path, const QByteArray &blob)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(blob) != blob.size())
        return false;
    return f.commit();
}

void removeWithBackup(const QString &path)
{
    QFile::remove(path);
    QFile::remove(backupPath(path));
}

} // namespace

bool SecretStore::set(const QString &key, const QString &value)
{
    if (key.isEmpty())
        return false;
    if (value.isEmpty()) {
        m_cache.remove(key);
        if (!m_dir.isEmpty())
            removeWithBackup(fileFor(key));
        return true;
    }
    // Nowhere safe to put it: keep it for this run and say so.
    if (!persistent()) {
        m_cache.insert(key, value);
        return false;
    }
    QDir().mkpath(m_dir);
    const QByteArray blob = secrets::protect(value.toUtf8(), entropyFor(key));
    if (blob.isEmpty())
        return false;
    const QString path = fileFor(key);
    // Only a file that still unlocks is worth keeping. Copying a torn file
    // over the backup would destroy the last good copy.
    if (!unlockFile(path, key).isEmpty()) {
        const QString bak = backupPath(path);
        QFile::remove(bak);
        QFile::copy(path, bak);
    }
    if (!writeAtomic(path, blob))
        return false;
    m_cache.insert(key, value);
    return true;
}

QString SecretStore::get(const QString &key) const
{
    const auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd())
        return it.value();
    if (!persistent() || key.isEmpty())
        return QString();
    const QString path = fileFor(key);
    QString value = unlockFile(path, key);
    if (value.isEmpty()) {
        const QString rescued = unlockFile(backupPath(path), key);
        if (!rescued.isEmpty()) {
            qWarning("noch: saved secret \"%s\" could not be unlocked; restored the previous copy",
                     qUtf8Printable(key));
            QFile::remove(path);
            QFile::copy(backupPath(path), path);
            value = rescued;
        } else if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
            qWarning("noch: saved secret \"%s\" could not be unlocked. The file was left in place.",
                     qUtf8Printable(key));
        }
    }
    if (!value.isEmpty())
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
    const QStringList files = QDir(m_dir).entryList(
        { p + QStringLiteral("*.sec"), p + QStringLiteral("*.sec.bak") }, QDir::Files);
    for (const QString &f : files)
        QFile::remove(QDir(m_dir).filePath(f));
}
