#include "AgentPolicy.h"

#include "OutputGuard.h"

// The policy's output guardrails live in OutputGuard, a stateless
// component with no policy state of its own; these statics forward so
// the policy's public surface (and every existing caller) is unchanged.

QString AgentPolicy::redactSecrets(const QString &text, const QStringList &knownSecrets)
{
    return OutputGuard::redactSecrets(text, knownSecrets);
}

bool AgentPolicy::looksLikePromptInjection(const QString &text)
{
    return OutputGuard::looksLikePromptInjection(text);
}

QString AgentPolicy::stripInvisible(const QString &text, bool *changed)
{
    return OutputGuard::stripInvisible(text, changed);
}

QString AgentPolicy::displaySafe(const QString &text, int maxChars, bool *hidden)
{
    return OutputGuard::displaySafe(text, maxChars, hidden);
}

QString AgentPolicy::neutralizeFence(const QString &text)
{
    return OutputGuard::neutralizeFence(text);
}
