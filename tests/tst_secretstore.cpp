// SecretStore: values are encrypted at rest (DPAPI on Windows), bound to their key,
// removable, and held in memory only when there is nowhere safe to put them.
#include <QtTest>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "SecretStore.h"

class TstSecretStore : public QObject
{
    Q_OBJECT
private slots:
    void memoryOnlyWithoutAFolder() {
        SecretStore s;
        QVERIFY(!s.persistent());
        QVERIFY(!s.set("a", "value"));   // not kept on disk ...
        QCOMPARE(s.get("a"), QStringLiteral("value"));   // ... but usable this run
        s.remove("a");
        QVERIFY(s.get("a").isEmpty());
    }

    void roundTripIsEncryptedOnDisk() {
        if (!secrets::encryptionAvailable())
            QSKIP("Encryption at rest needs DPAPI (Windows).");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString secret = QStringLiteral("ya29.super-secret-token-ABC123");
        {
            SecretStore s(dir.path());
            QVERIFY(s.persistent());
            QVERIFY(s.set("gmail.tokens", secret));
        }
        // The file never holds the text.
        const QStringList files = QDir(dir.path()).entryList({ "*.sec" }, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile f(QDir(dir.path()).filePath(files.first()));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(!raw.isEmpty());
        QVERIFY(!raw.contains("ya29"));
        // A fresh store (a restart) reads it back.
        SecretStore again(dir.path());
        QCOMPARE(again.get("gmail.tokens"), secret);
    }

    void aBlobCopiedOverAnotherKeyDoesNotDecrypt() {
        if (!secrets::encryptionAvailable())
            QSKIP("Encryption at rest needs DPAPI (Windows).");
        QTemporaryDir dir;
        SecretStore s(dir.path());
        QVERIFY(s.set("one", "first"));
        QVERIFY(s.set("two", "second"));
        const QStringList files = QDir(dir.path()).entryList({ "*.sec" }, QDir::Files, QDir::Name);
        QCOMPARE(files.size(), 2);
        // Swap the two files: each key now points at the other's blob.
        const QString a = QDir(dir.path()).filePath(files.at(0)), b = QDir(dir.path()).filePath(files.at(1));
        QFile fa(a), fb(b);
        QVERIFY(fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly));
        const QByteArray ba = fa.readAll(), bb = fb.readAll();
        fa.close(); fb.close();
        QVERIFY(fa.open(QIODevice::WriteOnly | QIODevice::Truncate) && fb.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fa.write(bb); fb.write(ba);
        fa.close(); fb.close();
        SecretStore fresh(dir.path());
        QVERIFY(fresh.get("one").isEmpty());
        QVERIFY(fresh.get("two").isEmpty());
    }

    void garbageOnDiskReadsAsEmpty() {
        QTemporaryDir dir;
        SecretStore s(dir.path());
        s.set("k", "v");
        for (const QString &f : QDir(dir.path()).entryList({ "*.sec" }, QDir::Files)) {
            QFile file(QDir(dir.path()).filePath(f));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("not a dpapi blob");
        }
        SecretStore fresh(dir.path());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be unlocked")));
        QVERIFY(fresh.get("k").isEmpty());
    }

    void tornFileFallsBackToThePreviousCopy() {
        if (!secrets::encryptionAvailable())
            QSKIP("Encryption at rest needs DPAPI (Windows).");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        {
            SecretStore s(dir.path());
            QVERIFY(s.set("gmail.tokens", "refresh-one"));
            QVERIFY(s.set("gmail.tokens", "refresh-two"));
        }
        const QStringList files = QDir(dir.path()).entryList({ QStringLiteral("*.sec") }, QDir::Files);
        QCOMPARE(files.size(), 1);
        {
            QFile torn(QDir(dir.path()).filePath(files.first()));
            QVERIFY(torn.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(torn.write("torn"), qint64(4));
        }
        SecretStore fresh(dir.path());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("restored the previous copy")));
        QCOMPARE(fresh.get("gmail.tokens"), QStringLiteral("refresh-one"));
        // The torn file was replaced, so the next launch does not warn again.
        SecretStore again(dir.path());
        QCOMPARE(again.get("gmail.tokens"), QStringLiteral("refresh-one"));
        again.remove("gmail.tokens");
        QVERIFY(QDir(dir.path()).entryList({ QStringLiteral("*.sec") }, QDir::Files).isEmpty());
        QVERIFY(QDir(dir.path()).entryList({ QStringLiteral("*.sec.bak") }, QDir::Files).isEmpty());
    }

    void removeAndPrefix() {
        QTemporaryDir dir;
        SecretStore s(dir.path());
        s.set("mcp.a.secrets", "1");
        s.set("mcp.b.secrets", "2");
        s.set("gmail.tokens", "3");
        s.removePrefix("mcp.");
        QVERIFY(s.get("mcp.a.secrets").isEmpty());
        QVERIFY(s.get("mcp.b.secrets").isEmpty());
        QCOMPARE(s.get("gmail.tokens"), QStringLiteral("3"));
        s.remove("gmail.tokens");
        QVERIFY(QDir(dir.path()).entryList({ "*.sec" }, QDir::Files).isEmpty());
    }

    void awkwardKeysAreSafeFileNames() {
        QTemporaryDir dir;
        SecretStore s(dir.path());
        s.set("../../evil/key", "x");
        const QStringList files = QDir(dir.path()).entryList({ "*.sec" }, QDir::Files);
        if (s.persistent())
            QCOMPARE(files.size(), 1);
        QVERIFY(!QFileInfo::exists(QDir(dir.path()).filePath("../../evil")));
        QCOMPARE(s.get("../../evil/key"), QStringLiteral("x"));
    }
};

QTEST_MAIN(TstSecretStore)
#include "tst_secretstore.moc"
