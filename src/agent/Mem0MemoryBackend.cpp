#include "Mem0MemoryBackend.h"

namespace {

QList<MemoryHit> convert(const QList<mem0::Hit> &hits)
{
    QList<MemoryHit> out;
    out.reserve(hits.size());
    for (const mem0::Hit &h : hits) {
        MemoryHit mh;
        mh.text = h.memory;
        mh.score = h.score;
        mh.rank = qBound(0.0, h.score, 1.0);   // server relevance is already 0..1
        mh.backend = QStringLiteral("mem0");
        mh.id = h.id;
        out.append(mh);
    }
    return out;
}

} // namespace

Mem0MemoryBackend::Mem0MemoryBackend(Mem0Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    connect(client, &Mem0Client::searched, this, &Mem0MemoryBackend::onSearched);
    connect(client, &Mem0Client::added, this, &Mem0MemoryBackend::onAdded);
}

void Mem0MemoryBackend::recall(const QString &query, int topK, quint64 token, RecallDone done)
{
    startSearch(query, topK, token, std::move(done));
}

void Mem0MemoryBackend::search(const QString &query, int topK, quint64 token, RecallDone done)
{
    startSearch(query, topK, token, std::move(done));
}

void Mem0MemoryBackend::startSearch(const QString &query, int topK, quint64 token, RecallDone done)
{
    // Mem0Client::search() aborts any in-flight search, so the superseded
    // callback is dropped here too — it must never fire with stale hits.
    m_searchPending = true;
    m_searchToken = token;
    m_searchDone = std::move(done);
    m_client->search(query, token, topK);
}

void Mem0MemoryBackend::cancelRecall()
{
    m_searchPending = false;
    m_searchDone = nullptr;
    m_client->cancelSearch();
}

void Mem0MemoryBackend::onSearched(quint64 token, const QList<mem0::Hit> &hits, int ms, const QString &error)
{
    if (!m_searchPending || token != m_searchToken)
        return;   // superseded or cancelled: not ours to answer
    RecallDone done = std::move(m_searchDone);
    m_searchPending = false;
    m_searchDone = nullptr;
    if (done)
        done(token, convert(hits), error, ms);
}

void Mem0MemoryBackend::addExchange(const QList<MemoryMessage> &messages, quint64 token,
                                    const QJsonObject &metadata, AddDone done)
{
    QList<mem0::Message> msgs;
    msgs.reserve(messages.size());
    for (const MemoryMessage &m : messages)
        msgs.append({ m.role, m.content });
    if (done)
        m_adds.insert(token, std::move(done));
    m_client->add(msgs, token, metadata);
}

void Mem0MemoryBackend::onAdded(quint64 token, bool ok, const QString &error)
{
    const AddDone done = m_adds.take(token);
    if (done)
        done(token, ok, error);
}
