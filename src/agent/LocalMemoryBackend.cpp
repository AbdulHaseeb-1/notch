#include "LocalMemoryBackend.h"

#include <QSet>

namespace {

MemoryHit hitOf(const MemoryFact &fact, double score)
{
    MemoryHit h;
    h.text = fact.gist;
    h.score = score;
    // Squash BM25's unbounded score into 0..1 for cross-source ranking;
    // monotonic, so ordering within the local store is unchanged.
    h.rank = score > 0.0 ? score / (1.0 + score) : 0.0;
    h.backend = QStringLiteral("local");
    h.id = fact.id;
    h.procedure = fact.procedure;
    return h;
}

} // namespace

LocalMemoryBackend::LocalMemoryBackend(RootFn root, SanitizeFn sanitize)
    : m_root(std::move(root))
    , m_sanitize(std::move(sanitize))
{
}

QList<MemoryHit> LocalMemoryBackend::searchCorpus(const QString &q, int topK) const
{
    if (!m_root)
        return {};
    MemoryStore store(m_root());
    QList<MemoryHit> out;
    for (const MemoryStore::SearchHit &h : store.search(q, topK))
        out.append(hitOf(h.fact, h.score));
    return out;
}

void LocalMemoryBackend::recall(const QString &query, int topK, quint64 token, RecallDone done)
{
    if (done)
        done(token, searchCorpus(query, topK), QString(), 0);
}

void LocalMemoryBackend::search(const QString &query, int topK, quint64 token, RecallDone done)
{
    if (done)
        done(token, searchCorpus(query, topK), QString(), 0);
}

void LocalMemoryBackend::addExchange(const QList<MemoryMessage> &messages, quint64 token,
                                     const QJsonObject &metadata, AddDone done)
{
    Q_UNUSED(messages);
    Q_UNUSED(metadata);
    // The write plan never routes exchanges here (saveReady() is false);
    // answer honestly rather than pretending to store them.
    if (done)
        done(token, false, QStringLiteral("The local memory store does not take exchanges."));
}

QList<MemoryHit> LocalMemoryBackend::snapshotHits(const QString &query, bool *corpusEmpty, bool *usedFallback)
{
    if (!m_root) {
        if (corpusEmpty)
            *corpusEmpty = true;
        if (usedFallback)
            *usedFallback = false;
        return {};
    }
    MemoryStore store(m_root());
    const QList<MemoryFact> all = store.list();
    if (corpusEmpty)
        *corpusEmpty = all.isEmpty();
    if (all.isEmpty()) {
        if (usedFallback)
            *usedFallback = false;
        return {};
    }
    const QList<MemoryStore::SearchHit> hits = store.search(all, query, 8);  // search the list we already loaded
    if (!hits.isEmpty()) {
        if (usedFallback)
            *usedFallback = false;
        QList<MemoryHit> out;
        for (const MemoryStore::SearchHit &h : hits)
            out.append(hitOf(h.fact, h.score));
        return out;
    }
    if (usedFallback)
        *usedFallback = true;
    QList<MemoryHit> out;
    for (int i = 0; i < qMin(4, all.size()); ++i)
        out.append(hitOf(all.at(i), 0.0));   // newest first; no relevance claim
    return out;
}

LocalMemoryBackend::WriteCounts LocalMemoryBackend::addFacts(const QStringList &candidates)
{
    if (!m_root)
        return {};
    MemoryStore store(m_root());
    // Read the corpus once; every candidate searches this snapshot.
    const QList<MemoryFact> corpus = store.list();
    WriteCounts counts;
    for (QString fact : candidates) {
        // The store bypasses every output guardrail in the harness and
        // re-injects facts into future system prompts, so a pasted secret
        // would leak to every future provider. Sanitize first.
        if (m_sanitize)
            fact = m_sanitize(fact);
        if (fact.trimmed().size() < 8)
            continue;
        // Dedupe on token *coverage*, not raw BM25 — BM25 scores scale
        // with corpus size, so fixed thresholds misbehave on small stores.
        // coverage = share of the candidate's terms found in the top hit.
        const QList<MemoryStore::SearchHit> hits = store.search(corpus, fact, 1);
        const QStringList factTerms = MemoryStore::tokenize(fact);
        const QSet<QString> qUniq(factTerms.begin(), factTerms.end());
        double coverage = 0.0;
        if (!hits.isEmpty() && !qUniq.isEmpty()) {
            // NB: tokenize once into a named list. Building a QSet from
            // two separate temporaries' iterators (begin of one, end of
            // another) is undefined behaviour — the old storeLocalFacts
            // did exactly that and only survived by allocator luck.
            const QStringList docTerms = MemoryStore::tokenize(hits.first().fact.text);
            const QSet<QString> docUniq(docTerms.begin(), docTerms.end());
            int shared = 0;
            for (const QString &t : qUniq)
                if (docUniq.contains(t))
                    ++shared;
            coverage = double(shared) / qUniq.size();
        }
        if (!hits.isEmpty() && coverage >= 0.6) {
            // The same fact restated: update with the newest wording.
            // Note: updateFact may rename the file to its new content
            // hash — the returned id keeps the content-addressing
            // invariant coherent instead of silently breaking it.
            if (!store.updateFact(hits.first().fact.id, fact + QLatin1Char('\n')).isEmpty())
                ++counts.updated;
        } else if (!hits.isEmpty() && coverage > 0.0) {
            // Topical overlap but a different statement: keep both, flag it.
            const QString noted = fact + QStringLiteral("\n(auto-extracted; possible overlap with /memories/%1 — verify)")
                                             .arg(hits.first().fact.id);
            if (!store.addFact(noted).isEmpty()) {
                ++counts.added;
                ++counts.flagged;
            }
        } else if (!store.addFact(fact).isEmpty()) {
            ++counts.added;
        }
    }
    return counts;
}

QString LocalMemoryBackend::addProcedure(const QString &text)
{
    if (!m_root)
        return QString();
    MemoryStore store(m_root());
    return store.addFact(m_sanitize ? m_sanitize(text) : text, true);
}
