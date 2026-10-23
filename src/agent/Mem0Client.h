// Long-term memory through Mem0.
//
// One client, three places to keep memories. All three speak the hosted Mem0
// Platform API (POST /v3/memories/add/ and /v3/memories/search/, header
// "Authorization: Token <key>"); the local Docker stack in docker/mem0 serves the
// same API, so the only differences are the address and the key:
//   Local     http://127.0.0.1:8765, key read from %LOCALAPPDATA%\noch\mem0-local.key
//             (written by docker/mem0/setup.ps1). The default: nothing leaves the PC.
//   Platform  https://api.mem0.ai with the user's Mem0 API key.
//   Custom    any address that serves the same API, with its own key.
//
// Memory is an optional extra: it fails open. A timeout, an HTTP error or an
// unreadable reply means "behave as if there were no Mem0", and repeated
// failures rest the client for a while so a stopped container costs one try.
// Replies are untrusted text: recalled memories are only ever shown to the model
// as quoted data (see mem0::recallSection), never as instructions.
#pragma once

#include <QElapsedTimer>
#include <functional>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QNetworkReply;

namespace mem0 {

struct Hit {
    QString id;
    QString memory;
    double score = 0.0;
};

struct Message {
    QString role;      // "user" | "assistant"
    QString content;
};

constexpr const char *kLocalBase = "http://127.0.0.1:8765";
constexpr const char *kPlatformBase = "https://api.mem0.ai";

// "local" | "platform" | "custom"; anything else is "local".
QString normalizeBackend(const QString &backend);
// The address a backend talks to, no trailing slash. `custom` is used for "custom" only.
QString baseFor(const QString &backend, const QString &custom);
// Where the local stack's setup script leaves its generated key.
QString localKeyPath();
QString readLocalKey();
// True for a host that is this computer.
bool isLoopbackHost(const QString &host);

QJsonObject buildAddBody(const QList<Message> &messages, const QString &userId,
                         const QJsonObject &metadata = QJsonObject());
QJsonObject buildSearchBody(const QString &query, const QString &userId, int topK, double threshold);
// {"results":[{"id","memory","score"}]} (a bare array is accepted too). Never throws;
// entries without text are skipped.
QList<Hit> parseSearch(const QByteArray &body);
// "HTTP 401 - message" from an error body, or a plain transport message.
QString errorSummary(const QByteArray &body, int status, const QString &transportError);

// The block added to the system prompt. Empty when there are no hits. Memories are
// flattened to one line, capped, and fenced so they read as data.
QString recallSection(const QList<Hit> &hits);
// A line per hit for a tool result.
QString recallLines(const QList<Hit> &hits);

} // namespace mem0

class Mem0Client : public QObject
{
    Q_OBJECT
public:
    explicit Mem0Client(QObject *parent = nullptr);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    void setConfig(const QString &backend, const QString &customUrl, const QString &key,
                   const QString &userId);
    QString backend() const { return m_backend; }
    QString customUrl() const { return m_custom; }
    QString userId() const { return m_userId; }
    QString base() const { return mem0::baseFor(m_backend, m_custom); }
    // The key in use: the typed one, or for Local the one setup.ps1 left behind.
    QString effectiveKey() const;
    // Enabled, has an address and a key, and is not resting after repeated failures.
    bool ready() const;
    // Recall may run before a reply only when this is true. It has its own failure count and
    // rest, so a successful save can never hide a search that keeps timing out.
    bool searchReady() const;
    int searchRestSecondsLeft() const;
    int restSecondsLeft() const;

    // Search; emits searched() exactly once unless cancelled.
    void search(const QString &query, quint64 token, int topK = 6);
    // Save an exchange in the background. Emits added() once.
    void add(const QList<mem0::Message> &messages, quint64 token, const QJsonObject &metadata = QJsonObject());
    // Checks the address and the key; answers testFinished().
    void test();
    void cancelSearch();
    void cancelAll();

    // The model provider to hand to the *local* server with each request: { llm: {base_url,
    // api_key, model}, embedder: {...} } (the app's own chat provider, so the server needs no
    // keys of its own). Never sent anywhere but this computer; empty = send nothing.
    using ProviderFn = std::function<QJsonObject()>;
    void setProviderSupplier(ProviderFn fn) { m_provider = std::move(fn); }

    // Tuning (tests shorten these).
    int searchTimeoutMs = 2500;   // recall delays the first model turn: not worth waiting long
    int addTimeoutMs = 120000;    // extraction runs a model; Noch does not wait for it
    int testTimeoutMs = 15000;
    int restAfterFailures = 2;
    int restMs = 120000;
    int searchRestMs = 600000;    // a search that keeps timing out pauses recall for ten minutes

signals:
    // `error` is empty exactly when the request worked.
    void searched(quint64 token, const QList<mem0::Hit> &hits, int ms, const QString &error);
    void added(quint64 token, bool ok, const QString &error);
    void testFinished(bool ok, const QString &message);

private:
    QNetworkRequest request(const QString &path, int timeout) const;
    QByteArray payload(QJsonObject body) const;
    ProviderFn m_provider;
    void noteSuccess();
    void noteFailure();
    void noteSearchSuccess();
    void noteSearchFailure();

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_searchReply;
    QPointer<QNetworkReply> m_testReply;
    QList<QPointer<QNetworkReply>> m_addReplies;
    QString m_backend = QStringLiteral("local");
    QString m_custom;
    QString m_key;
    QString m_userId = QStringLiteral("noch");
    bool m_enabled = false;   // the app's settings switch it on (tests never talk to a real Mem0)
    int m_failures = 0;
    QElapsedTimer m_rest;
    bool m_resting = false;
    int m_searchFailures = 0;
    QElapsedTimer m_searchRest;
    bool m_searchResting = false;
    // Local's generated key is read from disk; ready() runs on every prompt build,
    // so the file is re-read at most every few seconds.
    mutable QString m_fileKey;
    mutable QElapsedTimer m_fileKeyAge;
};

Q_DECLARE_METATYPE(QList<mem0::Hit>)
