// Spoken reports: when it speaks, what it says, and that nothing in a reply can run as a command.
#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "SpeechOut.h"

class TstSpeechOut : public QObject
{
    Q_OBJECT

    // A "voice" that writes what it is told to a file.
    static void useRecorder(SpeechOut &s, const QString &file, const QString &script = QString()) {
        s.setCommand(QStringLiteral("sh"), { QStringLiteral("-c"), script.isEmpty() ? QStringLiteral("cat > '%1'").arg(file) : script });
    }
    static QByteArray read(const QString &file) {
        QFile f(file);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QSKIP("These tests use a POSIX shell as the stand-in voice.");
#endif
    }

    void offByDefaultSaysNothing() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        QVERIFY(!s.enabled());
        s.speakReport("Done.");
        s.say("Hello");
        QTest::qWait(150);
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!s.speaking());
    }

    void speaksASummaryOfTheReply() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.speakReport("**Done.** I made the file.\n```\nlong code block\n```\nSee https://example.com for more.");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        const QString said = QString::fromUtf8(read(out));
        QVERIFY2(said.contains("Done. I made the file."), qPrintable(said));
        QVERIFY2(!said.contains("long code block") && !said.contains("https") && !said.contains("*"), qPrintable(said));
    }

    void sayIsExactlyTheShortLine() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say("I need your approval to continue.");
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QStringLiteral("I need your approval to continue."));
    }

    void textIsDataNotACommand() {
        // The reply goes in on stdin. Shell syntax in it is read aloud as words, never run.
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        const QString trap = dir.filePath("PWNED");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say(QStringLiteral("$(touch '%1'); `touch '%1'`; touch '%1' && echo \"x\" 'y'").arg(trap));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QVERIFY(!QFileInfo::exists(trap));
        QVERIFY(QString::fromUtf8(read(out)).contains("touch"));
    }

    void unicodeSurvives() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.say(QString::fromUtf8("Fertig \xc3\xa4\xc3\xb6\xc3\xbc \xe2\x80\x94 caf\xc3\xa9"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(out) && !s.speaking(), 3000);
        QCOMPARE(QString::fromUtf8(read(out)), QString::fromUtf8("Fertig \xc3\xa4\xc3\xb6\xc3\xbc \xe2\x80\x94 caf\xc3\xa9"));
    }

    void speakingIsReportedAndStopCutsIt() {
        SpeechOut s;
        s.setCommand("sh", { "-c", "sleep 5" });
        s.setEnabled(true);
        QSignalSpy spy(&s, &SpeechOut::speakingChanged);
        s.say("A long report");
        QTRY_VERIFY(s.speaking());
        QVERIFY(spy.count() >= 1);
        s.stop();
        QVERIFY(!s.speaking());
    }

    void aNewReportReplacesTheOldOne() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        // Records, then lingers, so the first is still "speaking" when the second arrives.
        s.setCommand("sh", { "-c", QStringLiteral("cat >> '%1'; sleep 5").arg(out) });
        s.setEnabled(true);
        s.say("first report");
        QTRY_VERIFY(s.speaking());
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromUtf8(read(out)), QStringLiteral("first report"), 3000);
        s.say("second report");
        QTRY_VERIFY_WITH_TIMEOUT(QString::fromUtf8(read(out)).contains("second report"), 3000);
        s.stop();
    }

    void turningItOffCutsTheVoiceAtOnce() {
        SpeechOut s;
        s.setCommand("sh", { "-c", "sleep 5" });
        s.setEnabled(true);
        s.say("Hello there");
        QTRY_VERIFY(s.speaking());
        s.setEnabled(false);
        QVERIFY(!s.speaking());
    }

    void noVoiceInstalledIsUnavailableNotAnError() {
        SpeechOut s;
        s.setCommand(QString(), {});
        s.setEnabled(true);
        QVERIFY(!s.available());
        s.speakReport("Done.");       // quietly nothing
        QVERIFY(!s.speaking());
    }

    void aMissingProgramDoesNotCrash() {
        SpeechOut s;
        s.setCommand("/nonexistent/voice", {});
        s.setEnabled(true);
        s.say("Hello");
        QTest::qWait(150);
        QVERIFY(!s.speaking());
    }

    void emptyOrCodeOnlyReplyIsNotSpoken() {
        QTemporaryDir dir;
        const QString out = dir.filePath("said.txt");
        SpeechOut s;
        useRecorder(s, out);
        s.setEnabled(true);
        s.speakReport("```\nonly code\n```");
        s.speakReport("   ");
        QTest::qWait(150);
        QVERIFY(!QFileInfo::exists(out));
    }
};

QTEST_MAIN(TstSpeechOut)
#include "tst_speechout.moc"
