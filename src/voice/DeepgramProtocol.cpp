#include "DeepgramProtocol.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>

namespace deepgram {

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString lower(const QString &s) { return s.trimmed().toLower(); }

} // namespace

// ---- model rules ---------------------------------------------------------------------------

bool isFluxModel(const QString &listenModel)
{
    return lower(listenModel).startsWith(QLatin1String("flux"));
}

bool modelAcceptsTemperature(const QString &provider, const QString &model)
{
    const QString p = lower(provider), m = lower(model);
    if (m.isEmpty())
        return false;
    if (p == QLatin1String("anthropic")) {
        // Only the older families still take it; the 4.6+ and 5.x models answer
        // "`temperature` is deprecated for this model" and the turn fails.
        static const QRegularExpression older(QStringLiteral("^claude-(3|haiku-4-5|sonnet-4-5|sonnet-4-2|opus-4-[0-5]|opus-4-2|sonnet-4$)"));
        return older.match(m).hasMatch();
    }
    if (p == QLatin1String("open_ai")) {
        // The GPT-5 / 6 reasoning families take reasoning_mode instead.
        static const QRegularExpression reasoning(QStringLiteral("^(gpt-[5-9]|o[1-9])"));
        return !reasoning.match(m).hasMatch();
    }
    return true;
}

QStringList knownProviders()
{
    return { QStringLiteral("anthropic"), QStringLiteral("open_ai"), QStringLiteral("google"),
             QStringLiteral("groq") };
}

QStringList listenModels()
{
    return { QStringLiteral("nova-3"), QStringLiteral("nova-2"), QStringLiteral("flux-general-en"),
             QStringLiteral("flux-general-multi") };
}

QString defaultPrompt()
{
    return QStringLiteral(
        "You are Buddy, a friendly voice assistant that lives on the user's Windows desktop. "
        "You are speaking out loud, so answer in one to three short sentences of plain speech: "
        "no lists, no markdown, no code, no URLs read letter by letter. Be warm and direct, lead "
        "with the answer instead of a preamble, skip filler like \"Great question\", and never say "
        "you are an AI model unless asked. If the user cuts in, drop what you were saying and "
        "answer what they just said.\n\n"
        "You can do things on the user's computer through the ask_buddy function: opening or "
        "installing apps, playing music, controlling the mouse and keyboard, working with files, "
        "searching the web, setting reminders, anything beyond a quick chat. Whenever the user asks "
        "for an action or for facts you could not know offhand (the weather, today's news, what is "
        "on their screen), call ask_buddy with the request in plain words and every detail they "
        "gave. First say one very short sentence such as \"On it\" in that same turn, then call "
        "the function, then tell them the result "
        "in your own words. Do not pretend an action happened before ask_buddy has answered. "
        "If you did not hear a clear request, ask them to repeat it.\n\n"
        "Sometimes ask_buddy (or answer_buddy) comes back saying Buddy needs the user: a yes or no "
        "for something that needs permission, or a choice between options. Ask the user out loud, "
        "in one short natural sentence, reading out the options if there are any, and listen. Then "
        "call answer_buddy with what they said, in their own words (\"the second one\", \"yes, go "
        "ahead\", \"no\"). Never answer for them and never guess. If what they said does not answer "
        "it, ask again. After answer_buddy, tell them the result as you would after ask_buddy.");
}

QString effectivePrompt(const AgentConfig &c)
{
    QString base = c.prompt.trimmed().isEmpty() ? defaultPrompt() : c.prompt.trimmed();
    if (!c.quickReplies)
        return base.left(25000);
    QString note = QStringLiteral(
        "\n\nSpeed matters on a voice call: start answering at once and keep it to a sentence or two, "
        "never a monologue.");
    if (c.askBuddy)
        note += QStringLiteral(
            " Do not work things out at length yourself: for anything that needs facts you cannot know "
            "offhand, several steps, or real work, say a very short \"On it\" and hand it to ask_buddy.");
    return base.left(25000 - int(note.size())) + note;
}

QString configProblem(const AgentConfig &c)
{
    if (c.apiKey.trimmed().isEmpty())
        return QObject::tr("Add your Deepgram API key (console.deepgram.com).");
    if (!keyLooksValid(c.apiKey))
        return QObject::tr("That does not look like a Deepgram API key.");
    if (c.listenModel.trimmed().isEmpty())
        return QObject::tr("Choose a speech-to-text model.");
    if (c.thinkModel.trimmed().isEmpty())
        return QObject::tr("Choose a language model for the agent.");
    if (c.speakModel.trimmed().isEmpty())
        return QObject::tr("Choose a voice for the agent.");
    if (!c.endpointUrl.trimmed().isEmpty()) {
        const QUrl u(c.endpointUrl.trimmed());
        if (!u.isValid() || u.host().isEmpty() || u.scheme() != QLatin1String("https"))
            return QObject::tr("Your own model's address must be an https:// address that Deepgram can reach.");
    }
    return QString();
}

QUrl agentUrl(const AgentConfig &c)
{
    return QUrl(c.endpoint.isEmpty() ? QString::fromLatin1(kAgentUrl) : c.endpoint);
}

QString FunctionCall::request() const
{
    if (name != QLatin1String(kAskBuddyFunction))
        return QString();
    const QJsonObject args = QJsonDocument::fromJson(arguments.toUtf8()).object();
    return args.value(QStringLiteral("request")).toString().trimmed();
}

QString FunctionCall::answer() const
{
    if (name != QLatin1String(kAnswerBuddyFunction))
        return QString();
    const QJsonObject args = QJsonDocument::fromJson(arguments.toUtf8()).object();
    return args.value(QStringLiteral("answer")).toString().trimmed();
}

QJsonObject buildSettings(const AgentConfig &c, QString *error)
{
    const QString problem = configProblem(c);
    if (error)
        *error = problem;

    const bool flux = isFluxModel(c.listenModel);
    QJsonObject listenProvider{ { QStringLiteral("type"), QStringLiteral("deepgram") },
                                { QStringLiteral("model"), c.listenModel.trimmed() } };
    if (flux) {
        // Flux works out the language from the model name and decides when you
        // are done speaking itself; these three tune that.
        listenProvider[QStringLiteral("version")] = QStringLiteral("v2");
        listenProvider[QStringLiteral("eot_threshold")] = std::clamp(c.eotThreshold, 0.5, 1.0);
        if (c.eagerEotThreshold >= 0.3)
            listenProvider[QStringLiteral("eager_eot_threshold")] =
                std::min(std::clamp(c.eagerEotThreshold, 0.3, 0.9), std::clamp(c.eotThreshold, 0.5, 1.0));
        listenProvider[QStringLiteral("eot_timeout_ms")] = std::clamp(c.eotTimeoutMs, 500, 10000);
    } else {
        listenProvider[QStringLiteral("version")] = QStringLiteral("v1");
        if (!c.listenLanguage.trimmed().isEmpty())
            listenProvider[QStringLiteral("language")] = c.listenLanguage.trimmed();
        listenProvider[QStringLiteral("smart_format")] = true;
    }
    QJsonArray terms;
    for (const QString &t : c.keyterms) {
        const QString term = t.simplified();
        if (!term.isEmpty() && terms.size() < 100)
            terms.append(term);
    }
    if (!terms.isEmpty())
        listenProvider[QStringLiteral("keyterms")] = terms;

    QJsonObject thinkProvider{ { QStringLiteral("type"), c.thinkProvider.trimmed().isEmpty() ? QStringLiteral("anthropic")
                                                                                              : c.thinkProvider.trimmed() },
                               { QStringLiteral("model"), c.thinkModel.trimmed() } };
    if (c.temperature >= 0.0 && modelAcceptsTemperature(c.thinkProvider, c.thinkModel)) {
        const bool anthropic = lower(c.thinkProvider) == QLatin1String("anthropic");
        thinkProvider[QStringLiteral("temperature")] = std::clamp(c.temperature, 0.0, anthropic ? 1.0 : 2.0);
    }
    const QString prompt = effectivePrompt(c);
    QJsonObject think{ { QStringLiteral("provider"), thinkProvider }, { QStringLiteral("prompt"), prompt } };

    const QString endpoint = c.endpointUrl.trimmed();
    if (!endpoint.isEmpty() && problem.isEmpty()) {
        QJsonObject headers;
        const QString key = c.endpointKey.trimmed();
        if (lower(c.thinkProvider) == QLatin1String("anthropic")) {
            if (!key.isEmpty())
                headers[QStringLiteral("x-api-key")] = key;
            headers[QStringLiteral("anthropic-version")] = QStringLiteral("2023-06-01");
        } else if (!key.isEmpty()) {
            headers[QStringLiteral("authorization")] = QStringLiteral("Bearer ") + key;
        }
        think[QStringLiteral("endpoint")] = QJsonObject{ { QStringLiteral("url"), endpoint }, { QStringLiteral("headers"), headers } };
    }

    if (c.askBuddy) {
        think[QStringLiteral("functions")] = QJsonArray{ QJsonObject{
            { QStringLiteral("name"), QString::fromLatin1(kAskBuddyFunction) },
            { QStringLiteral("description"),
              QStringLiteral("Hand a task or question to Buddy's own agent on the user's computer (apps, music, files, "
                             "web search, mouse and keyboard, reminders, current facts). Returns what happened, in "
                             "a sentence or two you can say aloud.") },
            { QStringLiteral("parameters"), QJsonObject{
                  { QStringLiteral("type"), QStringLiteral("object") },
                  { QStringLiteral("properties"), QJsonObject{
                        { QStringLiteral("request"), QJsonObject{
                              { QStringLiteral("type"), QStringLiteral("string") },
                              { QStringLiteral("description"),
                                QStringLiteral("What the user wants, in plain words, with every detail they gave.") } } } } },
                  { QStringLiteral("required"), QJsonArray{ QStringLiteral("request") } } } } },
            QJsonObject{
            { QStringLiteral("name"), QString::fromLatin1(kAnswerBuddyFunction) },
            { QStringLiteral("description"),
              QStringLiteral("Give Buddy the user's answer to a question or permission request it passed on to you. Only call this "
                             "after asking the user out loud and hearing their answer. Returns what happened next.") },
            { QStringLiteral("parameters"), QJsonObject{
                  { QStringLiteral("type"), QStringLiteral("object") },
                  { QStringLiteral("properties"), QJsonObject{
                        { QStringLiteral("answer"), QJsonObject{
                              { QStringLiteral("type"), QStringLiteral("string") },
                              { QStringLiteral("description"),
                                QStringLiteral("What the user said, in their own words: the option they chose, or yes / no / always.") } } } } },
                  { QStringLiteral("required"), QJsonArray{ QStringLiteral("answer") } } } } } };
    }

    const bool fluxVoice = lower(c.speakModel).startsWith(QLatin1String("flux"));
    QJsonObject speakProvider{ { QStringLiteral("type"), QStringLiteral("deepgram") },
                               { QStringLiteral("model"), c.speakModel.trimmed() } };
    if (fluxVoice)
        speakProvider[QStringLiteral("version")] = QStringLiteral("v2");
    const double speed = std::clamp(c.speakSpeed, fluxVoice ? 0.5 : 0.7, 1.5);
    if (!qFuzzyCompare(speed, 1.0))
        speakProvider[QStringLiteral("speed")] = speed;

    QJsonObject agent{
        { QStringLiteral("listen"), QJsonObject{ { QStringLiteral("provider"), listenProvider } } },
        { QStringLiteral("think"), think },
        { QStringLiteral("speak"), QJsonObject{ { QStringLiteral("provider"), speakProvider } } },
    };
    // Empty lines carry nothing; the most recent turns are enough to carry on, and few
    // enough to stay cheap.
    QJsonArray messages;
    for (const auto &turn : c.history) {
        const QString content = turn.second.simplified().left(2000);
        if (!content.isEmpty())
            messages.append(QJsonObject{ { QStringLiteral("type"), QStringLiteral("History") },
                                         { QStringLiteral("role"), turn.first == QLatin1String("user") ? QStringLiteral("user")
                                                                                                    : QStringLiteral("assistant") },
                                         { QStringLiteral("content"), content } });
    }
    while (messages.size() > 24)
        messages.removeFirst();
    if (!messages.isEmpty()) {
        agent[QStringLiteral("context")] = QJsonObject{ { QStringLiteral("messages"), messages } };
    } else if (!c.greeting.trimmed().isEmpty()) {
        agent[QStringLiteral("greeting")] = c.greeting.trimmed().left(500);
    }

    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("Settings") },
        { QStringLiteral("audio"), QJsonObject{
              { QStringLiteral("input"), QJsonObject{ { QStringLiteral("encoding"), QStringLiteral("linear16") },
                                                     { QStringLiteral("sample_rate"), c.inputRate } } },
              { QStringLiteral("output"), QJsonObject{ { QStringLiteral("encoding"), QStringLiteral("linear16") },
                                                      { QStringLiteral("sample_rate"), c.outputRate },
                                                      { QStringLiteral("container"), QStringLiteral("none") } } } } },
        { QStringLiteral("agent"), agent },
    };
}

// ---- server messages ---------------------------------------------------------------------------

Event parseEvent(const QString &json)
{
    Event e;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return e;
    const QJsonObject o = doc.object();
    const QString type = o.value(QStringLiteral("type")).toString();
    e.rawType = type;
    static const QHash<QString, Event::Type> types = {
        { QStringLiteral("Welcome"), Event::Welcome },
        { QStringLiteral("SettingsApplied"), Event::SettingsApplied },
        { QStringLiteral("UserStartedSpeaking"), Event::UserStartedSpeaking },
        { QStringLiteral("ConversationText"), Event::ConversationText },
        { QStringLiteral("AgentThinking"), Event::AgentThinking },
        { QStringLiteral("AgentStartedSpeaking"), Event::AgentStartedSpeaking },
        { QStringLiteral("AgentAudioDone"), Event::AgentAudioDone },
        { QStringLiteral("FunctionCallRequest"), Event::FunctionCallRequest },
        { QStringLiteral("FunctionCallCancelled"), Event::FunctionCallCancelled },
        { QStringLiteral("InjectionRefused"), Event::InjectionRefused },
        { QStringLiteral("PromptUpdated"), Event::Updated },
        { QStringLiteral("SpeakUpdated"), Event::Updated },
        { QStringLiteral("ThinkUpdated"), Event::Updated },
        { QStringLiteral("ListenUpdated"), Event::Updated },
        { QStringLiteral("LatencyReport"), Event::LatencyReport },
        { QStringLiteral("History"), Event::History },
        { QStringLiteral("EndOfTurn"), Event::EndOfTurn },
        { QStringLiteral("Error"), Event::Error },
        { QStringLiteral("Warning"), Event::Warning },
    };
    e.type = types.value(type, Event::Unknown);
    switch (e.type) {
    case Event::Welcome:
        e.requestId = o.value(QStringLiteral("request_id")).toString();
        break;
    case Event::ConversationText:
        e.role = o.value(QStringLiteral("role")).toString();
        e.text = o.value(QStringLiteral("content")).toString();
        break;
    case Event::AgentThinking:
        e.text = o.value(QStringLiteral("content")).toString();
        break;
    case Event::Error:
    case Event::Warning:
        e.code = o.value(QStringLiteral("code")).toString();
        e.description = o.value(QStringLiteral("description")).toString();
        break;
    case Event::LatencyReport:
        if (o.contains(QStringLiteral("total_latency")))
            e.totalLatency = o.value(QStringLiteral("total_latency")).toDouble(-1);
        break;
    case Event::FunctionCallRequest:
        for (const QJsonValue &v : o.value(QStringLiteral("functions")).toArray()) {
            const QJsonObject f = v.toObject();
            FunctionCall call;
            call.id = f.value(QStringLiteral("id")).toString();
            call.name = f.value(QStringLiteral("name")).toString();
            call.arguments = f.value(QStringLiteral("arguments")).toString();
            call.clientSide = f.value(QStringLiteral("client_side")).toBool(true);
            if (!call.id.isEmpty())
                e.calls.append(call);
        }
        break;
    case Event::FunctionCallCancelled:
        for (const char *key : { "ids", "function_call_ids" })
            for (const QJsonValue &v : o.value(QLatin1String(key)).toArray())
                e.cancelledIds.append(v.toString());
        for (const QJsonValue &v : o.value(QStringLiteral("functions")).toArray())
            e.cancelledIds.append(v.toObject().value(QStringLiteral("id")).toString());
        if (o.contains(QStringLiteral("id")))
            e.cancelledIds.append(o.value(QStringLiteral("id")).toString());
        break;
    default:
        break;
    }
    return e;
}

QString friendlyProblem(const QString &code, const QString &description)
{
    static const QHash<QString, QString> map = {
        { QStringLiteral("FAILED_TO_THINK"),
          QObject::tr("The language model did not answer. Check the model name and, if you use your own endpoint, its address and key.") },
        { QStringLiteral("THINK_REQUEST_FAILED"),
          QObject::tr("The language model refused a request.") },
        { QStringLiteral("FAILED_TO_SPEAK"), QObject::tr("The voice could not speak. Try another voice.") },
        { QStringLiteral("SPEAK_REQUEST_FAILED"), QObject::tr("The voice service refused a request.") },
        { QStringLiteral("INVALID_SETTINGS"), QObject::tr("Deepgram did not accept these settings.") },
        { QStringLiteral("FAILED_TO_START_LISTENING"), QObject::tr("The speech recognizer could not start.") },
        { QStringLiteral("ASR_CONNECTION_CLOSED"), QObject::tr("The speech recognizer disconnected.") },
        { QStringLiteral("ASR_DRIVER_TIMEOUT"), QObject::tr("Deepgram heard no usable audio. Check your microphone.") },
        { QStringLiteral("CLIENT_MESSAGE_TIMEOUT"), QObject::tr("The connection went quiet and was closed.") },
        { QStringLiteral("MAXIMUM_SESSION_LENGTH_REACHED"), QObject::tr("The conversation reached Deepgram's two hour limit.") },
        { QStringLiteral("MAXIMUM_SESSION_LENGTH_APPROACHING"), QObject::tr("This conversation will end in five minutes (Deepgram's two hour limit).") },
        { QStringLiteral("SERVER_GOING_AWAY"), QObject::tr("Deepgram is restarting. Start the conversation again.") },
        { QStringLiteral("SLOW_THINK_REQUEST"), QObject::tr("The language model is slow right now.") },
        { QStringLiteral("SLOW_SPEAK_REQUEST"), QObject::tr("The voice is slow right now.") },
    };
    QString base = map.value(code);
    if (base.isEmpty())
        return QString();
    if (code == QLatin1String("INVALID_SETTINGS") || code == QLatin1String("THINK_REQUEST_FAILED")
        || code == QLatin1String("FAILED_TO_THINK")) {
        const QString detail = description.simplified().left(220);
        if (!detail.isEmpty())
            base += QLatin1Char(' ') + detail;
    }
    return base;
}

bool isQuietWarning(const QString &code)
{
    return code.startsWith(QLatin1String("INJECT_AGENT_MESSAGE_DURING")) || code == QLatin1String("GPT_4O_STREAM_END_ERROR")
        || code == QLatin1String("FORCE_END_TURN_UNSUPPORTED");
}

// ---- client messages -------------------------------------------------------------------------

QString keepAliveMessage()
{
    return compact({ { QStringLiteral("type"), QStringLiteral("KeepAlive") } });
}

QString injectAgentMessage(const QString &text, const QString &behavior)
{
    return compact({ { QStringLiteral("type"), QStringLiteral("InjectAgentMessage") },
                     { QStringLiteral("behavior"), behavior },
                     { QStringLiteral("message"), text } });
}

QString injectUserMessage(const QString &text)
{
    return compact({ { QStringLiteral("type"), QStringLiteral("InjectUserMessage") }, { QStringLiteral("content"), text } });
}

QString updatePromptMessage(const QString &prompt)
{
    return compact({ { QStringLiteral("type"), QStringLiteral("UpdatePrompt") },
                     { QStringLiteral("prompt"), prompt.left(25000) } });
}

QString updateSpeakMessage(const QString &model, double speed)
{
    const bool flux = lower(model).startsWith(QLatin1String("flux"));
    QJsonObject provider{ { QStringLiteral("type"), QStringLiteral("deepgram") }, { QStringLiteral("model"), model } };
    if (flux)
        provider[QStringLiteral("version")] = QStringLiteral("v2");
    const double s = std::clamp(speed, flux ? 0.5 : 0.7, 1.5);
    if (!qFuzzyCompare(s, 1.0))
        provider[QStringLiteral("speed")] = s;
    return compact({ { QStringLiteral("type"), QStringLiteral("UpdateSpeak") },
                     { QStringLiteral("speak"), QJsonObject{ { QStringLiteral("provider"), provider } } } });
}

QString functionResponseMessage(const QString &id, const QString &name, const QString &content)
{
    return compact({ { QStringLiteral("type"), QStringLiteral("FunctionCallResponse") },
                     { QStringLiteral("id"), id },
                     { QStringLiteral("name"), name },
                     { QStringLiteral("content"), content } });
}

// ---- REST -----------------------------------------------------------------------------------

QUrl listenUrl(const QString &model, const QString &language, bool smartFormat, const QStringList &keyterms,
               const QString &base)
{
    QUrl url(base + QStringLiteral("/v1/listen"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("model"), model.trimmed());
    if (!language.trimmed().isEmpty())
        q.addQueryItem(QStringLiteral("language"), language.trimmed());
    q.addQueryItem(QStringLiteral("smart_format"), smartFormat ? QStringLiteral("true") : QStringLiteral("false"));
    // nova-3 takes "keyterm" (repeated); older models take "keywords" instead, which is not worth supporting.
    if (lower(model).startsWith(QLatin1String("nova-3"))) {
        int n = 0;
        for (const QString &t : keyterms) {
            const QString term = t.simplified();
            if (!term.isEmpty() && n++ < 100)
                q.addQueryItem(QStringLiteral("keyterm"), term);
        }
    }
    url.setQuery(q);
    return url;
}

QUrl speakUrl(const QString &model, int rate, double speed, const QString &base, const QString &container)
{
    QUrl url(base + QStringLiteral("/v1/speak"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("model"), model.trimmed());
    q.addQueryItem(QStringLiteral("encoding"), QStringLiteral("linear16"));
    q.addQueryItem(QStringLiteral("sample_rate"), QString::number(rate));
    q.addQueryItem(QStringLiteral("container"), container == QLatin1String("none") ? container : QStringLiteral("wav"));
    const double s = std::clamp(speed, 0.7, 1.5);
    if (!qFuzzyCompare(s, 1.0))
        q.addQueryItem(QStringLiteral("speed"), QString::number(s, 'f', 2));
    url.setQuery(q);
    return url;
}

QByteArray authorizationHeader(const QString &apiKey)
{
    return "Token " + apiKey.trimmed().toLatin1();
}

QString transcriptFromListen(const QByteArray &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonArray channels = root.value(QStringLiteral("results")).toObject().value(QStringLiteral("channels")).toArray();
    if (channels.isEmpty())
        return QString();
    const QJsonArray alts = channels.at(0).toObject().value(QStringLiteral("alternatives")).toArray();
    if (alts.isEmpty())
        return QString();
    return alts.at(0).toObject().value(QStringLiteral("transcript")).toString().trimmed();
}

QString restErrorMessage(const QByteArray &body)
{
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    for (const char *key : { "err_msg", "message", "error", "reason" }) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isString() && !v.toString().isEmpty())
            return v.toString().left(300);
        if (v.isObject()) {
            const QString m = v.toObject().value(QStringLiteral("message")).toString();
            if (!m.isEmpty())
                return m.left(300);
        }
    }
    return QString();
}

bool keyLooksValid(const QString &key)
{
    const QString k = key.trimmed();
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9_.\\-]{20,200}$"));
    return ok.match(k).hasMatch();
}

QString voiceLabel(const QString &modelId)
{
    // aura-2-thalia-en -> "Thalia (en)"; anything else is shown as it is.
    static const QRegularExpression aura(QStringLiteral("^aura(?:-2)?-([a-z]+)-([a-z]{2,3})$"));
    const auto m = aura.match(modelId.trimmed());
    if (!m.hasMatch())
        return modelId;
    QString name = m.captured(1);
    name[0] = name[0].toUpper();
    return QStringLiteral("%1 (%2)").arg(name, m.captured(2));
}

QVariantList builtinVoices()
{
    static const char *const names[] = {
        "thalia", "andromeda", "asteria", "athena", "aurora", "callista", "cora", "cordelia", "delia", "electra",
        "harmonia", "helena", "hera", "iris", "juno", "luna", "minerva", "ophelia", "pandora", "phoebe",
        "selene", "theia", "vesta", "amalthea", "apollo", "arcas", "aries", "atlas", "draco", "hermes",
        "hyperion", "janus", "jupiter", "mars", "neptune", "odysseus", "orion", "orpheus", "pluto", "saturn", "zeus" };
    QVariantList out;
    for (const char *n : names) {
        const QString id = QStringLiteral("aura-2-%1-en").arg(QLatin1String(n));
        out.append(QVariantMap{ { QStringLiteral("id"), id },
                                { QStringLiteral("label"), voiceLabel(id) },
                                { QStringLiteral("language"), QStringLiteral("en") } });
    }
    return out;
}

QVariantList voicesFromModels(const QByteArray &json)
{
    const QJsonArray tts = QJsonDocument::fromJson(json).object().value(QStringLiteral("tts")).toArray();
    struct Item { QString language, id; QVariantMap map; };
    QList<Item> items;
    for (const QJsonValue &v : tts) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("canonical_name")).toString();
        if (!id.startsWith(QLatin1String("aura-2-")))
            continue;
        const QString language = o.value(QStringLiteral("languages")).toArray().first().toString();
        const QJsonObject meta = o.value(QStringLiteral("metadata")).toObject();
        QStringList tags;
        for (const QJsonValue &t : meta.value(QStringLiteral("tags")).toArray())
            tags << t.toString();
        items.append({ language, id, QVariantMap{ { QStringLiteral("id"), id },
                                                  { QStringLiteral("label"), voiceLabel(id) },
                                                  { QStringLiteral("language"), language },
                                                  { QStringLiteral("accent"), meta.value(QStringLiteral("accent")).toString() },
                                                  { QStringLiteral("tags"), tags.join(QStringLiteral(", ")) } } });
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        // English first, then by language and name
        const bool ae = a.language == QLatin1String("en"), be = b.language == QLatin1String("en");
        if (ae != be)
            return ae;
        return a.language != b.language ? a.language < b.language : a.id < b.id;
    });
    QVariantList out;
    for (const Item &i : items)
        out.append(i.map);
    return out;
}

QVariantList thinkModelsFromJson(const QByteArray &json)
{
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(json).object().value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        out.append(QVariantMap{ { QStringLiteral("id"), id },
                                { QStringLiteral("name"), o.value(QStringLiteral("name")).toString(id) },
                                { QStringLiteral("provider"), o.value(QStringLiteral("provider")).toString() } });
    }
    return out;
}

} // namespace deepgram
