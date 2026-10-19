#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

#include "AgentPolicy.h"

// The "always allow" rules a user has saved, kept in a JSON file under Noch's
// private data folder (<AppData>/permission-rules.json). That folder is one
// of the places the agent can neither write nor, without asking, read, so a
// run cannot grant itself standing permission. Pure file handling; the
// matching lives in AgentPolicy.
class SavedRules
{
public:
    static constexpr int kMaxRules = 200;

    explicit SavedRules(const QString &path = QString());

    // Loads the file (a missing or unreadable file means no rules).
    void setPath(const QString &path);
    QString path() const { return m_path; }

    const QList<AgentPolicy::SavedRule> &rules() const { return m_rules; }
    // Adds a rule unless an identical one exists or the list is full. Assigns the id
    // and the time, writes the file. Returns whether it was added.
    bool add(AgentPolicy::SavedRule rule);
    bool remove(const QString &id);
    // Whether this very rule (same tool, kind, pattern and folder) is saved.
    bool contains(const AgentPolicy::SavedRule &rule) const;
    void clear();

    // [{ id, tool, kind, pattern, scope, label, createdAt }] for the settings list.
    QVariantList toVariant() const;

    // A rule read from disk is only trusted if it is still one the policy
    // could have suggested; a hand-edited file cannot slip in `rm -rf`.
    static bool acceptable(const AgentPolicy::SavedRule &rule);

private:
    void load();
    bool save() const;

    QString m_path;
    QList<AgentPolicy::SavedRule> m_rules;
};
