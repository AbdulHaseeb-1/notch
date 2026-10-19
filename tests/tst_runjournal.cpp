// Undo for a whole run: snapshots before changes, what counts as a change,
// restoring, conflicts with later edits, folders, wildcards, restarts, pruning.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "../src/AgentPolicy.h"
#include "../src/RunJournal.h"

namespace {

void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(data);
}

QByteArray read(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

} // namespace

class TstRunJournal : public QObject
{
    Q_OBJECT
    QTemporaryDir work;      // the "workspace"
    QTemporaryDir store;     // the journal's own folder
    QString p(const QString &rel) const { return QDir(work.path()).filePath(rel); }

private slots:
    void initTestCase() { QVERIFY(work.isValid() && store.isValid()); }
    void init() {
        QDir(work.path()).removeRecursively();
        QDir(store.path()).removeRecursively();
        QDir().mkpath(work.path());
        QDir().mkpath(store.path());
    }

    void editedFileIsRestored() {
        RunJournal j(store.path());
        write(p("a.txt"), "original");
        j.begin("run1");
        QCOMPARE(j.record(p("a.txt")), RunJournal::Record::Recorded);
        write(p("a.txt"), "changed by buddy");
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("count").toInt(), 1);
        QCOMPARE(s.value("modified").toInt(), 1);
        QCOMPARE(s.value("files").toList().first().toMap().value("action").toString(), QStringLiteral("modified"));
        const RunJournal::UndoResult r = j.undo("run1");
        QVERIFY(r.ok());
        QCOMPARE(r.restored, 1);
        QCOMPARE(read(p("a.txt")), QByteArray("original"));
        QVERIFY(j.summary("run1").isEmpty());                    // nothing left to undo
        QVERIFY(!QDir(store.path()).exists("run1"));             // and the backups are gone
    }

    void createdFileIsRemoved() {
        RunJournal j(store.path());
        j.begin("run1");
        QCOMPARE(j.record(p("new.txt")), RunJournal::Record::Recorded);   // does not exist yet
        write(p("new.txt"), "hello");
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("created").toInt(), 1);
        const RunJournal::UndoResult r = j.undo("run1");
        QCOMPARE(r.removed, 1);
        QVERIFY(!QFileInfo::exists(p("new.txt")));
    }

    void deletedFileComesBackEvenWithItsFolder() {
        RunJournal j(store.path());
        write(p("sub/deep/gone.txt"), "precious");
        j.begin("run1");
        j.record(p("sub/deep/gone.txt"));
        QVERIFY(QDir(p("sub")).removeRecursively());
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("deleted").toInt(), 1);
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("sub/deep/gone.txt")), QByteArray("precious"));
    }

    void theFirstSnapshotWins() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("run1");
        QCOMPARE(j.record(p("a.txt")), RunJournal::Record::Recorded);
        write(p("a.txt"), "v1");
        QCOMPARE(j.record(p("a.txt")), RunJournal::Record::AlreadyRecorded);   // must not snapshot v1
        write(p("a.txt"), "v2");
        j.finish();
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("a.txt")), QByteArray("v0"));
    }

    void unchangedFilesAreNotReportedOrKept() {
        RunJournal j(store.path());
        write(p("a.txt"), "same");
        j.begin("run1");
        j.record(p("a.txt"));
        j.record(p("never-created.txt"));
        QVERIFY(j.finish().isEmpty());
        QVERIFY(!QDir(store.path()).exists("run1"));
        QVERIFY(j.latestWithChanges().isEmpty());
    }

    void binaryFilesSurviveByteForByte() {
        RunJournal j(store.path());
        QByteArray bin;
        for (int i = 0; i < 256; ++i)
            bin.append(char(i));
        bin.append("\r\n\0tail", 7);
        write(p("data.bin"), bin);
        j.begin("run1");
        j.record(p("data.bin"));
        write(p("data.bin"), "text now");
        j.finish();
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("data.bin")), bin);
    }

    // ---- conflicts ---------------------------------------------------------------

    void aFileChangedAfterTheRunIsLeftAlone() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("run1");
        j.record(p("a.txt"));
        write(p("a.txt"), "buddy");
        j.finish();
        write(p("a.txt"), "the user kept typing");
        const RunJournal::UndoResult r = j.undo("run1");
        QVERIFY(!r.ok());
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(r.restored, 0);
        QCOMPARE(read(p("a.txt")), QByteArray("the user kept typing"));
        // Still undoable: forcing it overrides the user's later edit.
        QVERIFY(!j.summary("run1").isEmpty());
        const RunJournal::UndoResult forced = j.undo("run1", true);
        QVERIFY(forced.ok());
        QCOMPARE(read(p("a.txt")), QByteArray("v0"));
    }

    void aCreatedFileTheUserEditedIsNotDeleted() {
        RunJournal j(store.path());
        j.begin("run1");
        j.record(p("new.txt"));
        write(p("new.txt"), "buddy");
        j.finish();
        write(p("new.txt"), "now the user's work");
        const RunJournal::UndoResult r = j.undo("run1");
        QCOMPARE(r.conflicts.size(), 1);
        QVERIFY(QFileInfo::exists(p("new.txt")));
    }

    void aDeletedFileThatCameBackDifferentIsAConflict() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("run1");
        j.record(p("a.txt"));
        QVERIFY(QFile::remove(p("a.txt")));
        j.finish();
        write(p("a.txt"), "someone recreated it");
        QCOMPARE(j.undo("run1").conflicts.size(), 1);
        QCOMPARE(read(p("a.txt")), QByteArray("someone recreated it"));
    }

    void conflictsDoNotStopTheOtherFilesFromBeingRestored() {
        RunJournal j(store.path());
        write(p("ok.txt"), "o0");
        write(p("clash.txt"), "c0");
        j.begin("run1");
        j.record(p("ok.txt"));
        j.record(p("clash.txt"));
        write(p("ok.txt"), "o1");
        write(p("clash.txt"), "c1");
        j.finish();
        write(p("clash.txt"), "c-user");
        const RunJournal::UndoResult r = j.undo("run1");
        QCOMPARE(r.restored, 1);
        QCOMPARE(r.conflicts.size(), 1);
        QCOMPARE(read(p("ok.txt")), QByteArray("o0"));
        // Only the clash is left to retry.
        QCOMPARE(j.summary("run1").value("count").toInt(), 1);
    }

    void twoRunsOnTheSameFileUndoNewestFirst() {
        RunJournal j(store.path());
        write(p("f.txt"), "v0");
        j.begin("runA");
        j.record(p("f.txt"));
        write(p("f.txt"), "v1");
        j.finish();
        QTest::qWait(5);
        j.begin("runB");
        j.record(p("f.txt"));
        write(p("f.txt"), "v2");
        j.finish();
        // Older first would clobber run B's work: reported, not done.
        QCOMPARE(j.undo("runA").conflicts.size(), 1);
        QCOMPARE(read(p("f.txt")), QByteArray("v2"));
        QVERIFY(j.undo("runB").ok());
        QCOMPARE(read(p("f.txt")), QByteArray("v1"));
        QVERIFY(j.undo("runA").ok());
        QCOMPARE(read(p("f.txt")), QByteArray("v0"));
    }

    // ---- limits -----------------------------------------------------------------

    void hugeFilesAreNotSnapshottedButAreSaid() {
        RunJournal j(store.path());
        QFile big(p("big.bin"));
        QVERIFY(big.open(QIODevice::WriteOnly));
        QVERIFY(big.resize(RunJournal::kMaxFileBytes + 1));   // sparse
        big.close();
        write(p("small.txt"), "s0");
        j.begin("run1");
        QCOMPARE(j.record(p("big.bin")), RunJournal::Record::Skipped);
        j.record(p("small.txt"));
        write(p("small.txt"), "s1");
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("count").toInt(), 1);
        const QVariantList untracked = s.value("untracked").toList();
        QCOMPARE(untracked.size(), 1);
        QVERIFY(untracked.first().toMap().value("reason").toString().contains("larger than"));
    }

    void aNamedFolderIsRecordedFileByFile() {
        RunJournal j(store.path());
        write(p("proj/a.txt"), "a");
        write(p("proj/src/b.txt"), "b");
        write(p("proj/.hidden"), "h");
        j.begin("run1");
        QCOMPARE(j.record(p("proj")), RunJournal::Record::Recorded);   // e.g. rm -r proj
        QVERIFY(QDir(p("proj")).removeRecursively());
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("deleted").toInt(), 3);
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("proj/src/b.txt")), QByteArray("b"));
        QCOMPARE(read(p("proj/.hidden")), QByteArray("h"));
    }

    void aHugeFolderIsCappedAndSaid() {
        RunJournal j(store.path());
        for (int i = 0; i < RunJournal::kMaxFolderFiles + 20; ++i)
            write(p(QStringLiteral("many/f%1.txt").arg(i)), "x");
        j.begin("run1");
        j.record(p("many"));
        QVERIFY(QDir(p("many")).removeRecursively());
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("count").toInt(), RunJournal::kMaxFolderFiles);
        QVERIFY(s.value("untracked").toList().first().toMap().value("reason").toString().contains("only the first"));
    }

    // ---- shell paths -----------------------------------------------------------------

    void relativePathsResolveAgainstTheBaseFolder() {
        RunJournal j(store.path());
        write(p("rel.txt"), "r0");
        j.begin("run1");
        QCOMPARE(j.recordPaths({ "rel.txt" }, work.path()), 1);
        write(p("rel.txt"), "r1");
        j.finish();
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("rel.txt")), QByteArray("r0"));
    }

    void wildcardsExpandToTheFilesThatMatch() {
        RunJournal j(store.path());
        write(p("a.log"), "1");
        write(p("b.log"), "2");
        write(p("keep.txt"), "3");
        j.begin("run1");
        QCOMPARE(j.recordPaths({ "*.log" }, work.path()), 2);   // rm *.log
        QFile::remove(p("a.log"));
        QFile::remove(p("b.log"));
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("deleted").toInt(), 2);
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("a.log")), QByteArray("1"));
        QCOMPARE(read(p("b.log")), QByteArray("2"));
    }

    void unresolvedVariablesAreSaidNotGuessed() {
        RunJournal j(store.path());
        write(p("a.txt"), "a0");
        j.begin("run1");
        QCOMPARE(j.recordPaths({ "$env:SOMETHING/x.txt", "a.txt" }, work.path()), 1);
        write(p("a.txt"), "a1");
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("untracked").toList().size(), 1);
        QVERIFY(s.value("untracked").toList().first().toMap().value("reason").toString().contains("variable"));
    }

    void movedSourcesAreRecordedSoMoveCanBeUndone() {
        // The gate calls mv sources "reads"; the journal needs them as `moved`.
        const AgentPolicy::ShellPaths sp = AgentPolicy::shellPaths("mv old.txt new.txt");
        QVERIFY(sp.moved.contains("old.txt"));
        QVERIFY(sp.writes.contains("new.txt"));
        QVERIFY(AgentPolicy::shellPaths("cp a.txt b.txt").moved.isEmpty());
        QVERIFY(AgentPolicy::shellPaths("rm a.txt").moved.isEmpty());

        RunJournal j(store.path());
        write(p("old.txt"), "content");
        j.begin("run1");
        j.recordPaths(sp.moved + sp.writes, work.path());
        QVERIFY(QFile::rename(p("old.txt"), p("new.txt")));
        const QVariantMap s = j.finish();
        QCOMPARE(s.value("count").toInt(), 2);   // old.txt deleted, new.txt created
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("old.txt")), QByteArray("content"));
        QVERIFY(!QFileInfo::exists(p("new.txt")));
    }

    // ---- lifecycle ----------------------------------------------------------------------

    void undoSurvivesARestart() {
        write(p("a.txt"), "v0");
        {
            RunJournal j(store.path());
            j.begin("run1");
            j.record(p("a.txt"));
            write(p("a.txt"), "v1");
            j.finish();
        }
        RunJournal later(store.path());   // the app was closed and reopened
        QCOMPARE(later.latestWithChanges(), QStringLiteral("run1"));
        QVERIFY(later.undo("run1").ok());
        QCOMPARE(read(p("a.txt")), QByteArray("v0"));
    }

    void aRunThatNeverFinishedCanStillBeUndone() {
        write(p("a.txt"), "v0");
        {
            RunJournal j(store.path());
            j.begin("crashed");
            j.record(p("a.txt"));
            write(p("a.txt"), "half-done");
            // no finish(): the app died
        }
        RunJournal later(store.path());
        QCOMPARE(later.latestWithChanges(), QStringLiteral("crashed"));
        QVERIFY(later.undo("crashed").ok());
        QCOMPARE(read(p("a.txt")), QByteArray("v0"));
    }

    void theRunInProgressIsNeverOfferedForUndo() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("busy");
        j.record(p("a.txt"));
        write(p("a.txt"), "v1");
        QVERIFY(j.latestWithChanges().isEmpty());
        const RunJournal::UndoResult r = j.undo("busy");   // refused: it is still running
        QCOMPARE(r.restored + r.removed, 0);
        QCOMPARE(read(p("a.txt")), QByteArray("v1"));
        j.finish();
    }

    void onlyTheNewestRunsAreKept() {
        RunJournal j(store.path());
        for (int i = 0; i < RunJournal::kKeepRuns + 3; ++i) {
            write(p("f.txt"), QByteArray::number(i));
            j.begin(QStringLiteral("run%1").arg(i, 2, 10, QChar('0')));
            j.record(p("f.txt"));
            write(p("f.txt"), QByteArray::number(i + 100));
            QVERIFY(!j.finish().isEmpty());
            QTest::qWait(3);
        }
        const QStringList ids = j.runIds();
        QCOMPARE(ids.size(), RunJournal::kKeepRuns);
        QCOMPARE(ids.first(), QStringLiteral("run12"));
        QVERIFY(!ids.contains("run00"));
        QVERIFY(!QDir(store.path()).exists("run00"));
    }

    void beginningARunClosesTheOneBefore() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("one");
        j.record(p("a.txt"));
        write(p("a.txt"), "v1");
        j.begin("two");   // forgot to finish "one"
        QCOMPARE(j.summary("one").value("modified").toInt(), 1);
        j.finish();
    }

    void discardRemovesAJournal() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        j.begin("run1");
        j.record(p("a.txt"));
        write(p("a.txt"), "v1");
        j.finish();
        j.discard("run1");
        QVERIFY(j.runIds().isEmpty());
        QCOMPARE(read(p("a.txt")), QByteArray("v1"));   // discarding forgets, it does not undo
    }

    void recordingOutsideARunDoesNothing() {
        RunJournal j(store.path());
        write(p("a.txt"), "v0");
        QCOMPARE(j.record(p("a.txt")), RunJournal::Record::Skipped);
        QVERIFY(j.finish().isEmpty());
        QVERIFY(j.runIds().isEmpty());
    }

#ifndef Q_OS_WIN
    void aSymlinkIsSavedThroughToItsTarget() {
        RunJournal j(store.path());
        write(p("real.txt"), "v0");
        QVERIFY(QFile::link(p("real.txt"), p("alias.txt")));
        j.begin("run1");
        j.record(p("alias.txt"));
        write(p("real.txt"), "v1");   // the write lands on the target
        j.finish();
        QVERIFY(j.undo("run1").ok());
        QCOMPARE(read(p("real.txt")), QByteArray("v0"));
        QVERIFY(QFileInfo(p("alias.txt")).isSymLink());   // the link itself is untouched
    }
#endif
};

QTEST_GUILESS_MAIN(TstRunJournal)
#include "tst_runjournal.moc"
