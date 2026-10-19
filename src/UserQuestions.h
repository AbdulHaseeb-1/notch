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

} // namespace uq
