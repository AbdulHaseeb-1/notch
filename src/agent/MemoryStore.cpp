#include "MemoryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

QString MemoryStore::pathFor(const QString &id) const
{
    // M5: ids are sandbox-confined. Reject anything that could escape
    // <root> before it ever touches the filesystem.
    if (id.isEmpty())
        return QString();
    QString clean = id;
    clean.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (QDir::isAbsolutePath(clean))
        return QString();
    for (const QString &part : clean.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return QString();
    }
    const QDir rootDir(m_root);
    const QString abs = rootDir.absoluteFilePath(clean);
    if (abs != rootDir.absolutePath()
        && !abs.startsWith(rootDir.absolutePath() + QLatin1Char('/')))
        return QString();
    return abs;
}

QString MemoryStore::gistOf(const QString &text)
{
    // L7: strip only leading markdown markers, not '#' everywhere
    // ("C# project notes" stays "C# project notes"). Read the head only.
    const QString head = text.left(400);
    for (const QString &l : head.split(QLatin1Char('\n'))) {
        QString t = l.trimmed();
        while (t.startsWith(QLatin1Char('#')))
            t = t.mid(1).trimmed();
        if (!t.isEmpty())
            return t.left(160);
    }
    return QString();
}

QList<MemoryFact> MemoryStore::list() const
{
    // Stat everything (cheap), but read only the newest kMaxListed: this runs
    // every turn, so its memory must not grow with the size of the store.
    QFileInfoList files;
    QDirIterator it(m_root, { QStringLiteral("*.md") }, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        // M4: one oversized file must not make every corpus read unbounded.
        if (it.fileInfo().size() > kMaxScanBytes)
            continue;
        files.append(it.fileInfo());
    }
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
    if (files.size() > kMaxListed)
        files.resize(kMaxListed);

    QList<MemoryFact> out;
    out.reserve(files.size());
    for (const QFileInfo &info : std::as_const(files)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        MemoryFact fact;
        fact.id = QDir(m_root).relativeFilePath(info.absoluteFilePath());
        fact.text = QString::fromUtf8(file.readAll());
        fact.gist = gistOf(fact.text);
        fact.modified = info.lastModified();
        fact.procedure = fact.id.startsWith(QLatin1String("procedures/"));
        out.append(fact);
    }
    return out;
}

// Auto-extracted facts pile up over months; keep the newest kMaxFacts on disk.
// Procedures are curated on purpose and never pruned here.
void MemoryStore::pruneFacts() const
{
    QDir dir(QDir(m_root).absoluteFilePath(QStringLiteral("facts")));
    QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.md") }, QDir::Files);
    if (files.size() <= kMaxFacts)
        return;
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
    for (qsizetype i = kMaxFacts; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

MemoryFact MemoryStore::get(const QString &id) const
{
    MemoryFact fact;
    const QString path = pathFor(id);
    if (path.isEmpty())
        return fact;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fact;
    fact.id = id;
    fact.text = QString::fromUtf8(file.readAll());
    fact.gist = gistOf(fact.text);
    fact.modified = QFileInfo(file).lastModified();
    fact.procedure = id.startsWith(QLatin1String("procedures/"));
    return fact;
}

QString MemoryStore::addFact(const QString &text, bool procedure)
{
    // M4: model-supplied memory text gets the same treatment as every
    // other bounded buffer in the harness.
    if (text.toUtf8().size() > kMaxFactBytes)
        return QString();
    const QString sub = procedure ? QStringLiteral("procedures") : QStringLiteral("facts");
    QDir dir(QDir(m_root).absoluteFilePath(sub));
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath()))
        return QString();
    // Content-addressed name: identical text lands in the same file.
    const QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    const QString name = QString::fromLatin1(hash) + QStringLiteral(".md");
    QFile file(dir.absoluteFilePath(name));
    const bool isNew = !file.exists();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(text.toUtf8());
    file.close();
    if (isNew && !procedure)
        pruneFacts();
    return sub + QLatin1Char('/') + name;
}

QString MemoryStore::updateFact(const QString &id, const QString &text)
{
    // M3: an in-place rewrite would silently break the content-addressing
    // invariant (the name would no longer match the hash of the content),
    // so a changed fact is renamed to its new hash instead.
    if (text.toUtf8().size() > kMaxFactBytes)
        return QString();
    const QString oldPath = pathFor(id);
    if (oldPath.isEmpty())
        return QString();
    const QString sub = id.startsWith(QLatin1String("procedures/"))
        ? QStringLiteral("procedures") : QStringLiteral("facts");
    const QString newId = sub + QLatin1Char('/')
        + QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12))
        + QStringLiteral(".md");
    if (newId == id) {
        QFile file(oldPath);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(text.toUtf8()) >= 0 ? id : QString();
    }
    const QString newPath = QDir(m_root).absoluteFilePath(newId);
    QFile file(newPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    if (file.write(text.toUtf8()) < 0) {
        file.remove();
        return QString();
    }
    QFile::remove(oldPath);
    return newId;
}

QStringList MemoryStore::tokenize(const QString &text)
{
    // L6: Unicode-aware tokenization so non-Latin text actually searches.
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}]+"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    QStringList out;
    const QStringList parts = text.toLower().split(sep, Qt::SkipEmptyParts);
    for (const QString &p : parts)
        if (p.size() >= 2)
            out << p;
    return out;
}

QList<MemoryStore::SearchHit> MemoryStore::search(const QString &query, int topK) const
{
    // P0: one corpus read per query — callers should hoist list() out of
    // per-candidate loops and use the three-argument form.
    return search(list(), query, topK);
}

QList<MemoryStore::SearchHit> MemoryStore::search(const QList<MemoryFact> &facts,
                                                 const QString &query, int topK) const
{
    const QStringList qTerms = tokenize(query);
    if (qTerms.isEmpty() || facts.isEmpty())
        return {};

    // Corpus statistics for BM25.
    QList<QStringList> docTokens;
    docTokens.reserve(facts.size());
    double totalLen = 0;
    for (const MemoryFact &f : facts) {
        docTokens.append(tokenize(f.text));
        totalLen += docTokens.constLast().size();
    }
    const double avgLen = totalLen / facts.size();
    QHash<QString, int> docFreq;
    for (const QStringList &toks : docTokens) {
        const QSet<QString> uniq(toks.begin(), toks.end());
        for (const QString &t : uniq)
            ++docFreq[t];
    }

    // P0: the query's unique terms are computed once, not per document.
    const QSet<QString> qUniq(qTerms.begin(), qTerms.end());
    const double k1 = 1.2, b = 0.75;
    QList<SearchHit> hits;
    for (int i = 0; i < facts.size(); ++i) {
        const QStringList &toks = docTokens.at(i);
        if (toks.isEmpty())
            continue;
        QHash<QString, int> tf;
        for (const QString &t : toks)
            ++tf[t];
        double score = 0;
        for (const QString &term : qUniq) {
            const int f = tf.value(term, 0);
            if (f == 0)
                continue;
            const double idf = std::log(1.0 + (facts.size() - docFreq.value(term, 0) + 0.5)
                                              / (docFreq.value(term, 0) + 0.5));
            score += idf * (f * (k1 + 1.0)) / (f + k1 * (1.0 - b + b * toks.size() / avgLen));
        }
        if (score > 0)
            hits.append({ facts.at(i), score });
    }
    std::sort(hits.begin(), hits.end(),
              [](const SearchHit &a, const SearchHit &rhs) { return a.score > rhs.score; });
    if (hits.size() > topK)
        hits = hits.mid(0, topK);
    return hits;
}
