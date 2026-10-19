#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Undo for a whole Buddy run.
//
// While a run works, every file it is about to change is first copied into a
// per-run journal on disk (<AppData>/undo/<runId>/). When the run ends the
// journal is closed: what actually changed is worked out and a fingerprint of
// each changed file is kept. Undo puts every file back - restores the old
// contents, removes files the run created, brings back files it deleted or
// moved away - but only where the file still is exactly what the run left. A
// file the user (or a later run) changed in the meantime is reported and left
// alone unless undo is forced.
//
// What it can cover is deliberately limited and stated in the UI:
//  * the text-editor tool changes exactly one named file: fully tracked;
//  * a shell command is tracked for the files it names (redirects, cp, mv,
//    rm, Set-Content, ...), including wildcards and folders; a program a
//    command launches (build, installer, script) can touch anything, and
//    that is not tracked.
// No event loop, no network: unit tested on a temp folder.
class RunJournal
{
public:
    static constexpr qint64 kMaxFileBytes = 25LL * 1024 * 1024;   // larger files are not snapshotted
    static constexpr qint64 kMaxRunBytes = 200LL * 1024 * 1024;   // backups per run
    static constexpr int kMaxFolderFiles = 500;                   // files tracked inside one named folder
    static constexpr int kKeepRuns = 10;                          // journals kept on disk

    enum class Record { Recorded, AlreadyRecorded, Skipped };

    struct UndoResult {
        int restored = 0;          // files put back to their old contents
        int removed = 0;           // files the run created, now removed
        QStringList conflicts;     // changed since the run: left alone
        QStringList failed;        // could not be written or removed
        bool ok() const { return conflicts.isEmpty() && failed.isEmpty(); }
    };

    explicit RunJournal(const QString &root = QString());

    void setRoot(const QString &root);
    QString root() const { return m_root; }

    // ---- while a run works -----------------------------------------------
    void begin(const QString &runId);
    bool active() const { return !m_runId.isEmpty(); }
    QString runId() const { return m_runId; }
    // Remember the state of `path` before the run changes it (first call per
    // path wins). A folder records the files in it.
    Record record(const QString &path);
    // Shell targets: relative paths resolve against `baseDir`, wildcards
    // expand, `~` and variables resolve like the policy does; anything that
    // cannot be resolved is listed as untracked. Returns how many files were recorded.
    int recordPaths(const QStringList &paths, const QString &baseDir);
    // Close the journal: work out what changed, drop what did not, write it to disk.
    // Returns the summary (see summary()); empty when nothing changed.
    QVariantMap finish();

    // ---- afterwards ----------------------------------------------------------
    // { runId, count, created, modified, deleted, files: [{ path, action }], untracked: [{ path, reason }] }
    QVariantMap summary(const QString &runId) const;
    QStringList runIds() const;                    // newest first
    QString latestWithChanges() const;             // "" when none
    UndoResult undo(const QString &runId, bool force = false);
    void discard(const QString &runId);

private:
    struct Entry {
        QString path;
        bool existed = false;
        QString backup;            // file name inside the run folder
        qint64 size = 0;
        QString hashBefore;
        QString hashAfter;         // empty = absent after the run
        bool existsAfter = false;
        QString action;            // created / modified / deleted (set by finish)
        bool done = false;         // undone already
    };
    struct Run {
        QString id;
        bool finished = false;
        QList<Entry> entries;
        QList<QPair<QString, QString>> untracked;   // path, reason
        qint64 bytes = 0;
        int counter = 0;
    };

    QString runDir(const QString &runId) const;
    void recoverOrphans();
    bool load(const QString &runId, Run *out) const;
    bool save(const Run &run) const;
    Record snapshotFile(Run &run, const QString &path);
    static QString hashOf(const QString &path);
    static QString canonical(const QString &path);
    void prune() const;

    QString m_root;
    QString m_runId;
    Run m_run;
};
