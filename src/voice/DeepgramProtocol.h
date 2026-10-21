#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Everything Noch knows about Deepgram's wire formats, as pure functions on
// values so it can be unit tested without a network:
//
//  * AgentConfig + buildSettings(): the Voice Agent "Settings" message
//    (listen = speech to text, think = the LLM, speak = text to speech).
//  * parseEvent(): a server message (JSON text) to a typed Event.
//  * the small client messages (KeepAlive, InjectAgentMessage, ...).
//  * the REST addresses for plain speech-to-text and text-to-speech, used when
//    Buddy's own model does the thinking and Deepgram only hears and speaks.
//
// Reference: https://developers.deepgram.com/docs/voice-agent
namespace deepgram {

constexpr int kInputRate = 16000;      // what the microphone capture delivers (16-bit mono)
constexpr int kOutputRate = 24000;     // what the agent speaks at (16-bit mono)
constexpr int kKeepAliveMs = 5000;     // the server drops a silent socket; audio frames count as traffic
constexpr const char *kAgentUrl = "wss://agent.deepgram.com/v1/agent/converse";
constexpr const char *kApiBase = "https://api.deepgram.com";
constexpr const char *kThinkModelsUrl = "https://agent.deepgram.com/v1/agent/settings/think/models";

// The name of the function that hands a spoken task to Buddy's own agent.
constexpr const char *kAskBuddyFunction = "ask_buddy";
// ...and the one that carries the user's spoken answer to a question or approval Buddy asked.
constexpr const char *kAnswerBuddyFunction = "answer_buddy";

struct AgentConfig
{
    QString apiKey;
    QString endpoint = QString::fromLatin1(kAgentUrl);

    // listen: speech to text
    QString listenModel = QStringLiteral("nova-3");
    QString listenLanguage = QStringLiteral("en");
    QStringList keyterms;               // words to listen for (names, jargon)
    double eotThreshold = 0.7;          // Flux only: how sure it must be that you finished (0.5 .. 1.0)
    double eagerEotThreshold = 0.0;     // Flux only: start thinking early; 0 = off
    int eotTimeoutMs = 5000;            // Flux only: the longest silence before a turn ends

    // think: the LLM. Managed by Deepgram unless endpointUrl is set.
    QString thinkProvider = QStringLiteral("anthropic");
    QString thinkModel = QStringLiteral("claude-haiku-5-5");
    double temperature = -1.0;          // < 0 = the model's default (several models reject the field)
    QString prompt;                     // "" = defaultPrompt()
    QString endpointUrl;                // your own LLM endpoint (https)
    QString endpointKey;                // its API key
    bool askBuddy = true;               // let the agent hand tasks to Buddy's own tools

    // speak: text to speech
    QString speakModel = QStringLiteral("aura-2-thalia-en");
    double speakSpeed = 1.0;            // 0.7 .. 1.5

    QString greeting;                   // spoken when the session starts; "" = nothing
    // What was said so far, oldest first ({role, content}); a session that is
    // picked up again after a dropped connection continues from it.
    QList<QPair<QString, QString>> history;

    int inputRate = kInputRate;
    int outputRate = kOutputRate;
};

// ---- model rules --------------------------------------------------------------------------
bool isFluxModel(const QString &listenModel);
// Newer models refuse the `temperature` field outright ("deprecated for this
// model"), so it is only sent for models known to take it.
bool modelAcceptsTemperature(const QString &provider, const QString &model);
QStringList knownProviders();
QStringList listenModels();            // the choices Settings offers
QString defaultPrompt();

// Why this configuration cannot be used ("" if it can).
QString configProblem(const AgentConfig &c);

// The "Settings" message. `error`, when given, gets configProblem().
QJsonObject buildSettings(const AgentConfig &c, QString *error = nullptr);
// The WebSocket address for a configuration (honours a test override in c.endpoint).
QUrl agentUrl(const AgentConfig &c);

// ---- messages from the server ---------------------------------------------------------------
struct FunctionCall
{
    QString id;
    QString name;
    QString arguments;                  // JSON text
    bool clientSide = true;
    // The "request" argument of ask_buddy ("" for anything else).
    QString request() const;
    // The "answer" argument of answer_buddy ("" for anything else).
    QString answer() const;
};

struct Event
{
    enum Type {
        Unknown,
        Welcome,
        SettingsApplied,
        UserStartedSpeaking,
        ConversationText,
        AgentThinking,
        AgentStartedSpeaking,
        AgentAudioDone,
        FunctionCallRequest,
        FunctionCallCancelled,
        InjectionRefused,
        Updated,                // PromptUpdated, SpeakUpdated, ThinkUpdated, ListenUpdated
        LatencyReport,
        History,
        EndOfTurn,
        Error,
        Warning,
    };
    Type type = Unknown;
    QString role;                       // ConversationText: "user" | "assistant"
    QString text;                       // ConversationText content; AgentThinking text
    QString code;                       // Error / Warning
    QString description;                // Error / Warning
    QString requestId;                  // Welcome
    QList<FunctionCall> calls;          // FunctionCallRequest
    QStringList cancelledIds;           // FunctionCallCancelled
    double totalLatency = -1;           // LatencyReport (seconds), when it carries the total
    QString rawType;
};

Event parseEvent(const QString &json);
// A sentence for the user for a server error/warning code ("" when the raw
// description is the better message).
QString friendlyProblem(const QString &code, const QString &description);
// True for warnings that are routine (the agent was spoken over, and so on) and
// not worth showing the user.
bool isQuietWarning(const QString &code);

// ---- messages to the server -----------------------------------------------------------------
QString keepAliveMessage();
QString injectAgentMessage(const QString &text, const QString &behavior = QStringLiteral("default"));
QString injectUserMessage(const QString &text);
QString updatePromptMessage(const QString &prompt);
QString updateSpeakMessage(const QString &model, double speed);
QString functionResponseMessage(const QString &id, const QString &name, const QString &content);

// ---- REST (Buddy's own model, Deepgram for hearing and speaking) ----------------------------
// POST audio/wav here for plain speech to text.
QUrl listenUrl(const QString &model, const QString &language, bool smartFormat, const QStringList &keyterms,
               const QString &base = QString::fromLatin1(kApiBase));
// POST {"text": "..."} here for linear16 speech at `rate` Hz: inside a WAV file
// (container "wav") or as raw samples that can be played while they arrive ("none").
QUrl speakUrl(const QString &model, int rate, double speed, const QString &base = QString::fromLatin1(kApiBase),
              const QString &container = QStringLiteral("wav"));
// "Authorization: Token <key>"
QByteArray authorizationHeader(const QString &apiKey);
// The words out of a /v1/listen response ("" if none).
QString transcriptFromListen(const QByteArray &json);
// A readable reason out of a Deepgram REST error body ("" if there is none).
QString restErrorMessage(const QByteArray &body);
// Whether a key looks usable at all (non-empty, no spaces, sensible length).
bool keyLooksValid(const QString &key);
// The English Aura-2 voices, so the voice picker works before (or without) the
// full list being fetched. {id, label, language}.
QVariantList builtinVoices();
// Reads Deepgram's /v1/models response into the same shape (Aura-2 voices only,
// sorted by language then name). Empty if the body is not understood.
QVariantList voicesFromModels(const QByteArray &json);
// Reads the agent's think-models response into {id, name, provider} maps.
QVariantList thinkModelsFromJson(const QByteArray &json);
// A text-to-speech voice's display name from its id: "aura-2-thalia-en" -> "Thalia (en)".
QString voiceLabel(const QString &modelId);

} // namespace deepgram
