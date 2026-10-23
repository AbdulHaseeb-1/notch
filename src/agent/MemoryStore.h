// P8: Mem0-style memory store. Memories are small Markdown files under the
// memories root; a "fact" is one file whose first line doubles as its
// gist. Retrieval is BM25 over file text (no embeddings, no network).
// Procedures (distilled checklists from successful tasks) live in
// procedures/ and rank alongside facts.
#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>

struct MemoryFact {
    QString id;            // path relative to the memories root, e.g. "facts/abc.md"
    QString text;          // full file contents
    QString gist;          // first non-empty line, trimmed
    QDateTime modified;
    bool procedure = false;
};

class MemoryStore {
public:
    explicit MemoryStore(const QString &root) : m_root(root) {}

    QString root() const { return m_root; }

    // M4: hard caps so one bloated memory file can't stall the UI forever.
    static constexpr qint64 kMaxFactBytes = 32 * 1024;  // addFact rejection
    static constexpr qint64 kMaxScanBytes = 64 * 1024;   // list() skips larger files
    static constexpr qsizetype kMaxListed = 500;         // list() reads at most the newest N files
    static constexpr qsizetype kMaxFacts = 500;          // addFact keeps the newest N auto facts

    // All facts (recursively), newest first.
    QList<MemoryFact> list() const;

    // One fact by id; empty when missing.
    MemoryFact get(const QString &id) const;

    // Adds a new fact file and returns its id ("" on failure). The file
    // name derives from a hash of the text, so re-adding the same text
    // updates in place instead of duplicating.
    QString addFact(const QString &text, bool procedure = false);

    // Replaces a fact's full contents. Returns the id of the result — the
    // file is renamed to the new content hash when the text changes, so the
    // content-addressing invariant stays coherent (M3). "" on failure.
    QString updateFact(const QString &id, const QString &text);

    struct SearchHit {
        MemoryFact fact;
        double score = 0.0;
    };
    // BM25-ranked search over all facts. Returns up to topK hits with
    // score > 0, best first. Prefer the three-argument form: pass one
    // list() per run instead of re-reading the corpus per query (P0).
    QList<SearchHit> search(const QString &query, int topK = 5) const;
    QList<SearchHit> search(const QList<MemoryFact> &facts, const QString &query, int topK = 5) const;

    // Tokenizes for the BM25 pass (lower-cased, alphanumeric, length >= 2).
    static QStringList tokenize(const QString &text);

private:
    QString m_root;
    QString pathFor(const QString &id) const;
    void pruneFacts() const;
    static QString gistOf(const QString &text);
};
