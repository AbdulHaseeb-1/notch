// Typed request classification ("JEV").
//
// Before the agent loop starts, one small model (jev) labels the user's request:
// what it is (intent), how much it can change (risk), how big it is (effort),
// which specialist should take it and which deferred tools it will probably need.
//
// The reply is untrusted text, so nothing reaches the harness unless it parses
// into the enums below: an unknown intent, risk, effort or agent makes the whole
// result invalid, unknown tool names are dropped, and an out-of-range confidence
// is invalid. The harness only ever uses a valid result to *narrow* things
// (pick a specialist, pre-load tools, ask before a mismatching write); it never
// uses it to allow anything the permission policy would not.
//
// Two kinds of endpoint are understood, told apart by the address:
//  * Chat: an OpenAI-compatible /v1/chat/completions endpoint. The model is asked
//    for a JSON object (parseClassification).
//  * Decisions: a decision model (Jev, Mercury Decide, Span ...) behind a System
//    One / Decisions endpoint, for example https://openrouter.ai/api/v1/systemone.
//    These models do not write text: they answer typed yes/no questions with a
//    probability, so the request is a fixed set of questions about the text and
//    the label is the most probable answer (parseDecisions).
// Either way the classifier fails open: a timeout, an HTTP error or an invalid
// reply means "behave as if there were no classifier".
#pragma once

#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QNetworkReply;

namespace turnclass {

enum class Intent { Chat, Lookup, WebResearch, CodeEdit, ShellTask, DesktopControl,
                    Timer, LaunchApp, Plan, Unclear };
enum class Risk   { ReadOnly, WritesFiles, RunsCommands, ControlsScreen };
enum class Effort { Trivial, Simple, MultiStep };

// Canonical names, as sent to and accepted from the model ("WebResearch" ...).
QString intentName(Intent v);
QString riskName(Risk v);
QString effortName(Effort v);

// Confidence needed before a valid result is used at all, to switch the
// specialist, and to let the intent-alignment hook ask the user.
constexpr double kMinConfidence = 0.6;
constexpr double kAgentSwitchConfidence = 0.75;
constexpr double kAlignConfidence = 0.75;
// A reply without a confidence field counts as this (above kMinConfidence).
constexpr double kDefaultConfidence = 0.7;

struct Classification {
    Intent intent = Intent::Unclear;
    Risk risk = Risk::RunsCommands;       // pessimistic until the model says otherwise
    Effort effort = Effort::Simple;
    QString agent = QStringLiteral("buddy");   // "buddy" | "research" | "code"
    QStringList tools;                    // validated against the tool registry
    QStringList droppedTools;             // names the model made up
    double confidence = 0.0;
    bool valid = false;
    QString error;                        // why it is invalid

    QVariantMap toVariantMap() const;
    // "WebResearch / ReadOnly / MultiStep -> research (92%)"
    QString summary() const;
};

// Parses the model's reply text (a JSON object, optionally fenced or wrapped in
// prose; a bare intent label is accepted too). Never throws; check `valid`.
Classification parseClassification(const QString &modelText);

// Which wire format an address speaks: ".../systemone" or ".../decisions" is a
// decision model, anything else a chat endpoint.
enum class Protocol { Chat, Decisions };
Protocol protocolFor(const QString &base);

// Decisions protocol. The text is `state`; the questions are yes/no ("noul")
// questions with plain-string instructions, which every decision model accepts.
QJsonObject buildDecisionsBody(const QString &model, const QString &userText);
// Reads {"answers": {"intent_Chat": {"type":"noul","noul":0.02}, ...}}. The intent
// is the most probable intent_* answer and its probability is the confidence; the
// risk never drops below what the intent implies and is raised by confident
// writes/commands/screen answers. Never throws; check `valid`.
Classification parseDecisions(const QByteArray &body);

// The instruction that makes the model answer with the schema above.
QString systemPrompt();
// The request body: only model + messages (jev takes no sampling, tool or
// response_format parameters).
QJsonObject buildRequestBody(const QString &model, const QString &userText);
// base -> .../v1/chat/completions (not doubled when the base already has it);
// a ".../systemone" or ".../decisions" address is used as it is.
QString endpointFor(const QString &base);
// The assistant text of an OpenAI-style, Anthropic-style or bare-object reply.
QString replyText(const QByteArray &body);
// "HTTP 503 - message (code)" from an error body, or a plain transport message.
QString errorSummary(const QByteArray &body, int status, const QString &transportError);

} // namespace turnclass

// Calls the classifier endpoint. One request at a time; cancel() drops it.
class TurnClassifier : public QObject
{
    Q_OBJECT
public:
    explicit TurnClassifier(QObject *parent = nullptr);

    void setConfig(const QString &base, const QString &key, const QString &model);
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    QString base() const { return m_base; }
    QString key() const { return m_key; }
    QString model() const { return m_model; }
    QString endpoint() const;
    turnclass::Protocol protocol() const { return turnclass::protocolFor(m_base); }
    // Endpoint and model are set (a key is optional: some relays need none).
    bool configured() const;
    // Enabled, configured and not resting after repeated failures.
    bool ready() const;
    // Seconds of rest left after repeated failures (0 = not resting).
    int restSecondsLeft() const;

    // Classify one request. Emits classified() exactly once unless cancelled.
    void classify(const QString &text, quint64 token);
    // Send a fixed sample request and report what came back.
    void test();
    void cancel();

    // Tuning (tests shorten these).
    int timeoutMs = 6000;   // a decision model answers in 1-4 s; a slower one is not worth waiting for
    int testTimeoutMs = 20000;
    int restAfterFailures = 2;
    int restMs = 120000;

signals:
    // `error` is empty exactly when `result.valid`.
    void classified(quint64 token, const turnclass::Classification &result, int ms, const QString &error);
    void testFinished(bool ok, const QString &message);

private:
    QNetworkRequest buildRequest(int timeout) const;
    void noteSuccess();
    void noteFailure();

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_testReply;
    QString m_base;
    QString m_key;
    QString m_model;
    bool m_enabled = false;
    int m_failures = 0;
    QElapsedTimer m_rest;
    bool m_resting = false;
};

Q_DECLARE_METATYPE(turnclass::Classification)

