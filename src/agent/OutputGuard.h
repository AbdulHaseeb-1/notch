#pragma once

#include <QString>
#include <QStringList>

// The output guardrails: pure functions over text, with no policy state.
//
// Everything the model produces and everything untrusted that the model
// or the user is about to read passes through these: secrets are
// redacted before text is stored or shown, invisible characters used
// for ASCII smuggling are stripped (or made visible on approval cards),
// prompt-injection phrasing is flagged, and the fences around untrusted
// content cannot be closed early by the content itself.
//
// AgentPolicy exposes this component through its own static methods for
// compatibility; new code can use OutputGuard directly.
class OutputGuard
{
public:
    // Replaces known secrets and anything shaped like a credential
    // (provider keys, bearer tokens, key = value pairs, private keys)
    // with "[redacted]".
    static QString redactSecrets(const QString &text, const QStringList &knownSecrets = {});
    // Heuristic scan for instruction-override and exfiltration phrasing
    // in untrusted text.
    static bool looksLikePromptInjection(const QString &text);
    // Drops invisible "tag" characters (ASCII smuggling) and zero-width
    // characters from text the model will read.
    static QString stripInvisible(const QString &text, bool *changed = nullptr);
    // Makes bidi overrides and zero-width characters visible, so the
    // approval card shows exactly what will run. Long text keeps its head
    // and tail with a marker in between.
    static QString displaySafe(const QString &text, int maxChars, bool *hidden = nullptr);
    // Escapes <untrusted_content> tags inside fenced data so content cannot
    // close the fence early.
    static QString neutralizeFence(const QString &text);
};
