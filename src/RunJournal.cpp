#include "RunJournal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

#include "AgentPolicy.h"

namespace {

constexpr int kMaxWildcardMatches = 200;

QString journalFile(const QString &runDir)
{
    return QDir(runDir).filePath(QStringLiteral("journal.json"));
}

QString actionLabel(const QString &a)
{
    return a;   // created / modified / deleted
}

void addUntracked(QList<QPair<QString, QString>> &list, const QString &path, const QString &reason)
{
    for (const auto &u : list)
        if (u.first == path && u.second == reason)
            return;
    list.append({ path, reason });
}

} // namespace

RunJournal::RunJournal(const QString &root)
    : m_root(root)
{
    recoverOrphans();
}

void RunJournal::setRoot(const QString &root)
{
    m_root = root;
    recoverOrphans();
}

// A run that never finished (the app was closed or crashed mid-run) left a
// journal of what it was about to change. Close it now, taking the files as
// they are as "what the run left", so it can still be undone.
void RunJournal::recoverOrphans()
{
    if (m_root.isEmpty())
        return;
    for (const QString &name : QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        Run run;
        if (name == m_runId || !load(name, &run) || run.finished)
            continue;
        QList<Entry> kept;
        for (Entry e : run.entries) {
            e.existsAfter = QFileInfo(e.path).isFile();
            e.hashAfter = e.existsAfter ? hashOf(e.path) : QString();
            if (!e.existed && e.existsAfter)
                e.action = QStringLiteral("created");
            else if (e.existed && !e.existsAfter)
                e.action = QStringLiteral("deleted");
            else if (e.existed && e.hashAfter != e.hashBefore)
                e.action = QStringLiteral("modified");
            else {
                if (!e.backup.isEmpty())
                    QFile::remove(QDir(runDir(name)).filePath(e.backup));
                continue;
            }
            kept.append(e);
        }
        run.entries = kept;
        run.finished = true;
        if (kept.isEmpty())
            QDir(runDir(name)).removeRecursively();
        else
            save(run);
    }
}

QString RunJournal::runDir(const QString &runId) const
{
    return QDir(m_root).filePath(runId);
}

QString RunJournal::hashOf(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha1);
    while (!f.atEnd())
        h.addData(f.read(1 << 20));
    return QString::fromLatin1(h.result().toHex());
}

// The file a path really names: through symlinks when it exists, otherwise
// its cleaned absolute form. The editor and the shell both write through a
// link, so the target is what has to be saved.
QString RunJournal::canonical(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.exists()) {
        const QString c = fi.canonicalFilePath();
        if (!c.isEmpty())
            return c;
    }
    return QDir::cleanPath(fi.absoluteFilePath());
}

// ---- recording ---------------------------------------------------------------

void RunJournal::begin(const QString &runId)
{
    if (active())
        finish();
    m_run = Run();
    m_run.id = runId;
    m_runId = runId;
}

RunJournal::Record RunJournal::snapshotFile(Run &run, const QString &rawPath)
{
    const QString path = canonical(rawPath);
    for (const Entry &e : run.entries)
        if (e.path == path)
            return Record::AlreadyRecorded;

    Entry e;
    e.path = path;
    const QFileInfo fi(path);
    if (fi.exists()) {
        if (!fi.isFile()) {
            addUntracked(run.untracked, path, QStringLiteral("not a regular file"));
            return Record::Skipped;
        }
        if (fi.size() > kMaxFileBytes) {
            addUntracked(run.untracked, path, QStringLiteral("larger than %1 MB").arg(kMaxFileBytes / (1024 * 1024)));
            return Record::Skipped;
        }
        if (run.bytes + fi.size() > kMaxRunBytes) {
            addUntracked(run.untracked, path, QStringLiteral("this run's backup limit was reached"));
            return Record::Skipped;
        }
        QDir().mkpath(runDir(run.id));
        e.backup = QStringLiteral("b%1").arg(++run.counter);
        if (!QFile::copy(path, QDir(runDir(run.id)).filePath(e.backup))) {
            addUntracked(run.untracked, path, QStringLiteral("could not be copied"));
            return Record::Skipped;
        }
        e.existed = true;
        e.size = fi.size();
        e.hashBefore = hashOf(path);
        run.bytes += e.size;
    }
    run.entries.append(e);
    return Record::Recorded;
}

RunJournal::Record RunJournal::record(const QString &rawPath)
{
    if (!active() || rawPath.trimmed().isEmpty())
        return Record::Skipped;
    const QString path = canonical(rawPath);
    Record result = Record::AlreadyRecorded;
    if (QFileInfo(path).isDir()) {
        // A named folder: every file in it, up to a limit.
        int seen = 0;
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString f = it.next();
            if (++seen > kMaxFolderFiles) {
                addUntracked(m_run.untracked, path, QStringLiteral("more than %1 files; only the first %1 can be undone").arg(kMaxFolderFiles));
                break;
            }
            if (snapshotFile(m_run, f) == Record::Recorded)
                result = Record::Recorded;
        }
    } else {
        result = snapshotFile(m_run, path);
    }
    if (result == Record::Recorded)
        save(m_run);   // a crash mid-run still leaves a journal to undo from
    return result;
}

int RunJournal::recordPaths(const QStringList &paths, const QString &baseDir)
{
    if (!active())
        return 0;
    int recorded = 0;
    for (const QString &raw : paths) {
        const QString resolved = AgentPolicy::resolvePath(raw, baseDir);
        if (resolved.isEmpty())
            continue;
        if (resolved.contains(QLatin1Char('$')) || resolved.contains(QLatin1Char('%'))) {
            addUntracked(m_run.untracked, raw, QStringLiteral("uses a variable that could not be resolved"));
            continue;
        }
        if (resolved.contains(QLatin1Char('*')) || resolved.contains(QLatin1Char('?'))) {
            const QFileInfo fi(resolved);
            if (fi.path().contains(QLatin1Char('*')) || fi.path().contains(QLatin1Char('?'))) {
                addUntracked(m_run.untracked, raw, QStringLiteral("wildcard in a folder name"));
                continue;
            }
            const QFileInfoList matches = QDir(fi.path()).entryInfoList(
                { fi.fileName() }, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
            int n = 0;
            for (const QFileInfo &m : matches) {
                if (++n > kMaxWildcardMatches) {
                    addUntracked(m_run.untracked, raw, QStringLiteral("more than %1 matches").arg(kMaxWildcardMatches));
                    break;
                }
                if (record(m.absoluteFilePath()) == Record::Recorded)
                    ++recorded;
            }
            continue;
        }
        if (record(resolved) == Record::Recorded)
            ++recorded;
    }
    return recorded;
}

// ---- closing ---------------------------------------------------------------------

QVariantMap RunJournal::finish()
{
    if (!active())
        return {};
    Run run = m_run;
    m_runId.clear();
    m_run = Run();

    QList<Entry> kept;
    for (Entry e : run.entries) {
        const QFileInfo fi(e.path);
        e.existsAfter = fi.isFile();
        e.hashAfter = e.existsAfter ? hashOf(e.path) : QString();
        if (!e.existed && e.existsAfter)
            e.action = QStringLiteral("created");
        else if (e.existed && !e.existsAfter)
            e.action = QStringLiteral("deleted");
        else if (e.existed && e.existsAfter && e.hashAfter != e.hashBefore)
            e.action = QStringLiteral("modified");
        else {
            if (!e.backup.isEmpty())
                QFile::remove(QDir(runDir(run.id)).filePath(e.backup));   // nothing changed: no backup needed
            continue;
        }
        kept.append(e);
    }
    run.entries = kept;
    run.finished = true;
    if (kept.isEmpty()) {
        QDir(runDir(run.id)).removeRecursively();
        return {};
    }
    save(run);
    prune();
    return summary(run.id);
}

// ---- persistence -----------------------------------------------------------------

bool RunJournal::save(const Run &run) const
{
    if (m_root.isEmpty())
        return false;
    QJsonArray entries;
    for (const Entry &e : run.entries) {
        entries.append(QJsonObject{
            { "path", e.path }, { "existed", e.existed }, { "backup", e.backup }, { "size", double(e.size) },
            { "hashBefore", e.hashBefore }, { "hashAfter", e.hashAfter }, { "existsAfter", e.existsAfter },
            { "action", e.action }, { "done", e.done } });
    }
    QJsonArray untracked;
    for (const auto &u : run.untracked)
        untracked.append(QJsonObject{ { "path", u.first }, { "reason", u.second } });
    const QJsonObject doc{
        { "runId", run.id }, { "finished", run.finished }, { "bytes", double(run.bytes) },
        { "counter", run.counter }, { "entries", entries }, { "untracked", untracked },
        { "savedAt", double(QDateTime::currentMSecsSinceEpoch()) } };
    QDir().mkpath(runDir(run.id));
    QSaveFile f(journalFile(runDir(run.id)));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(doc).toJson(QJsonDocument::Compact));
    return f.commit();
}

bool RunJournal::load(const QString &runId, Run *out) const
{
    QFile f(journalFile(runDir(runId)));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    if (doc.value("runId").toString() != runId)
        return false;
    Run run;
    run.id = runId;
    run.finished = doc.value("finished").toBool();
    run.bytes = qint64(doc.value("bytes").toDouble());
    run.counter = doc.value("counter").toInt();
    for (const QJsonValue &v : doc.value("entries").toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.path = o.value("path").toString();
        e.existed = o.value("existed").toBool();
        e.backup = o.value("backup").toString();
        e.size = qint64(o.value("size").toDouble());
        e.hashBefore = o.value("hashBefore").toString();
        e.hashAfter = o.value("hashAfter").toString();
        e.existsAfter = o.value("existsAfter").toBool();
        e.action = o.value("action").toString();
        e.done = o.value("done").toBool();
        run.entries.append(e);
    }
    for (const QJsonValue &v : doc.value("untracked").toArray())
        addUntracked(run.untracked, v.toObject().value("path").toString(), v.toObject().value("reason").toString());
    *out = run;
    return true;
}

void RunJournal::prune() const
{
    QStringList ids = runIds();
    for (int i = kKeepRuns; i < ids.size(); ++i)
        QDir(runDir(ids.at(i))).removeRecursively();
}

QStringList RunJournal::runIds() const
{
    struct Item { QString id; qint64 when; };
    QList<Item> items;
    const QDir root(m_root);
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(journalFile(root.filePath(name)));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const qint64 when = qint64(QJsonDocument::fromJson(f.readAll()).object().value("savedAt").toDouble());
        items.append({ name, when });
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.when != b.when ? a.when > b.when : a.id > b.id;
    });
    QStringList out;
    for (const Item &i : items)
        out << i.id;
    return out;
}

QVariantMap RunJournal::summary(const QString &runId) const
{
    Run run;
    if (!load(runId, &run))
        return {};
    QVariantList files;
    int created = 0, modified = 0, deleted = 0;
    for (const Entry &e : run.entries) {
        if (e.done || e.action.isEmpty())
            continue;
        created += e.action == QLatin1String("created");
        modified += e.action == QLatin1String("modified");
        deleted += e.action == QLatin1String("deleted");
        files.append(QVariantMap{ { QStringLiteral("path"), e.path }, { QStringLiteral("action"), actionLabel(e.action) } });
    }
    if (files.isEmpty())
        return {};
    QVariantList untracked;
    for (const auto &u : run.untracked)
        untracked.append(QVariantMap{ { QStringLiteral("path"), u.first }, { QStringLiteral("reason"), u.second } });
    return QVariantMap{
        { QStringLiteral("runId"), runId },
        { QStringLiteral("count"), int(files.size()) },
        { QStringLiteral("created"), created },
        { QStringLiteral("modified"), modified },
        { QStringLiteral("deleted"), deleted },
        { QStringLiteral("files"), files },
        { QStringLiteral("untracked"), untracked },
    };
}

QString RunJournal::latestWithChanges() const
{
    for (const QString &id : runIds()) {
        if (id == m_runId)
            continue;   // still working
        if (!summary(id).isEmpty())
            return id;
    }
    return QString();
}

// ---- undo --------------------------------------------------------------------------

RunJournal::UndoResult RunJournal::undo(const QString &runId, bool force)
{
    UndoResult res;
    Run run;
    if (runId == m_runId || !load(runId, &run))
        return res;
    // Newest change first, so a file the run touched twice ends as it began.
    for (int i = run.entries.size() - 1; i >= 0; --i) {
        Entry &e = run.entries[i];
        if (e.done)
            continue;
        const QFileInfo fi(e.path);
        const bool existsNow = fi.isFile();
        const bool blockedByFolder = fi.exists() && !fi.isFile();
        const QString nowHash = existsNow ? hashOf(e.path) : QString();
        const bool asLeft = !blockedByFolder && existsNow == e.existsAfter && nowHash == e.hashAfter;
        if (!asLeft && !force) {
            res.conflicts << e.path;
            continue;
        }
        if (blockedByFolder) {
            res.failed << e.path;
            continue;
        }
        if (e.existed) {
            QFile src(QDir(runDir(runId)).filePath(e.backup));
            if (!src.open(QIODevice::ReadOnly)) {
                res.failed << e.path;
                continue;
            }
            const QByteArray data = src.readAll();
            QDir().mkpath(fi.absolutePath());
            QSaveFile out(e.path);
            if (!out.open(QIODevice::WriteOnly)) {
                res.failed << e.path;
                continue;
            }
            out.write(data);
            if (!out.commit()) {
                res.failed << e.path;
                continue;
            }
            ++res.restored;
        } else {
            if (existsNow && !QFile::remove(e.path)) {
                res.failed << e.path;
                continue;
            }
            ++res.removed;
        }
        e.done = true;
    }

    // Keep only what is still to do (conflicts can be retried with force).
    QList<Entry> left;
    for (const Entry &e : run.entries) {
        if (e.done) {
            if (!e.backup.isEmpty())
                QFile::remove(QDir(runDir(runId)).filePath(e.backup));
        } else {
            left.append(e);
        }
    }
    run.entries = left;
    if (left.isEmpty())
        QDir(runDir(runId)).removeRecursively();
    else
        save(run);
    return res;
}

void RunJournal::discard(const QString &runId)
{
    if (runId.isEmpty() || runId == m_runId)
        return;
    QDir(runDir(runId)).removeRecursively();
}
