#pragma once

#include <QObject>

class BuddyProvider;
class ChatSessions;

// Connects the two halves of rolling chat summaries: when ChatSessions says a
// chat has outgrown its verbatim history, the job goes to BuddyProvider (which
// owns the model connection), and what comes back is stored. Neither class
// knows about the other.
class SessionLink : public QObject
{
    Q_OBJECT
public:
    SessionLink(ChatSessions *sessions, BuddyProvider *provider, QObject *parent = nullptr);

private:
    ChatSessions *m_sessions;
    BuddyProvider *m_provider;
};
