// App storage: the settings file, its backup, and the JSON snapshot.
// install() is not called here — it points QSettings at the real user profile.
#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include "AppStorage.h"

class TstAppStorage : public QObject
{
    Q_OBJECT
private slots:
    void backupRoundTripReplacesATornFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("[dock]\ninitialized=true\n") > 0);
        }
        QVERIFY(noch::storage::backupSettingsFile(ini));
        QVERIFY(QFileInfo::exists(ini + QStringLiteral(".bak")));

        {
            QFile torn(ini);
            QVERIFY(torn.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(torn.write("not an ini") > 0);
        }
        QVERIFY(noch::storage::restoreSettingsBackup(ini));
        QFile back(ini);
        QVERIFY(back.open(QIODevice::ReadOnly));
        QVERIFY(back.readAll().contains("initialized=true"));
    }

    void backupRefusesATornFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));
        QFile f(ini);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.write("torn") > 0);
        f.close();
        QVERIFY(!noch::storage::backupSettingsFile(ini));
        QVERIFY(!QFileInfo::exists(ini + QStringLiteral(".bak")));
    }

    void snapshotFillsSettings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString snap = dir.filePath(QStringLiteral("noch-state.json"));
        {
            QFile f(snap);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("{\"settings\":{\"ui/buddyModel\":\"gpt-4o-mini\",\"dock/initialized\":true}}") > 0);
        }
        QSettings ini(dir.filePath(QStringLiteral("noch.ini")), QSettings::IniFormat);
        QCOMPARE(noch::storage::importStateSnapshot(ini, snap), 2);
        QCOMPARE(ini.value(QStringLiteral("ui/buddyModel")).toString(), QStringLiteral("gpt-4o-mini"));
        QCOMPARE(ini.value(QStringLiteral("dock/initialized")).toBool(), true);
    }

    void restoreLeavesAHealthyFileAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("kept=yes\n") > 0);
        }
        {
            QFile bak(ini + QStringLiteral(".bak"));
            QVERIFY(bak.open(QIODevice::WriteOnly));
            QVERIFY(bak.write("kept=no\n") > 0);
        }
        QVERIFY(!noch::storage::restoreSettingsBackup(ini));
        QFile back(ini);
        QVERIFY(back.open(QIODevice::ReadOnly));
        QVERIFY(back.readAll().contains("kept=yes"));
    }
};

QTEST_MAIN(TstAppStorage)
#include "tst_appstorage.moc"
