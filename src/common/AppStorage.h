// One place for where Noch keeps its state, and for putting that state back
// when a file is missing or unreadable.
//
// Everything durable lives under one directory (on Windows, %APPDATA%/noch):
//   noch.ini            settings (QSettings, IniFormat)
//   noch.ini.bak        the last settings file that still parsed
//   noch-state.json     the same settings as JSON, written beside the ini
//   connectors/         MCP list and the encrypted secret vault
//
// Call install() once at startup, before any QSettings object is created.
// Paths below can be asked for at any time; they do not touch the disk.
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace noch::storage {

inline QString configRoot()
{
    QString root = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
    if (root.isEmpty())
        root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (root.isEmpty())
        root = QDir(QDir::homePath()).filePath(QStringLiteral(".config"));
    return root;
}

inline QString organization()
{
    const QString name = QCoreApplication::organizationName();
    return name.isEmpty() ? QStringLiteral("noch") : name;
}

inline QString application()
{
    const QString name = QCoreApplication::applicationName();
    return name.isEmpty() ? QStringLiteral("noch") : name;
}

// The directory that holds noch.ini. Matches QSettings after install():
// setPath(configRoot) + UserScope + organization + application.
inline QString dataDir()
{
    return QDir(configRoot()).filePath(organization());
}

inline QString settingsFile()
{
    return QDir(dataDir()).filePath(application() + QStringLiteral(".ini"));
}

inline QString settingsBackupFile()
{
    return settingsFile() + QStringLiteral(".bak");
}

inline QString stateFile()
{
    return QDir(dataDir()).filePath(QStringLiteral("noch-state.json"));
}

inline QString connectorsDir()
{
    return QDir(dataDir()).filePath(QStringLiteral("connectors"));
}

inline QString secretsDir()
{
    return QDir(connectorsDir()).filePath(QStringLiteral("secrets"));
}

// A settings file we are willing to trust: it has at least one key.
inline bool fileLooksLikeSettings(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return f.read(8192).contains('=');
}

// Copies `settingsFile` onto settingsFile + ".bak" when the live file parses.
// A file that does not look like settings is left alone, so a torn ini cannot
// replace the last good backup.
inline bool backupSettingsFile(const QString &path)
{
    if (!fileLooksLikeSettings(path))
        return false;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = in.readAll();
    in.close();
    if (!data.contains('='))
        return false;
    QSaveFile out(path + QStringLiteral(".bak"));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (out.write(data) != data.size())
        return false;
    return out.commit();
}

// When the live settings file is missing or not an ini, put the backup back.
// Returns true when the live file was replaced.
inline bool restoreSettingsBackup(const QString &path)
{
    if (fileLooksLikeSettings(path))
        return false;
    const QString bak = path + QStringLiteral(".bak");
    if (!fileLooksLikeSettings(bak))
        return false;
    QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    return QFile::copy(bak, path);
}

// Copies the "settings" object out of a noch-state.json snapshot into
// `settings`. Returns how many keys were written. The caller decides when
// a snapshot is allowed to fill the live file (install() does that only
// when the dock has never been saved).
inline int importStateSnapshot(QSettings &settings, const QString &snapshotPath)
{
    QFile f(snapshotPath);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QJsonObject saved = QJsonDocument::fromJson(f.readAll()).object()
                                  .value(QStringLiteral("settings")).toObject();
    int written = 0;
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        if (it.value().isArray()) {
            QStringList list;
            for (const QJsonValue &v : it.value().toArray())
                list.append(v.toString());
            settings.setValue(it.key(), list);
        } else {
            settings.setValue(it.key(), it.value().toVariant());
        }
        ++written;
    }
    if (written > 0)
        settings.sync();
    return written;
}

inline bool sameDir(const QString &a, const QString &b)
{
    const Qt::CaseSensitivity sens =
#ifdef Q_OS_WIN
        Qt::CaseInsensitive
#else
        Qt::CaseSensitive
#endif
        ;
    return QString::compare(QDir::cleanPath(a), QDir::cleanPath(b), sens) == 0;
}

// Older builds kept connectors under QStandardPaths::AppDataLocation, which
// is a different folder from the settings directory whenever the organization
// name is set (%APPDATA%/noch/noch/connectors vs %APPDATA%/noch). Copy that
// tree once. Existing files in the new folder are never replaced.
inline void importLegacyConnectors()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;
    const QString oldDir = QDir(base).filePath(QStringLiteral("connectors"));
    const QString newDir = connectorsDir();
    if (sameDir(oldDir, newDir) || !QDir(oldDir).exists())
        return;

    const auto copyMissing = [](const QString &from, const QString &to) {
        if (!QFileInfo::exists(from) || QFileInfo::exists(to))
            return;
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::copy(from, to);
    };
    copyMissing(QDir(oldDir).filePath(QStringLiteral("connectors.json")),
                QDir(newDir).filePath(QStringLiteral("connectors.json")));
    copyMissing(QDir(oldDir).filePath(QStringLiteral("connectors.json.bak")),
                QDir(newDir).filePath(QStringLiteral("connectors.json.bak")));

    const QDir oldSecrets(QDir(oldDir).filePath(QStringLiteral("secrets")));
    if (!oldSecrets.exists())
        return;
    const QString newSecrets = QDir(newDir).filePath(QStringLiteral("secrets"));
    const QStringList names = oldSecrets.entryList(
        { QStringLiteral("*.sec"), QStringLiteral("*.sec.bak") }, QDir::Files);
    for (const QString &name : names)
        copyMissing(oldSecrets.filePath(name), QDir(newSecrets).filePath(name));
}

// Points QSettings at dataDir(), repairs a broken ini from the backup or the
// JSON snapshot, and runs the one-time registry import. Safe to call again.
inline void install()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString root = configRoot();
    if (!root.isEmpty())
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
    QDir().mkpath(dataDir());

    // Repair the bytes on disk before the first QSettings reads them into
    // its cache. A later sync would otherwise write that cache back.
    const QString iniPath = settingsFile();
    restoreSettingsBackup(iniPath);

    QSettings ini;
    if (!ini.value(QStringLiteral("dock/initialized"), false).toBool()
        && QFileInfo::exists(stateFile()))
        importStateSnapshot(ini, stateFile());

    // One-time migration of any existing settings from the Windows Registry.
    if (!ini.value(QStringLiteral("meta/migratedFromRegistry"), false).toBool()) {
        QSettings reg(QSettings::NativeFormat, QSettings::UserScope,
                      organization(), application());
        const QStringList regKeys = reg.allKeys();
        for (const QString &key : regKeys) {
            if (!ini.contains(key))
                ini.setValue(key, reg.value(key));
        }
        ini.setValue(QStringLiteral("meta/migratedFromRegistry"), true);
        ini.sync();
    }

    // Retired voice settings from earlier designs: the pre-Deepgram cloud
    // transcription keys, the speech-to-text engine choice (Deepgram is the
    // only engine now) and the Kokoro spoken-answers settings (that voice is
    // gone; the remaining voices are Google and Deepgram).
    for (const char *key : { "ui/buddyVoiceBase", "ui/buddyVoiceKey", "ui/buddyVoiceModel", "ui/buddyVoiceLanguage",
                             "ui/buddyWakeCloudOk", "ui/buddySttEngine", "ui/buddyTtsKokoroBase",
                             "ui/buddyTtsKokoroVoice" })
        ini.remove(QString::fromLatin1(key));

    // The spoken-answers voice used to default to this computer's own voice
    // (or a Kokoro server); both are gone, so an old choice becomes Google.
    const QString ttsEngine = ini.value(QStringLiteral("ui/buddyTtsEngine")).toString();
    if (ttsEngine == QLatin1String("system") || ttsEngine == QLatin1String("kokoro"))
        ini.setValue(QStringLiteral("ui/buddyTtsEngine"), QStringLiteral("google"));

    // Blur behind was removed (the desktop is never blurred now); drop its switch.
    ini.remove(QStringLiteral("ui/blurBehind"));
    ini.sync();
}

} // namespace noch::storage
