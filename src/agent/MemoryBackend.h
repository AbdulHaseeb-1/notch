// One memory interface for Buddy's two memory stores.
//
// Buddy remembers in two places: the local BM25 store (MemoryStore, files
// under /memories) and Mem0 (Mem0Client, a server that extracts and recalls
// facts across conversations). Before this interface each consumer wired the
// two together itself: recall merged local and remote hits by string
// concatenation in two places, the memory_search tool merged them a third
// way, and the end-of-run write path picked a store with an if-branch — so
// turning Mem0 off silently changed what was remembered and what was found.
//
// Now both stores implement MemoryBackend (recall / search / addExchange),
// mergeMemoryHits() below is the single place where hits from the two
// sources are combined (dedupe, rank, cap), and BuddyProvider's write plan
// names the backends it uses explicitly.
//
// Recall is callback-based because the two stores answer differently: the
// local store is synchronous (its callback fires before recall() returns),
// Mem0 is a network round trip. Callers must not assume either timing —
// the first-turn recall race in BuddyProvider is built on exactly that.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <functional>

// One memory, whichever store it came from.
struct MemoryHit {
    QString text;            // mem0: the memory text; local: the fact's one-line gist
    double score = 0.0;      // backend-native relevance (mem0: 0..1; local: raw BM25)
    double rank = 0.0;       // normalized to 0..1 for cross-source ranking (see mergeMemoryHits)
    QString backend;         // "mem0" | "local"
    QString id;              // mem0 hit id, or the local fact's path under /memories
    bool procedure = false;  // local only: a distilled procedure, not a fact
};

// A message to save, in the shape both stores can be handed.
struct MemoryMessage {
    QString role;      // "user" | "assistant"
    QString content;
};

// Combines hits from the two backends into one ranked list — the only
// merge point for memory results.
//
//   dedupe: hits whose text matches after flattening (case-insensitive,
//           whitespace-collapsed) collapse to one; the higher-ranked copy
//           wins, and on a tie the earlier list wins (callers pass local
//           first, so a tie keeps the local hit — it has a /memories path
//           the model can open).
//   rank:   sorted by MemoryHit::rank, descending, stable — equal ranks
//           keep their input order (local before remote).
//   cap:    truncated to `cap` entries after ranking; cap <= 0 = no cap.
QList<MemoryHit> mergeMemoryHits(const QList<MemoryHit> &local, const QList<MemoryHit> &remote, int cap);

// The prompt block for recalled-from-conversation hits, over the unified
// hit type. Byte-identical to mem0::recallSection() for the same hits
// (pinned by tst_memorybackend): memories are flattened to one line,
// capped, and fenced so they read as data, never as instructions.
QString memoryPromptSection(const QList<MemoryHit> &hits);
// One bullet line per hit, for tool results. Same guarantee vs
// mem0::recallLines().
QString memoryRecallLines(const QList<MemoryHit> &hits);

class MemoryBackend
{
public:
    // token echoes the value passed to recall/search/addExchange so the
    // caller can drop completions that arrive after it moved on (a new
    // run, a cancel). error is empty exactly when the operation worked;
    // memory fails open, so an error means "no hits", never "run failed".
    using RecallDone = std::function<void(quint64 token, const QList<MemoryHit> &hits,
                                          const QString &error, int ms)>;
    using AddDone = std::function<void(quint64 token, bool ok, const QString &error)>;

    virtual ~MemoryBackend() = default;

    virtual QString id() const = 0;   // "mem0" | "local"
    // Recall/search may be attempted right now (enabled, configured, not
    // resting after failures). The local store is always ready.
    virtual bool recallReady() const = 0;
    // addExchange may be attempted right now. Only Mem0 takes exchanges —
    // it extracts the facts server-side; the local store reports false and
    // receives explicit facts instead (see LocalMemoryBackend::addFacts).
    virtual bool saveReady() const = 0;

    // Prompt recall for `query`. The callback fires exactly once unless
    // cancelRecall() drops it; a synchronous backend fires it before
    // recall() returns.
    virtual void recall(const QString &query, int topK, quint64 token, RecallDone done) = 0;
    // The same query for the memory_search tool (identical mechanics,
    // separate call site so the two consumers stay distinguishable).
    virtual void search(const QString &query, int topK, quint64 token, RecallDone done) = 0;
    // Drops the in-flight recall/search: its callback never fires.
    virtual void cancelRecall() = 0;

    // Fire-and-forget save of a full exchange; the callback reports the
    // outcome exactly once. Backends that do not take exchanges answer
    // (token, false, ...) without storing anything.
    virtual void addExchange(const QList<MemoryMessage> &messages, quint64 token,
                             const QJsonObject &metadata, AddDone done) = 0;
};
