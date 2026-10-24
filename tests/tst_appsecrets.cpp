// AppSecrets, the legacy-secret migration and the snapshot exclusion:
// API keys move out of the plain settings file into the SecretStore, and
// the plain-text state snapshot never copies a credential back out.
//
// Everything here is platform-independent on purpose: a memory-only
// SecretStore already takes values (held for the run), so the migration's
// move-and-clear behaviour is fully testable without DPAPI.
#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AppSecrets.h"
#include "DockController.h"
#include "SecretKeys.h"
#include "SecretStore.h"

namespace {

QSettings tempIni(const QTemporaryDir &dir)
{
    return QSettings(dir.filePath(QStringLiteral("noch.ini")), QSettings::IniFormat);
}

} // namespace

class TstAppSecrets : public QObject
{
    Q_OBJECT
private slots:

    // ---- the bridge ----------------------------------------------------

    void bridgeReadsWritesAndSignals()
    {
        SecretStore store;   // memory only
        AppSecrets secrets(&store);
        QSignalSpy changed(&secrets, &AppSecrets::changed);
        const int rev0 = secrets.revision();

        QVERIFY(secrets.deepgramKey().isEmpty());
        secrets.setDeepgramKey(QStringLiteral("dg-key"));
        QCOMPARE(secrets.deepgramKey(), QStringLiteral("dg-key"));
        QCOMPARE(store.get(appsecrets::deepgramStoreKey()), QStringLiteral("dg-key"));
        QCOMPARE(secrets.revision(), rev0 + 1);
        QCOMPARE(changed.size(), 1);

        // Setting the same value again is a no-op: no churn for bindings
        // that write per keystroke.
        secrets.setDeepgramKey(QStringLiteral("dg-key"));
        QCOMPARE(secrets.revision(), rev0 + 1);
        QCOMPARE(changed.size(), 1);

        secrets.setMemKey(QStringLiteral("m0-key"));
        QCOMPARE(secrets.memKey(), QStringLiteral("m0-key"));
        secrets.setMemKey(QString());
        QVERIFY(secrets.memKey().isEmpty());
        QVERIFY(!store.contains(appsecrets::memStoreKey()));
    }

    void bridgeProviderKeys()
    {
        SecretStore store;
        AppSecrets secrets(&store);
        QVERIFY(!secrets.hasProviderKey(QStringLiteral("openai")));
        secrets.setProviderKey(QStringLiteral("openai"), QStringLiteral("sk-1"));
        QVERIFY(secrets.hasProviderKey(QStringLiteral("openai")));
        QCOMPARE(secrets.providerKey(QStringLiteral("openai")), QStringLiteral("sk-1"));
        QCOMPARE(secrets.providerKey(QStringLiteral("anthropic")), QString());
        // No collision with the connector token namespaces.
        QVERIFY(store.get(QStringLiteral("gmail.tokens")).isEmpty());
        // An empty provider names nothing.
        secrets.setProviderKey(QString(), QStringLiteral("sk-x"));
        QCOMPARE(secrets.providerKey(QString()), QString());
        secrets.setProviderKey(QStringLiteral("openai"), QString());
        QVERIFY(!secrets.hasProviderKey(QStringLiteral("openai")));
    }

    void bridgeSurvivesANullStore()
    {
        AppSecrets secrets(nullptr);
        secrets.setGoogleTtsKey(QStringLiteral("g-key"));
        QCOMPARE(secrets.googleTtsKey(), QStringLiteral("g-key"));
    }

    // ---- the migration -------------------------------------------------

    void migrationMovesTheFlatKeys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store;
        {
            QSettings ini = tempIni(dir);
            ini.setValue(QStringLiteral("ui/buddyProvider"), QStringLiteral("openai"));
            ini.setValue(QStringLiteral("ui/buddyApiKey"), QStringLiteral("sk-chat"));
            ini.setValue(QStringLiteral("ui/buddyDgKey"), QStringLiteral("dg-key"));
            ini.setValue(QStringLiteral("ui/buddyTtsGoogleKey"), QStringLiteral("g-key"));
            ini.setValue(QStringLiteral("ui/buddyLiveEndpointKey"), QStringLiteral("ep-key"));
            ini.setValue(QStringLiteral("ui/buddyMemKey"), QStringLiteral("m0-key"));
            ini.setValue(QStringLiteral("ui/buddyMemEmbedKey"), QStringLiteral("emb-key"));
            ini.setValue(QStringLiteral("ui/buddyModel"), QStringLiteral("gpt-4o-mini"));
            ini.sync();
        }
        {
            QSettings ini = tempIni(dir);
            appsecrets::migrateLegacySecrets(ini, store);
        }
        QCOMPARE(store.get(appsecrets::deepgramStoreKey()), QStringLiteral("dg-key"));
        QCOMPARE(store.get(appsecrets::googleTtsStoreKey()), QStringLiteral("g-key"));
        QCOMPARE(store.get(appsecrets::liveEndpointStoreKey()), QStringLiteral("ep-key"));
        QCOMPARE(store.get(appsecrets::memStoreKey()), QStringLiteral("m0-key"));
        QCOMPARE(store.get(appsecrets::memEmbedStoreKey()), QStringLiteral("emb-key"));
        // The flat chat key belongs to the provider that was current.
        QCOMPARE(store.get(appsecrets::providerStoreKey(QStringLiteral("openai"))),
                 QStringLiteral("sk-chat"));

        // And the plain file is clean (a fresh reader sees the removals),
        // while ordinary settings are untouched.
        QSettings ini = tempIni(dir);
        for (const QString &key : noch::secretkeys::iniKeys())
            QVERIFY2(!ini.contains(key), qPrintable(key));
        QCOMPARE(ini.value(QStringLiteral("ui/buddyModel")).toString(),
                 QStringLiteral("gpt-4o-mini"));
        QCOMPARE(ini.value(QStringLiteral("ui/buddyProvider")).toString(),
                 QStringLiteral("openai"));
    }

    void migrationSweepsTheProviderConfigsJson()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store;
        const QString json = QStringLiteral(
            "{\"openai\":{\"key\":\"sk-oa\",\"base\":\"https://api.openai.com/v1\","
            "\"model\":\"gpt-4o\",\"models\":[\"gpt-4o\"]},"
            "\"anthropic\":{\"key\":\"\",\"base\":\"https://api.anthropic.com\"}}");
        {
            QSettings ini = tempIni(dir);
            ini.setValue(QStringLiteral("ui/buddyProvider"), QStringLiteral("anthropic"));
            ini.setValue(QStringLiteral("ui/buddyApiKey"), QStringLiteral("sk-ant"));
            ini.setValue(noch::secretkeys::providerConfigsIniKey(), json);
            ini.sync();
        }
        {
            QSettings ini = tempIni(dir);
            appsecrets::migrateLegacySecrets(ini, store);
        }
        QCOMPARE(store.get(appsecrets::providerStoreKey(QStringLiteral("openai"))),
                 QStringLiteral("sk-oa"));
        // Anthropic's JSON key was empty; the flat key is attributed to it.
        QCOMPARE(store.get(appsecrets::providerStoreKey(QStringLiteral("anthropic"))),
                 QStringLiteral("sk-ant"));

        QSettings ini = tempIni(dir);
        const QJsonObject root = QJsonDocument::fromJson(
            ini.value(noch::secretkeys::providerConfigsIniKey()).toString().toUtf8()).object();
        const QJsonObject openai = root.value(QStringLiteral("openai")).toObject();
        QVERIFY(!openai.contains(QStringLiteral("key")));
        QCOMPARE(openai.value(QStringLiteral("base")).toString(),
                 QStringLiteral("https://api.openai.com/v1"));
        QCOMPARE(openai.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-4o"));
        QCOMPARE(openai.value(QStringLiteral("models")).toArray().size(), 1);
        QVERIFY(!root.value(QStringLiteral("anthropic")).toObject().contains(QStringLiteral("key")));
        QVERIFY(!ini.contains(QStringLiteral("ui/buddyApiKey")));
    }

    void migrationDoesNotResurrectClearedKeys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store;
        {
            QSettings ini = tempIni(dir);
            ini.setValue(QStringLiteral("ui/buddyDgKey"), QStringLiteral("dg-key"));
            ini.sync();
        }
        {
            QSettings ini = tempIni(dir);
            appsecrets::migrateLegacySecrets(ini, store);
        }
        QCOMPARE(store.get(appsecrets::deepgramStoreKey()), QStringLiteral("dg-key"));
        // The user clears the key in the vault...
        store.remove(appsecrets::deepgramStoreKey());
        // ...and the next start's migration must not bring it back: the
        // plain value it would migrate from is already gone.
        {
            QSettings ini = tempIni(dir);
            appsecrets::migrateLegacySecrets(ini, store);
        }
        QVERIFY(store.get(appsecrets::deepgramStoreKey()).isEmpty());
    }

    void migrationParksAnUnattributableChatKey()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store;
        {
            QSettings ini = tempIni(dir);
            ini.setValue(QStringLiteral("ui/buddyProvider"), QStringLiteral("none"));
            ini.setValue(QStringLiteral("ui/buddyApiKey"), QStringLiteral("sk-orphan"));
            ini.sync();
        }
        {
            QSettings ini = tempIni(dir);
            appsecrets::migrateLegacySecrets(ini, store);
        }
        // Kept, but parked where nothing attributes it to a provider.
        QCOMPARE(store.get(appsecrets::legacyChatStoreKey()), QStringLiteral("sk-orphan"));
        QVERIFY(store.get(appsecrets::providerStoreKey(QStringLiteral("openai"))).isEmpty());
        QSettings ini = tempIni(dir);
        QVERIFY(!ini.contains(QStringLiteral("ui/buddyApiKey")));
    }

    void migrationLeavesAnEmptyFileAlone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SecretStore store;
        QSettings ini = tempIni(dir);
        ini.setValue(QStringLiteral("ui/buddyModel"), QStringLiteral("gpt-4o-mini"));
        ini.sync();
        appsecrets::migrateLegacySecrets(ini, store);
        QVERIFY(store.get(appsecrets::deepgramStoreKey()).isEmpty());
        QCOMPARE(ini.value(QStringLiteral("ui/buddyModel")).toString(),
                 QStringLiteral("gpt-4o-mini"));
    }

    // ---- the snapshot exclusion ----------------------------------------

    void snapshotNeverContainsSecrets()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings ini = tempIni(dir);
        ini.setValue(QStringLiteral("ui/buddyApiKey"), QStringLiteral("sk-chat"));
        ini.setValue(QStringLiteral("ui/buddyDgKey"), QStringLiteral("dg-key"));
        ini.setValue(QStringLiteral("ui/buddyTtsGoogleKey"), QStringLiteral("g-key"));
        ini.setValue(QStringLiteral("ui/buddyLiveEndpointKey"), QStringLiteral("ep-key"));
        ini.setValue(QStringLiteral("ui/buddyMemKey"), QStringLiteral("m0-key"));
        ini.setValue(QStringLiteral("ui/buddyMemEmbedKey"), QStringLiteral("emb-key"));
        ini.setValue(noch::secretkeys::providerConfigsIniKey(),
                     QStringLiteral("{\"openai\":{\"key\":\"sk-oa\"}}"));
        ini.setValue(QStringLiteral("ui/buddyModel"), QStringLiteral("gpt-4o-mini"));
        ini.setValue(QStringLiteral("ui/buddyProvider"), QStringLiteral("openai"));
        ini.setValue(QStringLiteral("player/volume"), 42);
        ini.setValue(QStringLiteral("ui/buddyDisabledSkills"),
                     QStringList{ QStringLiteral("a"), QStringLiteral("b") });
        ini.sync();

        const QJsonObject snap = DockController::settingsSnapshot(ini);
        for (const QString &key : noch::secretkeys::iniKeys())
            QVERIFY2(!snap.contains(key), qPrintable(key));
        QVERIFY(!snap.contains(noch::secretkeys::providerConfigsIniKey()));
        // Ordinary settings survive, with their shapes intact.
        QCOMPARE(snap.value(QStringLiteral("ui/buddyModel")).toString(),
                 QStringLiteral("gpt-4o-mini"));
        QCOMPARE(snap.value(QStringLiteral("ui/buddyProvider")).toString(),
                 QStringLiteral("openai"));
        QCOMPARE(snap.value(QStringLiteral("player/volume")).toInt(), 42);
        QCOMPARE(snap.value(QStringLiteral("ui/buddyDisabledSkills")).toArray().size(), 2);
        // The snapshot must not leak through values either.
        const QString dumped = QString::fromUtf8(QJsonDocument(snap).toJson());
        QVERIFY(!dumped.contains(QStringLiteral("sk-chat")));
        QVERIFY(!dumped.contains(QStringLiteral("sk-oa")));
        QVERIFY(!dumped.contains(QStringLiteral("dg-key")));
    }

    void secretKeyPredicateAgreesWithTheList()
    {
        for (const QString &key : noch::secretkeys::iniKeys())
            QVERIFY(noch::secretkeys::isSecretIniKey(key));
        QVERIFY(noch::secretkeys::isSecretIniKey(noch::secretkeys::providerConfigsIniKey()));
        QVERIFY(!noch::secretkeys::isSecretIniKey(QStringLiteral("ui/buddyModel")));
        QVERIFY(!noch::secretkeys::isSecretIniKey(QStringLiteral("ui/buddyMemBackend")));
        QVERIFY(!noch::secretkeys::isSecretIniKey(QStringLiteral("dock/pinned")));
    }
};

QTEST_MAIN(TstAppSecrets)
#include "tst_appsecrets.moc"
