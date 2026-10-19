#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "SessionFormat.h"

// Buddy's chats, kept on disk.
//
// Until now a conversation was the last eight messages held in the UI and
// gone when Noch closed. A session is a whole chat: every question and answer,
// saved as <AppData>/sessions/<id>.json after each turn, so it can be resumed
// after a restart, branched into a new chat that keeps the history up to a
// point, or deleted.
//
// Long chats stay affordable through a rolling summary. When enough older
// turns have piled up, summaryWanted() fires; whoever owns a model (BuddyProvider)
// writes a summary of those turns and hands it back through setSummary(). From
// then on context() sends "[Conversation summary] ..." plus only the newer
// turns word for word. Until a summary exists (or when asking for one fails)
// the provider's old one-line note about earlier topics is the fallback, so a
// chat never loses its thread because the summary is missing.
//
// This class does no networking and no model calls.
class ChatSessions : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentId READ currentId NOTIFY currentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged)
    Q_PROPERTY(int currentTurns READ currentTurns NOTIFY currentChanged)
    // [{ id, title, preview, updated, turns, parentId, parentTitle, summarized, current }], newest first
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)

public:
    static constexpr int kMaxSessions = 100;
    static constexpr int kMaxTurns = 400;            // per session; older ones are dropped
    static constexpr int kMaxAnswerChars = 24000;    // stored per answer
    static constexpr int kMaxTitleChars = 60;
    static constexpr int kKeepVerbatimTurns = 4;     // the newest turns a summary never replaces
    static constexpr int kSummarizeAfterChars = 12000;
    static constexpr int kSummarizeAfterTurns = 10;
    static constexpr int kMaxContextTurns = 60;
    static constexpr int kMaxSummaryChars = 4000;
    static constexpr const char *kSummaryPrefix = kConversationSummaryPrefix;

    explicit ChatSessions(QObject *parent = nullptr);
    explicit ChatSessions(const QString &root, QObject *parent = nullptr);

    // Where sessions live; loads the index. Tests point it at a temp folder.
    void setRoot(const QString &root);
    QString root() const { return m_root; }
    Q_INVOKABLE QString sessionsPath() const;

    QString currentId() const { return m_current.id; }
    QString currentTitle() const { return m_current.title; }
    int currentTurns() const { return int(m_current.turns.size()); }
    QVariantList sessions() const;

    // ---- the current chat ------------------------------------------------------
    // Start over: the next turn opens a new session (nothing is created until then).
    Q_INVOKABLE void newChat();
    // Add a finished question and its answer. Creates the session on the first turn.
    Q_INVOKABLE void appendTurn(const QString &question, const QString &answer);
    // What to send with the next question: the summary (if any) as a first
    // user/assistant pair, then the turns after it word for word.
    Q_INVOKABLE QVariantList context() const;
    Q_INVOKABLE QVariantMap lastTurn() const;

    // ---- other chats -------------------------------------------------------------
    Q_INVOKABLE bool open(const QString &id);
    // A new chat holding the turns of `id` (default: the current chat) up to and
    // including turn `atTurn` (0-based; -1 = all of them). The original is left
    // as it is. The branch becomes the current chat. Returns its id, or "".
    Q_INVOKABLE QString branch(const QString &id = QString(), int atTurn = -1);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool rename(const QString &id, const QString &title);
    Q_INVOKABLE void clearAll();
    // { id, title, parentId, summary, summarizedTurns, turns: [{ q, a, t }] } (empty if unknown)
    Q_INVOKABLE QVariantMap session(const QString &id) const;

    // ---- summaries ---------------------------------------------------------------
    // The turns a summary should now fold in: { id, prior, upTo, turns: [{ q, a }] }.
    // `keep` newest turns stay out of it (default kKeepVerbatimTurns). Empty when there is nothing to fold.
    Q_INVOKABLE QVariantMap summaryJob(const QString &id, int keep = -1) const;
    // Store a finished summary covering the first `upTo` turns. Ignored if it is
    // empty, older than the one we have, beyond the turns that exist, or the chat is gone.
    Q_INVOKABLE bool setSummary(const QString &id, const QString &summary, int upTo);
    // Asking for the summary failed: do not ask again until two more turns exist.
    Q_INVOKABLE void noteSummaryFailed(const QString &id);
    // The user asked for it (/compact): fire summaryWanted() now if a job exists.
    Q_INVOKABLE bool requestSummary(const QString &id = QString(), int keep = 2);

signals:
    void currentChanged();
    void sessionsChanged();
    void summaryWanted(const QString &id, int keep);

private:
    struct Turn {
        QString q;
        QString a;
        qint64 t = 0;
    };
    struct Session {
        QString id;
        QString title;
        qint64 created = 0;
        qint64 updated = 0;
        QString parentId;
        QString parentTitle;
        int branchedAtTurn = 0;
        QString summary;
        int summarizedTurns = 0;
        QList<Turn> turns;
    };
    struct IndexEntry {
        QString id, title, preview, parentId, parentTitle;
        qint64 updated = 0;
        int turns = 0;
        bool summarized = false;
    };

    QString filePath(const QString &id) const;
    bool load(const QString &id, Session *out) const;
    bool save(const Session &s) const;
    void reindex();
    IndexEntry entryOf(const Session &s) const;
    void touchIndex(const Session &s);
    void prune();
    bool wantsSummary(const Session &s) const;
    static QString newId();
    static QString titleFor(const QString &question);

    QString m_root;
    Session m_current;
    QList<IndexEntry> m_index;           // newest first
    QHash<QString, int> m_summaryFailedAt;
};
