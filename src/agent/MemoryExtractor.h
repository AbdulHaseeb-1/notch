// P8: end-of-run fact extraction (the Mem0-style pipeline's first stage).
// Scans the run's USER messages for durable-fact signals — explicit memory
// directives ("remember that…", "don't forget…") and first-person durable
// statements ("my editor is…", "I prefer…"). Conservative by design: a
// false fact in long-term memory is worse than a missed one, so anything
// ambiguous is skipped. Candidates are deduplicated against the store by
// the caller (BM25 ADD/UPDATE).
#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

inline QStringList extractFactCandidates(const QStringList &userTexts)
{
    QStringList out;
    static const QRegularExpression sentenceRe(
        QStringLiteral("[.!?]\\s+|\\n+"));
    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral("^\\s*(?:please\\s+)?remember(?:\\s+that)?\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^\\s*don'?t forget(?:\\s+that)?\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^\\s*note(?:\\s+that)?\\s+(.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
        // M6: durable *attributes* only — the "my X is" shape matches
        // transient complaints ("my laptop is slow today") just as easily,
        // so it is capped at a single trailing object phrase (<= 60 chars).
        // "use"/"need" were dropped from the first-person list: "I need
        // this by Friday" is a task, not a trait.
        QRegularExpression(QStringLiteral("^\\s*(my\\s+\\w+(?:\\s+\\w+){0,2}?\\s+is\\s+[^,.]{4,60})$"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("^\\s*(i\\s+(?:prefer|like|love|hate|always|never)\\s+.+)$"),
                           QRegularExpression::CaseInsensitiveOption),
    };

    for (const QString &text : userTexts) {
        // Harness-injected turns ("[Tool results]", "[Plan accepted…") are
        // not the user's voice; never mine them for facts.
        if (text.trimmed().startsWith(QLatin1Char('[')))
            continue;
        const QStringList sentences = text.split(sentenceRe, Qt::SkipEmptyParts);
        for (const QString &raw : sentences) {
            const QString s = raw.trimmed();
            if (s.size() < 8 || s.size() > 240 || s.contains(QLatin1Char('?')))
                continue;
            for (const QRegularExpression &rx : patterns) {
                const QRegularExpressionMatch m = rx.match(s);
                if (!m.hasMatch())
                    continue;
                QString fact = m.captured(1).trimmed();
                if (fact.size() < 8)
                    break;
                // Normalize: capitalize first letter, end with a period.
                fact[0] = fact.at(0).toUpper();
                if (!fact.endsWith(QLatin1Char('.')))
                    fact += QLatin1Char('.');
                if (!out.contains(fact))
                    out << fact;
                break;   // first matching pattern per sentence wins
            }
        }
    }
    return out;
}
