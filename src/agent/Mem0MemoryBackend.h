// The Mem0 implementation of MemoryBackend: a thin adapter over
// Mem0Client, which keeps owning the transport, the readiness/rest logic
// and the provider supplier. This class only translates — Mem0Client's
// signal API (one search slot, token-tagged adds) into the interface's
// callbacks, and mem0::Hit into MemoryHit.
//
// The client's single search slot is mirrored faithfully: starting a
// recall or search supersedes any in-flight one, whose callback is
// dropped and never fires (the client aborts its reply the same way).
// Adds, by contrast, run concurrently on the client, so each keeps its
// own callback, keyed by token.
#pragma once

#include "Mem0Client.h"
#include "MemoryBackend.h"

#include <QHash>
#include <QObject>

class Mem0MemoryBackend : public QObject, public MemoryBackend
{
    Q_OBJECT
public:
    // `client` is not owned (BuddyProvider owns it and exposes it to the
    // settings UI); both must share a parent/lifetime.
    explicit Mem0MemoryBackend(Mem0Client *client, QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("mem0"); }
    bool recallReady() const override { return m_client->searchReady(); }
    bool saveReady() const override { return m_client->ready(); }

    void recall(const QString &query, int topK, quint64 token, RecallDone done) override;
    void search(const QString &query, int topK, quint64 token, RecallDone done) override;
    void cancelRecall() override;
    void addExchange(const QList<MemoryMessage> &messages, quint64 token,
                     const QJsonObject &metadata, AddDone done) override;

    Mem0Client *client() const { return m_client; }

private:
    void startSearch(const QString &query, int topK, quint64 token, RecallDone done);
    void onSearched(quint64 token, const QList<mem0::Hit> &hits, int ms, const QString &error);
    void onAdded(quint64 token, bool ok, const QString &error);

    Mem0Client *m_client;
    // The one search slot: {token, callback} of the in-flight recall or
    // search; empty when idle. Replaced (dropped) by every new search.
    bool m_searchPending = false;
    quint64 m_searchToken = 0;
    RecallDone m_searchDone;
    QHash<quint64, AddDone> m_adds;
};
