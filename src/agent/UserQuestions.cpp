#include "UserQuestions.h"

#include <QJsonArray>
#include <QSet>

#include "AgentPolicy.h"

#include <algorithm>

namespace uq {

namespace {

QString clean(const QString &raw, int maxChars)
{
    QString s = AgentPolicy::stripInvisible(raw).simplified();
    if (s.size() > maxChars)
        s = s.left(maxChars - 1).trimmed() + QStringLiteral("…");
    return s;
}

QJsonObject str(const QString &description)
{
    return QJsonObject{ { QStringLiteral("type"), QStringLiteral("string") },
                        { QStringLiteral("description"), description } };
}

} // namespace

QJsonObject inputSchema()
{
    const QJsonObject option{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("label"), str(QStringLiteral("The choice, 1-5 words (max %1 characters).").arg(kMaxLabelChars)) },
            { QStringLiteral("description"), str(QStringLiteral("One short sentence on what this choice means or implies.")) } } },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("label") } } };
    const QJsonObject question{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("question"), str(QStringLiteral("The complete question, ending with a question mark.")) },
            { QStringLiteral("header"), str(QStringLiteral("Very short title for the question (max %1 characters), e.g. \"Format\".").arg(kMaxHeaderChars)) },
            { QStringLiteral("multiSelect"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("boolean") },
                { QStringLiteral("description"), QStringLiteral("True when several options can apply at once.") } } },
            { QStringLiteral("options"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("array") },
                { QStringLiteral("minItems"), kMinOptions },
                { QStringLiteral("maxItems"), kMaxOptions },
                { QStringLiteral("items"), option },
                { QStringLiteral("description"), QStringLiteral("%1-%2 distinct choices. Do not add an \"Other\" option: the user can always type their own answer.")
                                                     .arg(kMinOptions).arg(kMaxOptions) } } } } },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("question"), QStringLiteral("options") } } };
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("object") },
        { QStringLiteral("properties"), QJsonObject{
            { QStringLiteral("questions"), QJsonObject{
                { QStringLiteral("type"), QStringLiteral("array") },
                { QStringLiteral("minItems"), 1 },
                { QStringLiteral("maxItems"), kMaxQuestions },
                { QStringLiteral("items"), question },
                { QStringLiteral("description"), QStringLiteral("1-%1 questions to ask together.").arg(kMaxQuestions) } } } } },
        { QStringLiteral("required"), QJsonArray{ QStringLiteral("questions") } } };
}

Parsed parse(const QJsonObject &input)
{
    Parsed out;
    const QJsonValue qv = input.value(QStringLiteral("questions"));
    if (!qv.isArray() || qv.toArray().isEmpty()) {
        out.error = QStringLiteral("'questions' must be an array of 1-%1 questions.").arg(kMaxQuestions);
        return out;
    }
    const QJsonArray arr = qv.toArray();
    if (arr.size() > kMaxQuestions) {
        out.error = QStringLiteral("Ask at most %1 questions at once (got %2).").arg(kMaxQuestions).arg(arr.size());
        return out;
    }
    QSet<QString> seenQuestions;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject qo = arr.at(i).toObject();
        Question q;
        q.question = clean(qo.value(QStringLiteral("question")).toString(), kMaxQuestionChars);
        if (q.question.isEmpty()) {
            out.error = QStringLiteral("Question %1 has no 'question' text.").arg(i + 1);
            return out;
        }
        if (seenQuestions.contains(q.question.toLower())) {
            out.error = QStringLiteral("Question %1 repeats an earlier question.").arg(i + 1);
            return out;
        }
        seenQuestions.insert(q.question.toLower());
        q.header = clean(qo.value(QStringLiteral("header")).toString(), kMaxHeaderChars);
        q.multiSelect = qo.value(QStringLiteral("multiSelect")).toBool(false);
        const QJsonArray opts = qo.value(QStringLiteral("options")).toArray();
        if (opts.size() < kMinOptions || opts.size() > kMaxOptions) {
            out.error = QStringLiteral("Question %1 needs %2-%3 options (got %4).")
                            .arg(i + 1).arg(kMinOptions).arg(kMaxOptions).arg(opts.size());
            return out;
        }
        QSet<QString> seenLabels;
        for (const QJsonValue &ov : opts) {
            Option o;
            // An option may be a bare string as well as {label, description}.
            if (ov.isString()) {
                o.label = clean(ov.toString(), kMaxLabelChars);
            } else {
                o.label = clean(ov.toObject().value(QStringLiteral("label")).toString(), kMaxLabelChars);
                o.description = clean(ov.toObject().value(QStringLiteral("description")).toString(), kMaxDescriptionChars);
            }
            if (o.label.isEmpty()) {
                out.error = QStringLiteral("An option of question %1 has no label.").arg(i + 1);
                return out;
            }
            if (seenLabels.contains(o.label.toLower())) {
                out.error = QStringLiteral("Question %1 lists the option \"%2\" twice.").arg(i + 1).arg(o.label);
                return out;
            }
            seenLabels.insert(o.label.toLower());
            q.options.append(o);
        }
        out.questions.append(q);
    }
    return out;
}

QVariantList toVariant(const QList<Question> &questions)
{
    QVariantList out;
    for (const Question &q : questions) {
        QVariantList opts;
        for (const Option &o : q.options)
            opts.append(QVariantMap{ { QStringLiteral("label"), o.label }, { QStringLiteral("description"), o.description } });
        out.append(QVariantMap{ { QStringLiteral("question"), q.question },
                                { QStringLiteral("header"), q.header },
                                { QStringLiteral("multiSelect"), q.multiSelect },
                                { QStringLiteral("options"), opts } });
    }
    return out;
}

QList<Answer> normalizeAnswers(const QList<Question> &questions, const QVariantList &raw)
{
    QList<Answer> out;
    for (int i = 0; i < questions.size(); ++i) {
        Answer a;
        if (i < raw.size()) {
            const QVariantMap m = raw.at(i).toMap();
            QSet<int> seen;
            for (const QVariant &v : m.value(QStringLiteral("selected")).toList()) {
                bool ok = false;
                const int idx = v.toInt(&ok);
                if (!ok || idx < 0 || idx >= questions.at(i).options.size() || seen.contains(idx))
                    continue;
                seen.insert(idx);
                a.selected.append(idx);
            }
            std::sort(a.selected.begin(), a.selected.end());
            if (!questions.at(i).multiSelect && a.selected.size() > 1)
                a.selected = a.selected.mid(0, 1);
            a.other = AgentPolicy::stripInvisible(m.value(QStringLiteral("other")).toString()).trimmed();
            if (a.other.size() > kMaxOtherChars)
                a.other = a.other.left(kMaxOtherChars);
            // Typing your own answer to a single-choice question replaces the chip.
            if (!questions.at(i).multiSelect && !a.other.isEmpty())
                a.selected.clear();
        }
        out.append(a);
    }
    return out;
}

bool complete(const QList<Answer> &answers)
{
    for (const Answer &a : answers)
        if (a.empty())
            return false;
    return !answers.isEmpty();
}

QString formatAnswers(const QList<Question> &questions, const QList<Answer> &answers)
{
    QStringList lines;
    lines << QStringLiteral("The user answered:");
    for (int i = 0; i < questions.size(); ++i) {
        const Question &q = questions.at(i);
        const Answer a = i < answers.size() ? answers.at(i) : Answer();
        QStringList parts;
        for (int idx : a.selected)
            parts << q.options.at(idx).label;
        if (!a.other.isEmpty())
            parts << QStringLiteral("(their own answer) %1").arg(a.other);
        lines << QStringLiteral("Q%1. %2").arg(i + 1).arg(q.question);
        lines << QStringLiteral("A%1. %2").arg(i + 1).arg(parts.isEmpty() ? QStringLiteral("(no answer)") : parts.join(QStringLiteral("; ")));
    }
    lines << QStringLiteral("Carry on with these answers; do not ask the same thing again.");
    return lines.join(QLatin1Char('\n'));
}

QString summary(const QList<Question> &questions)
{
    if (questions.isEmpty())
        return QStringLiteral("Ask a question");
    QString first = questions.first().question;
    if (first.size() > 56)
        first = first.left(55).trimmed() + QStringLiteral("…");
    return questions.size() > 1 ? QStringLiteral("Ask: %1 (+%2 more)").arg(first).arg(questions.size() - 1)
                                : QStringLiteral("Ask: %1").arg(first);
}

} // namespace uq
