#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Buddy's clarifying questions: the `ask_user_question` tool. The model asks
// one to four multiple-choice questions instead of guessing; the user taps a
// chip (or types their own answer) and the choice goes back as the tool
// result. Pure functions only (no event loop, no network), so the rules can
// be unit tested. BuddyProvider does the pausing.
//
// The input comes from the model, which may have read hostile web content, so
// it is treated as untrusted text: invisible and bidi characters are stripped
// and every string is length-capped before it reaches the screen.
namespace uq {

constexpr int kMaxQuestions = 4;
constexpr int kMinOptions = 2;
constexpr int kMaxOptions = 4;
constexpr int kMaxQuestionChars = 300;
constexpr int kMaxHeaderChars = 12;
constexpr int kMaxLabelChars = 60;
constexpr int kMaxDescriptionChars = 200;
constexpr int kMaxOtherChars = 500;

struct Option {
    QString label;
    QString description;
};

struct Question {
    QString question;
    QString header;        // short chip title, e.g. "Format"
    bool multiSelect = false;
    QList<Option> options;
};

struct Parsed {
    QList<Question> questions;
    QString error;         // non-empty: the call is rejected, and this tells the model why
    bool valid() const { return error.isEmpty() && !questions.isEmpty(); }
};

// One answer per question: indices into the question's options, and/or text
// the user typed instead ("Other").
struct Answer {
    QList<int> selected;
    QString other;
    bool empty() const { return selected.isEmpty() && other.isEmpty(); }
};

// The JSON schema of the tool's input.
QJsonObject inputSchema();

// Validates and sanitises the tool input.
Parsed parse(const QJsonObject &input);

// For the UI: [{ question, header, multiSelect, options: [{ label, description }] }].
QVariantList toVariant(const QList<Question> &questions);

// The UI's answers, [{ selected: [index, ...], other: "text" }] one per
// question, made safe: out-of-range and repeated indices are dropped, a
// single-select question keeps at most one, text is trimmed and capped.
QList<Answer> normalizeAnswers(const QList<Question> &questions, const QVariantList &raw);

// True when every question has an answer.
bool complete(const QList<Answer> &answers);

// The tool result the model reads.
QString formatAnswers(const QList<Question> &questions, const QList<Answer> &answers);

// One line for the trace row / approval-style title.
QString summary(const QList<Question> &questions);

// ---- answers given out loud ------------------------------------------------------
// In a voice conversation the user answers in words: "the second one", "PDF please",
// "yes, go ahead". These turn what they said into the same Answer / decision the cards
// produce. They only ever pick among the options on offer or pass the user's own words
// along as text; a spoken sentence can never select something that was not offered.

// The best option(s) for `said` (an ordinal, a number, or words from a label); when no
// option fits, the words themselves become the user's own answer.
Answer matchSpoken(const Question &q, const QString &said);
// Several questions answered in one go: parts split by ";" or new lines go to the
// questions in order; one part answers every question.
QList<Answer> matchSpokenAll(const QList<Question> &questions, const QString &said);

enum class Decision { Unclear, Approve, Deny, ApproveAlways };
// "yes", "go ahead" -> Approve; "no", "don't" -> Deny; "yes, always" -> ApproveAlways.
Decision spokenDecision(const QString &said);

// The questions as a person would read them out: "Which format? Options: PDF, Word, or text."
QString spokenForm(const QList<Question> &questions);

// The option to pick when nobody can be asked (auto-approve): the one labelled
// "(Recommended)", else the first.
int defaultOption(const Question &q);

} // namespace uq
