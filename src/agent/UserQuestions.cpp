#include "UserQuestions.h"

#include <QJsonArray>
#include <QHash>
#include <QPair>
#include <QRegularExpression>
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

// ---- spoken answers ------------------------------------------------------------------

namespace {

QStringList words(const QString &text)
{
    QString t = text.toLower();
    t.remove(QLatin1Char('\''));                         // don't -> dont
    for (QChar &c : t)
        if (!c.isLetterOrNumber())
            c = QLatin1Char(' ');
    return t.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

bool isFiller(const QString &w)
{
    static const QSet<QString> filler = { QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
                                          QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("and"),
                                          QStringLiteral("or"), QStringLiteral("it"), QStringLiteral("for"),
                                          QStringLiteral("in"), QStringLiteral("with"), QStringLiteral("on") };
    return w.size() < 2 || filler.contains(w);
}

// 1-based position named by a spoken ordinal or number ("second", "option 2", "number three"), or 0.
int spokenIndex(const QStringList &w)
{
    static const QHash<QString, int> ord = {
        { QStringLiteral("first"), 1 }, { QStringLiteral("1st"), 1 }, { QStringLiteral("second"), 2 }, { QStringLiteral("2nd"), 2 },
        { QStringLiteral("third"), 3 }, { QStringLiteral("3rd"), 3 }, { QStringLiteral("fourth"), 4 }, { QStringLiteral("4th"), 4 },
        { QStringLiteral("last"), 99 } };
    static const QHash<QString, int> card = {
        { QStringLiteral("1"), 1 }, { QStringLiteral("2"), 2 }, { QStringLiteral("3"), 3 }, { QStringLiteral("4"), 4 },
        { QStringLiteral("one"), 1 }, { QStringLiteral("two"), 2 }, { QStringLiteral("three"), 3 }, { QStringLiteral("four"), 4 } };
    static const QSet<QString> intro = { QStringLiteral("option"), QStringLiteral("number"), QStringLiteral("choice"),
                                         QStringLiteral("answer"), QStringLiteral("pick"), QStringLiteral("take") };
    for (int i = 0; i < w.size(); ++i) {
        if (ord.contains(w.at(i)))
            return ord.value(w.at(i));
        // A bare number only counts when it is introduced ("option two") or nearly all they said.
        if (card.contains(w.at(i)) && ((i > 0 && intro.contains(w.at(i - 1))) || w.size() <= 2))
            return card.value(w.at(i));
    }
    return 0;
}

double labelScore(const Option &o, const QString &saidLower, const QStringList &saidWords)
{
    const QString label = o.label.toLower().simplified();
    if (!label.isEmpty() && saidLower.contains(label))
        return 1.0;
    QStringList lw;
    for (const QString &w : words(label))
        if (!isFiller(w))
            lw << w;
    if (lw.isEmpty())
        return 0.0;
    int hit = 0;
    for (const QString &w : std::as_const(lw))
        if (saidWords.contains(w))
            ++hit;
    const double share = double(hit) / double(lw.size());
    // "word" for "Word document": everything they said that matters is part of the label.
    QStringList sw;
    for (const QString &w : saidWords)
        if (!isFiller(w))
            sw << w;
    bool allInLabel = !sw.isEmpty();
    for (const QString &w : std::as_const(sw))
        if (!lw.contains(w))
            allInLabel = false;
    return allInLabel ? std::max(share, 0.75) : share;
}

} // namespace

Answer matchSpoken(const Question &q, const QString &said)
{
    Answer a;
    const QString lower = AgentPolicy::stripInvisible(said).simplified().toLower();
    if (lower.isEmpty() || q.options.isEmpty())
        return a;
    const QStringList w = words(lower);

    // An ordinal or number first: "the second one".
    const int pos = spokenIndex(w);
    if (pos > 0) {
        const int idx = pos == 99 ? int(q.options.size()) - 1 : pos - 1;
        if (idx >= 0 && idx < q.options.size()) {
            a.selected << idx;
            return a;
        }
    }
    // Then words from the labels.
    QList<QPair<int, double>> scored;
    for (int i = 0; i < q.options.size(); ++i) {
        const double sc = labelScore(q.options.at(i), lower, w);
        if (sc >= 0.6)
            scored.append({ i, sc });
    }
    if (!scored.isEmpty()) {
        if (q.multiSelect) {
            for (const auto &p : std::as_const(scored))
                a.selected << p.first;
            return a;
        }
        std::sort(scored.begin(), scored.end(), [](const auto &x, const auto &y) { return x.second > y.second; });
        // Two options named equally well is not an answer: the words go back as the user's own.
        if (scored.size() == 1 || scored.at(0).second > scored.at(1).second + 0.01) {
            a.selected << scored.first().first;
            return a;
        }
    }
    a.other = clean(said, kMaxOtherChars);
    return a;
}

QList<Answer> matchSpokenAll(const QList<Question> &questions, const QString &said)
{
    QStringList parts;
    for (const QString &p : said.split(QRegularExpression(QStringLiteral("[;\\n]")), Qt::SkipEmptyParts))
        if (!p.trimmed().isEmpty())
            parts << p.trimmed();
    QList<Answer> out;
    for (int i = 0; i < questions.size(); ++i) {
        const QString part = parts.size() == questions.size() ? parts.at(i) : said;
        out << matchSpoken(questions.at(i), part);
    }
    return out;
}

Decision spokenDecision(const QString &said)
{
    static const QSet<QString> yes = {
        QStringLiteral("yes"), QStringLiteral("yeah"), QStringLiteral("yep"), QStringLiteral("yup"), QStringLiteral("sure"),
        QStringLiteral("ok"), QStringLiteral("okay"), QStringLiteral("approve"), QStringLiteral("approved"), QStringLiteral("allow"),
        QStringLiteral("proceed"), QStringLiteral("confirm"), QStringLiteral("fine"), QStringLiteral("granted"),
        QStringLiteral("absolutely"), QStringLiteral("definitely"), QStringLiteral("correct"), QStringLiteral("affirmative") };
    static const QSet<QString> no = {
        QStringLiteral("no"), QStringLiteral("nope"), QStringLiteral("nah"), QStringLiteral("deny"), QStringLiteral("denied"),
        QStringLiteral("dont"), QStringLiteral("stop"), QStringLiteral("cancel"), QStringLiteral("decline"), QStringLiteral("reject"),
        QStringLiteral("never"), QStringLiteral("refuse"), QStringLiteral("negative"), QStringLiteral("abort") };
    const QString lower = AgentPolicy::stripInvisible(said).simplified().toLower();
    const QStringList w = words(lower);
    int firstYes = -1, firstNo = -1;
    for (int i = 0; i < w.size(); ++i) {
        const QString &x = w.at(i);
        const bool next = i + 1 < w.size();
        if (firstYes < 0 && (yes.contains(x) || (x == QLatin1String("go") && next && w.at(i + 1) == QLatin1String("ahead"))
                             || (x == QLatin1String("do") && next && w.at(i + 1) == QLatin1String("it"))))
            firstYes = i;
        // "do not", "dont", "no" all refuse; a bare "not" too ("not now").
        if (firstNo < 0 && (no.contains(x) || x == QLatin1String("not")))
            firstNo = i;
    }
    if (firstYes < 0 && firstNo < 0)
        return Decision::Unclear;
    // The first verdict stated wins: "no, stop" refuses, "yes but not the second one" approves.
    if (firstNo >= 0 && (firstYes < 0 || firstNo < firstYes))
        return Decision::Deny;
    const bool always = lower.contains(QLatin1String("always")) || lower.contains(QLatin1String("every time"))
                        || lower.contains(QLatin1String("from now on"));
    return always ? Decision::ApproveAlways : Decision::Approve;
}

QString spokenForm(const QList<Question> &questions)
{
    QStringList out;
    for (int i = 0; i < questions.size(); ++i) {
        const Question &q = questions.at(i);
        QStringList labels;
        for (const Option &o : q.options)
            labels << o.label;
        QString opts;
        if (labels.size() == 1)
            opts = labels.first();
        else if (!labels.isEmpty())
            opts = labels.mid(0, labels.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(", or ") + labels.last();
        out << (questions.size() > 1 ? QStringLiteral("Question %1: ").arg(i + 1) : QString())
                   + q.question + (opts.isEmpty() ? QString() : QStringLiteral(" Options: %1.").arg(opts))
                   + (q.multiSelect ? QStringLiteral(" (they may pick more than one)") : QString());
    }
    return out.join(QLatin1Char(' '));
}

int defaultOption(const Question &q)
{
    for (int i = 0; i < q.options.size(); ++i)
        if (q.options.at(i).label.contains(QLatin1String("recommended"), Qt::CaseInsensitive))
            return i;
    return 0;
}

} // namespace uq
