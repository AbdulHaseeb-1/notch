// The local implementation of MemoryBackend: the BM25 MemoryStore (small
// Markdown files under /memories). Synchronous — recall/search invoke
// their callback before returning, which the interface explicitly allows.
//
// The store is rooted through a supplier rather than a fixed path because
// BuddyProvider's memoriesPath() is computed per call; every operation
// builds a fresh MemoryStore on the current root, exactly as the call
// sites did before this class existed.
//
// Writes are this backend's own surface, beyond the interface: only a
// local store can dedupe candidate facts against its own corpus, so
// addFacts (the ADD / UPDATE / flag-overlap pipeline) and addProcedure
// live here, and BuddyProvider's write plan calls them by name. For the
// same reason saveReady() is false — the local store takes facts, not
// exchanges; nothing extracts facts from an exchange locally.
#pragma once

#include "MemoryBackend.h"
#include "MemoryStore.h"

#include <functional>

class LocalMemoryBackend : public MemoryBackend
{
public:
    using RootFn = std::function<QString()>;
    // Applied to every fact before it is written (BuddyProvider passes its
    // secret redaction): the store bypasses the harness's output guard and
    // re-injects facts into future system prompts, so nothing is stored
    // unsanitized.
    using SanitizeFn = std::function<QString(const QString &)>;

    LocalMemoryBackend() = default;
    LocalMemoryBackend(RootFn root, SanitizeFn sanitize = {});

    // BuddyProvider is constructed before its memories root and redactor
    // are wired up, so it configures its backend in the constructor body.
    void setRootSupplier(RootFn root) { m_root = std::move(root); }
    void setSanitizer(SanitizeFn sanitize) { m_sanitize = std::move(sanitize); }

    QString id() const override { return QStringLiteral("local"); }
    bool recallReady() const override { return true; }
    bool saveReady() const override { return false; }   // facts, not exchanges — see above

    // Synchronous: `done` fires before these return, with ms = 0.
    void recall(const QString &query, int topK, quint64 token, RecallDone done) override;
    void search(const QString &query, int topK, quint64 token, RecallDone done) override;
    void cancelRecall() override {}   // nothing is ever in flight
    void addExchange(const QList<MemoryMessage> &messages, quint64 token,
                     const QJsonObject &metadata, AddDone done) override;

    // The run-start snapshot selection, over one corpus read: BM25 top-8
    // for the query, or — when nothing matches — the 4 newest facts, so
    // the model at least knows what exists. Sets *corpusEmpty when the
    // store holds nothing at all and *usedFallback for the newest-case.
    QList<MemoryHit> snapshotHits(const QString &query, bool *corpusEmpty, bool *usedFallback);

    struct WriteCounts {
        int added = 0;
        int updated = 0;
        int flagged = 0;   // added with a "possible overlap" review note
    };
    // Stores extracted fact candidates: dedupe on token coverage against
    // the corpus (not raw BM25 — its scores scale with corpus size), then
    // UPDATE the same fact restated (>= 0.6 coverage), ADD with a review
    // note on partial overlap, ADD plainly otherwise.
    WriteCounts addFacts(const QStringList &candidates);
    // Stores a distilled procedure; returns the fact id ("" on failure).
    QString addProcedure(const QString &text);

private:
    QList<MemoryHit> searchCorpus(const QString &q, int topK) const;
    RootFn m_root;
    SanitizeFn m_sanitize;
};
