// Deepgram Voice Agent: the wire protocol (pure), the streaming player, and the
// live session against a scripted local server standing in for Deepgram.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

#include "DeepgramAgent.h"
#include "DeepgramProtocol.h"
#include "SpeechFakes.h"
#include "StreamingPlayer.h"
#include "WsTestServer.h"

namespace {

const QString kKey = QStringLiteral("test_key_0123456789abcdef0123456789abcd");

QJsonObject jsonOf(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }

QString typeOf(const QString &text) { return jsonOf(text).value(QStringLiteral("type")).toString(); }

QString eventJson(const QString &type, const QJsonObject &extra = {})
{
    QJsonObject o = extra;
    o[QStringLiteral("type")] = type;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString convo(const QString &role, const QString &text)
{
    return eventJson(QStringLiteral("ConversationText"), { { QStringLiteral("role"), role }, { QStringLiteral("content"), text } });
}

bool allZero(const QByteArray &b)
{
    for (char c : b)
        if (c != 0)
            return false;
    return true;
}

} // namespace

// A scripted Deepgram: says Welcome on connect, applies the first Settings, and
// records everything the app sends.
struct Rig
{
    WsTestServer server;
    DeepgramAgent agent;
    FakeCapture *mic = nullptr;
    QJsonObject settings;                       // the most recent Settings
    QList<QJsonObject> allSettings;
    bool autoApply = true;

    explicit Rig(bool enabled = true)
    {
        agent.setEnabled(enabled);
        agent.setApiKey(kKey);
        agent.setEndpoint(server.url());
        agent.player()->setDeviceDisabled(true);
        auto cap = std::make_unique<FakeCapture>();
        mic = cap.get();
        agent.setCapture(std::move(cap));
        QObject::connect(&server, &WsTestServer::upgraded, &server, [this] { server.sendText(eventJson(QStringLiteral("Welcome"), { { QStringLiteral("request_id"), QStringLiteral("r1") } })); });
        QObject::connect(&server, &WsTestServer::textReceived, &server, [this](const QString &t) {
            if (typeOf(t) == QLatin1String("Settings")) {
                settings = jsonOf(t);
                allSettings << settings;
                if (autoApply)
                    server.sendText(eventJson(QStringLiteral("SettingsApplied")));
            }
        });
    }

    // Texts the app sent of one type.
    QList<QJsonObject> sent(const QString &type) const
    {
        QList<QJsonObject> out;
        for (const QString &t : server.texts)
            if (typeOf(t) == type)
                out << jsonOf(t);
        return out;
    }

    bool startAndWaitListening()
    {
        if (!agent.start())
            return false;
        return QTest::qWaitFor([this] { return agent.state() == QLatin1String("listening"); }, 5000);
    }

    // One 40 ms microphone frame of speech.
    void speak() { mic->feed(tone(0.04)); }
};

class tst_Deepgram : public QObject
{
    Q_OBJECT

private slots:
    // ---- protocol -------------------------------------------------------------------------------

    void defaultSettingsAreHaikuOnNova()
    {
        deepgram::AgentConfig c;
        c.apiKey = kKey;
        QString why;
        const QJsonObject s = deepgram::buildSettings(c, &why);
        QVERIFY2(why.isEmpty(), qPrintable(why));
        QCOMPARE(s["type"].toString(), QStringLiteral("Settings"));
        const QJsonObject in = s["audio"].toObject()["input"].toObject();
        QCOMPARE(in["encoding"].toString(), QStringLiteral("linear16"));
        QCOMPARE(in["sample_rate"].toInt(), 16000);
        const QJsonObject out = s["audio"].toObject()["output"].toObject();
        QCOMPARE(out["sample_rate"].toInt(), 24000);
        QCOMPARE(out["container"].toString(), QStringLiteral("none"));
        const QJsonObject agent = s["agent"].toObject();
        const QJsonObject listen = agent["listen"].toObject()["provider"].toObject();
        QCOMPARE(listen["model"].toString(), QStringLiteral("nova-3"));
        QCOMPARE(listen["version"].toString(), QStringLiteral("v1"));
        QCOMPARE(listen["language"].toString(), QStringLiteral("en"));
        QVERIFY(!listen.contains("eot_threshold"));
        const QJsonObject think = agent["think"].toObject();
        QCOMPARE(think["provider"].toObject()["type"].toString(), QStringLiteral("anthropic"));
        QCOMPARE(think["provider"].toObject()["model"].toString(), QStringLiteral("claude-haiku-5-5"));
        // Haiku 5.5 rejects `temperature` outright, so it must never be sent by default.
        QVERIFY(!think["provider"].toObject().contains("temperature"));
        QVERIFY(think["prompt"].toString().contains(QStringLiteral("ask_buddy")));
        QCOMPARE(think["functions"].toArray().size(), 2);
        QCOMPARE(think["functions"].toArray().at(0).toObject()["name"].toString(), QStringLiteral("ask_buddy"));
        QCOMPARE(think["functions"].toArray().at(1).toObject()["name"].toString(), QStringLiteral("answer_buddy"));
        QVERIFY(think["prompt"].toString().contains(QStringLiteral("answer_buddy")));
        QVERIFY(!think.contains("endpoint"));
        const QJsonObject speak = agent["speak"].toObject()["provider"].toObject();
        QCOMPARE(speak["type"].toString(), QStringLiteral("deepgram"));
        QCOMPARE(speak["model"].toString(), QStringLiteral("aura-2-thalia-en"));
        QVERIFY(!speak.contains("speed"));
        QVERIFY(!agent.contains("greeting"));
    }

    void temperatureOnlyForModelsThatTakeIt()
    {
        QVERIFY(!deepgram::modelAcceptsTemperature("anthropic", "claude-haiku-5-5"));
        QVERIFY(!deepgram::modelAcceptsTemperature("anthropic", "claude-sonnet-5"));
        QVERIFY(!deepgram::modelAcceptsTemperature("anthropic", "claude-sonnet-5-5"));
        QVERIFY(!deepgram::modelAcceptsTemperature("anthropic", "claude-sonnet-4-6"));
        QVERIFY(deepgram::modelAcceptsTemperature("anthropic", "claude-haiku-4-5"));
        QVERIFY(deepgram::modelAcceptsTemperature("anthropic", "claude-sonnet-4-5"));
        QVERIFY(deepgram::modelAcceptsTemperature("anthropic", "claude-3-5-haiku-latest"));
        QVERIFY(deepgram::modelAcceptsTemperature("open_ai", "gpt-4o-mini"));
        QVERIFY(!deepgram::modelAcceptsTemperature("open_ai", "gpt-5-mini"));
        QVERIFY(!deepgram::modelAcceptsTemperature("anthropic", ""));

        deepgram::AgentConfig c;
        c.apiKey = kKey;
        c.temperature = 0.5;
        QVERIFY(!deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["provider"].toObject().contains("temperature"));
        c.thinkModel = QStringLiteral("claude-haiku-4-5");
        QCOMPARE(deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["provider"].toObject()["temperature"].toDouble(), 0.5);
        c.temperature = 7.0;                         // Anthropic's range is 0..1
        QCOMPARE(deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["provider"].toObject()["temperature"].toDouble(), 1.0);
        c.temperature = -1.0;                        // "model default"
        QVERIFY(!deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["provider"].toObject().contains("temperature"));
    }

    void fluxChangesTheListenBlock()
    {
        deepgram::AgentConfig c;
        c.apiKey = kKey;
        c.listenModel = QStringLiteral("flux-general-en");
        c.eotThreshold = 0.85;
        c.eagerEotThreshold = 0.6;
        c.eotTimeoutMs = 4000;
        const QJsonObject l = deepgram::buildSettings(c)["agent"].toObject()["listen"].toObject()["provider"].toObject();
        QCOMPARE(l["version"].toString(), QStringLiteral("v2"));
        QCOMPARE(l["eot_threshold"].toDouble(), 0.85);
        QCOMPARE(l["eager_eot_threshold"].toDouble(), 0.6);
        QCOMPARE(l["eot_timeout_ms"].toInt(), 4000);
        QVERIFY(!l.contains("language"));            // Flux works the language out from the model
        QVERIFY(!l.contains("smart_format"));        // and refuses smart_format
        // The eager threshold can never be above the final one.
        c.eagerEotThreshold = 0.9;
        c.eotThreshold = 0.7;
        QCOMPARE(deepgram::buildSettings(c)["agent"].toObject()["listen"].toObject()["provider"].toObject()["eager_eot_threshold"].toDouble(), 0.7);
        c.eagerEotThreshold = 0.0;
        QVERIFY(!deepgram::buildSettings(c)["agent"].toObject()["listen"].toObject()["provider"].toObject().contains("eager_eot_threshold"));
        QVERIFY(deepgram::isFluxModel("FLUX-general-multi"));
        QVERIFY(!deepgram::isFluxModel("nova-3"));
    }

    void keytermsGreetingPromptAndSpeed()
    {
        deepgram::AgentConfig c;
        c.apiKey = kKey;
        c.keyterms = { QStringLiteral("  Noch "), QString(), QStringLiteral("Moonshine") };
        c.greeting = QStringLiteral("  Hi, I'm Buddy.  ");
        c.prompt = QStringLiteral("Speak like a pirate.");
        c.speakSpeed = 1.3;
        c.speakModel = QStringLiteral("aura-2-zeus-en");
        const QJsonObject a = deepgram::buildSettings(c)["agent"].toObject();
        QCOMPARE(a["listen"].toObject()["provider"].toObject()["keyterms"].toArray().size(), 2);
        QCOMPARE(a["listen"].toObject()["provider"].toObject()["keyterms"].toArray().first().toString(), QStringLiteral("Noch"));
        QCOMPARE(a["greeting"].toString(), QStringLiteral("Hi, I'm Buddy."));
        QCOMPARE(a["think"].toObject()["prompt"].toString(), QStringLiteral("Speak like a pirate."));
        QCOMPARE(a["speak"].toObject()["provider"].toObject()["speed"].toDouble(), 1.3);
        c.speakSpeed = 9.0;
        QCOMPARE(deepgram::buildSettings(c)["agent"].toObject()["speak"].toObject()["provider"].toObject()["speed"].toDouble(), 1.5);
        c.prompt = QString(30000, 'x');
        QCOMPARE(deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["prompt"].toString().size(), 25000);
        c.askBuddy = false;
        QVERIFY(!deepgram::buildSettings(c)["agent"].toObject()["think"].toObject().contains("functions"));
    }

    void ownModelEndpoint()
    {
        deepgram::AgentConfig c;
        c.apiKey = kKey;
        c.endpointUrl = QStringLiteral("https://api.anthropic.com/v1/messages");
        c.endpointKey = QStringLiteral("sk-ant-secret");
        QJsonObject e = deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["endpoint"].toObject();
        QCOMPARE(e["url"].toString(), QStringLiteral("https://api.anthropic.com/v1/messages"));
        QCOMPARE(e["headers"].toObject()["x-api-key"].toString(), QStringLiteral("sk-ant-secret"));
        QCOMPARE(e["headers"].toObject()["anthropic-version"].toString(), QStringLiteral("2023-06-01"));
        c.thinkProvider = QStringLiteral("open_ai");
        c.endpointUrl = QStringLiteral("https://llm.example.com/v1/chat/completions");
        e = deepgram::buildSettings(c)["agent"].toObject()["think"].toObject()["endpoint"].toObject();
        QCOMPARE(e["headers"].toObject()["authorization"].toString(), QStringLiteral("Bearer sk-ant-secret"));
        // Deepgram's servers cannot reach this computer, and plain http would put the key on the wire.
        c.endpointUrl = QStringLiteral("http://127.0.0.1:11434/v1");
        QVERIFY(!deepgram::configProblem(c).isEmpty());
        QString why;
        const QJsonObject s = deepgram::buildSettings(c, &why);
        QVERIFY(!why.isEmpty());
        QVERIFY(!s["agent"].toObject()["think"].toObject().contains("endpoint"));
    }

    void historyReplacesTheGreeting()
    {
        deepgram::AgentConfig c;
        c.apiKey = kKey;
        c.greeting = QStringLiteral("Hello!");
        for (int i = 0; i < 30; ++i)
            c.history.append({ i % 2 ? QStringLiteral("assistant") : QStringLiteral("user"), QStringLiteral("line %1").arg(i) });
        c.history.append({ QStringLiteral("user"), QString() });          // empty lines are skipped
        const QJsonObject a = deepgram::buildSettings(c)["agent"].toObject();
        QVERIFY(!a.contains("greeting"));
        const QJsonArray m = a["context"].toObject()["messages"].toArray();
        QCOMPARE(m.size(), 24);                                           // only the latest turns
        QCOMPARE(m.last().toObject()["content"].toString(), QStringLiteral("line 29"));
        QCOMPARE(m.first().toObject()["type"].toString(), QStringLiteral("History"));
        QCOMPARE(m.last().toObject()["role"].toString(), QStringLiteral("assistant"));
    }

    void configProblems()
    {
        deepgram::AgentConfig c;
        QVERIFY(deepgram::configProblem(c).contains(QStringLiteral("API key")));
        c.apiKey = QStringLiteral("short");
        QVERIFY(!deepgram::configProblem(c).isEmpty());
        c.apiKey = kKey;
        QVERIFY(deepgram::configProblem(c).isEmpty());
        c.thinkModel.clear();
        QVERIFY(!deepgram::configProblem(c).isEmpty());
        QVERIFY(deepgram::keyLooksValid(kKey));
        QVERIFY(!deepgram::keyLooksValid(QStringLiteral("has space in it 0123456789012345")));
        QVERIFY(!deepgram::keyLooksValid(QString()));
    }

    void parsesServerEvents()
    {
        using E = deepgram::Event;
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"Welcome\",\"request_id\":\"abc\"}")).type, E::Welcome);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"Welcome\",\"request_id\":\"abc\"}")).requestId, QStringLiteral("abc"));
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"SettingsApplied\"}")).type, E::SettingsApplied);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"UserStartedSpeaking\"}")).type, E::UserStartedSpeaking);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"AgentAudioDone\"}")).type, E::AgentAudioDone);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"EndOfTurn\",\"trigger\":\"manual\"}")).type, E::EndOfTurn);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"PromptUpdated\"}")).type, E::Updated);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"History\",\"role\":\"user\",\"content\":\"x\"}")).type, E::History);
        const E t = deepgram::parseEvent(QStringLiteral("{\"type\":\"ConversationText\",\"role\":\"assistant\",\"content\":\"Hi!\"}"));
        QCOMPARE(t.type, E::ConversationText);
        QCOMPARE(t.role, QStringLiteral("assistant"));
        QCOMPARE(t.text, QStringLiteral("Hi!"));
        const E err = deepgram::parseEvent(QStringLiteral("{\"type\":\"Error\",\"code\":\"FAILED_TO_THINK\",\"description\":\"Failed to think.\"}"));
        QCOMPARE(err.type, E::Error);
        QCOMPARE(err.code, QStringLiteral("FAILED_TO_THINK"));
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"Warning\",\"code\":\"X\",\"description\":\"d\"}")).type, E::Warning);
        const E lat = deepgram::parseEvent(QStringLiteral("{\"type\":\"LatencyReport\",\"total_latency\":0.654}"));
        QCOMPARE(lat.type, E::LatencyReport);
        QVERIFY(qAbs(lat.totalLatency - 0.654) < 1e-9);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"LatencyReport\",\"ttt_text_latency\":0.5}")).totalLatency, -1.0);
        const E call = deepgram::parseEvent(QStringLiteral(
            "{\"type\":\"FunctionCallRequest\",\"functions\":[{\"id\":\"toolu_1\",\"name\":\"ask_buddy\",\"arguments\":\"{\\\"request\\\":\\\"Open Notepad\\\"}\",\"client_side\":true},{\"id\":\"srv\",\"name\":\"x\",\"arguments\":\"{}\",\"client_side\":false},{\"name\":\"noid\"}]}"));
        QCOMPARE(call.type, E::FunctionCallRequest);
        QCOMPARE(call.calls.size(), 2);                                   // the one without an id is dropped
        QCOMPARE(call.calls.at(0).request(), QStringLiteral("Open Notepad"));
        QVERIFY(call.calls.at(0).clientSide);
        QVERIFY(!call.calls.at(1).clientSide);
        QCOMPARE(call.calls.at(1).request(), QString());                  // not ask_buddy
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"FunctionCallCancelled\",\"ids\":[\"a\",\"b\"]}")).cancelledIds, (QStringList{ "a", "b" }));
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"FunctionCallCancelled\",\"functions\":[{\"id\":\"z\"}]}")).cancelledIds, (QStringList{ "z" }));
        QCOMPARE(deepgram::parseEvent(QStringLiteral("{\"type\":\"Brand New Thing\"}")).type, E::Unknown);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("not json")).type, E::Unknown);
        QCOMPARE(deepgram::parseEvent(QStringLiteral("[1,2]")).type, E::Unknown);
    }

    void clientMessages()
    {
        QCOMPARE(jsonOf(deepgram::keepAliveMessage())["type"].toString(), QStringLiteral("KeepAlive"));
        const QJsonObject inj = jsonOf(deepgram::injectAgentMessage(QStringLiteral("Hi"), QStringLiteral("queue")));
        QCOMPARE(inj["type"].toString(), QStringLiteral("InjectAgentMessage"));
        QCOMPARE(inj["behavior"].toString(), QStringLiteral("queue"));
        QCOMPARE(inj["message"].toString(), QStringLiteral("Hi"));
        QCOMPARE(jsonOf(deepgram::injectUserMessage(QStringLiteral("yo")))["content"].toString(), QStringLiteral("yo"));
        QCOMPARE(jsonOf(deepgram::updatePromptMessage(QStringLiteral("be nice")))["prompt"].toString(), QStringLiteral("be nice"));
        const QJsonObject sp = jsonOf(deepgram::updateSpeakMessage(QStringLiteral("aura-2-zeus-en"), 1.2));
        QCOMPARE(sp["type"].toString(), QStringLiteral("UpdateSpeak"));
        QCOMPARE(sp["speak"].toObject()["provider"].toObject()["model"].toString(), QStringLiteral("aura-2-zeus-en"));
        QCOMPARE(sp["speak"].toObject()["provider"].toObject()["speed"].toDouble(), 1.2);
        const QJsonObject r = jsonOf(deepgram::functionResponseMessage(QStringLiteral("id1"), QStringLiteral("ask_buddy"), QStringLiteral("Done")));
        QCOMPARE(r["type"].toString(), QStringLiteral("FunctionCallResponse"));
        QCOMPARE(r["id"].toString(), QStringLiteral("id1"));
        QCOMPARE(r["name"].toString(), QStringLiteral("ask_buddy"));
        QCOMPARE(r["content"].toString(), QStringLiteral("Done"));
    }

    void friendlyProblems()
    {
        QVERIFY(deepgram::friendlyProblem("FAILED_TO_THINK", "bad model").contains(QStringLiteral("bad model")));
        QVERIFY(!deepgram::friendlyProblem("ASR_DRIVER_TIMEOUT", "").isEmpty());
        QCOMPARE(deepgram::friendlyProblem("SOMETHING_NEW", "x"), QString());
        QVERIFY(deepgram::isQuietWarning("INJECT_AGENT_MESSAGE_DURING_USER_SPEECH"));
        QVERIFY(!deepgram::isQuietWarning("THINK_REQUEST_FAILED"));
    }

    void restAddressesAndParsing()
    {
        const QUrl l = deepgram::listenUrl(QStringLiteral("nova-3"), QStringLiteral("en"), true, { QStringLiteral("Noch"), QStringLiteral("Buddy") });
        QCOMPARE(l.path(), QStringLiteral("/v1/listen"));
        const QUrlQuery q(l);
        QCOMPARE(q.queryItemValue("model"), QStringLiteral("nova-3"));
        QCOMPARE(q.queryItemValue("smart_format"), QStringLiteral("true"));
        QCOMPARE(q.allQueryItemValues("keyterm"), (QStringList{ "Noch", "Buddy" }));
        // Older models do not take keyterm.
        QVERIFY(QUrlQuery(deepgram::listenUrl(QStringLiteral("nova-2"), QStringLiteral("en"), false, { QStringLiteral("x") })).allQueryItemValues("keyterm").isEmpty());
        const QUrlQuery s(deepgram::speakUrl(QStringLiteral("aura-2-thalia-en"), 24000, 1.0));
        QCOMPARE(s.queryItemValue("encoding"), QStringLiteral("linear16"));
        QCOMPARE(s.queryItemValue("container"), QStringLiteral("wav"));
        QCOMPARE(s.queryItemValue("sample_rate"), QStringLiteral("24000"));
        QVERIFY(!s.hasQueryItem("speed"));
        QCOMPARE(QUrlQuery(deepgram::speakUrl(QStringLiteral("m"), 24000, 1.25)).queryItemValue("speed"), QStringLiteral("1.25"));
        QCOMPARE(QUrlQuery(deepgram::speakUrl(QStringLiteral("m"), 24000, 5.0)).queryItemValue("speed"), QStringLiteral("1.50"));
        QCOMPARE(deepgram::authorizationHeader(QStringLiteral(" abc ")), QByteArray("Token abc"));
        QCOMPARE(deepgram::transcriptFromListen("{\"results\":{\"channels\":[{\"alternatives\":[{\"transcript\":\" Hello there. \"}]}]}}"), QStringLiteral("Hello there."));
        QCOMPARE(deepgram::transcriptFromListen("{\"results\":{\"channels\":[]}}"), QString());
        QCOMPARE(deepgram::transcriptFromListen("garbage"), QString());
        QCOMPARE(deepgram::restErrorMessage("{\"err_code\":\"INVALID_AUTH\",\"err_msg\":\"Invalid credentials.\"}"), QStringLiteral("Invalid credentials."));
        QCOMPARE(deepgram::restErrorMessage("{\"error\":{\"message\":\"nope\"}}"), QStringLiteral("nope"));
        QCOMPARE(deepgram::restErrorMessage("<html>"), QString());
    }

    void voiceAndModelLists()
    {
        QCOMPARE(deepgram::voiceLabel(QStringLiteral("aura-2-thalia-en")), QStringLiteral("Thalia (en)"));
        QCOMPARE(deepgram::voiceLabel(QStringLiteral("something-else")), QStringLiteral("something-else"));
        const QVariantList builtin = deepgram::builtinVoices();
        QVERIFY(builtin.size() > 30);
        QCOMPARE(builtin.first().toMap()["id"].toString(), QStringLiteral("aura-2-thalia-en"));
        const QByteArray models = "{\"tts\":[\n            {\"canonical_name\":\"aura-2-zeus-en\",\"languages\":[\"en\",\"en-US\"],\"metadata\":{\"accent\":\"American\",\"tags\":[\"masculine\",\"deep\"]}},\n            {\"canonical_name\":\"aura-2-agathe-fr\",\"languages\":[\"fr\",\"fr-FR\"],\"metadata\":{}},\n            {\"canonical_name\":\"aura-asteria-en\",\"languages\":[\"en\"],\"metadata\":{}},\n            {\"canonical_name\":\"aura-2-andromeda-en\",\"languages\":[\"en\",\"en-US\"],\"metadata\":{\"accent\":\"American\",\"tags\":[\"feminine\"]}}],\n            \"stt\":[]}";
        const QVariantList v = deepgram::voicesFromModels(models);
        QCOMPARE(v.size(), 3);                                            // Aura-1 is not offered
        QCOMPARE(v.at(0).toMap()["id"].toString(), QStringLiteral("aura-2-andromeda-en"));   // English first, sorted
        QCOMPARE(v.at(1).toMap()["id"].toString(), QStringLiteral("aura-2-zeus-en"));
        QCOMPARE(v.at(1).toMap()["tags"].toString(), QStringLiteral("masculine, deep"));
        QCOMPARE(v.at(2).toMap()["language"].toString(), QStringLiteral("fr"));
        QVERIFY(deepgram::voicesFromModels("nope").isEmpty());
        const QVariantList t = deepgram::thinkModelsFromJson("{\"models\":[{\"id\":\"claude-haiku-5-5\",\"name\":\"Claude Haiku 5.5\",\"provider\":\"anthropic\"},{\"name\":\"no id\"}]}");
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().toMap()["provider"].toString(), QStringLiteral("anthropic"));
    }

    // ---- streaming player -------------------------------------------------------------------------

    void resampleKeepsItsPlaceBetweenChunks()
    {
        // 24 kHz speech cut into odd-sized chunks and converted to 48 kHz stereo must come
        // out byte for byte as if it had been converted in one piece: no click at a seam.
        const QByteArray pcm = tone(0.3, 440, 0.4, 24000);
        QAudioFormat f;
        f.setSampleRate(48000);
        f.setChannelCount(2);
        f.setSampleFormat(QAudioFormat::Float);
        double p1 = 0;
        qint16 l1 = 0;
        const QByteArray whole = StreamingPlayer::resample(pcm, 24000, f, 1.0, &p1, &l1);
        double p2 = 0;
        qint16 l2 = 0;
        QByteArray pieces;
        int at = 0;
        const int sizes[] = { 200, 666, 2, 1000, 34, 4096, 90 };
        for (int i = 0; at < pcm.size(); i = (i + 1) % 7) {
            const int n = qMin<int>(sizes[i], pcm.size() - at);
            pieces += StreamingPlayer::resample(pcm.mid(at, n), 24000, f, 1.0, &p2, &l2);
            at += n;
        }
        QCOMPARE(pieces.size(), whole.size());
        QCOMPARE(pieces, whole);
        // 44.1 kHz (not an integer ratio) stays continuous too.
        f.setSampleRate(44100);
        f.setSampleFormat(QAudioFormat::Int16);
        p1 = p2 = 0;
        l1 = l2 = 0;
        const QByteArray w2 = StreamingPlayer::resample(pcm, 24000, f, 1.0, &p1, &l1);
        QByteArray c2;
        for (int i = 0; i < pcm.size(); i += 1000)
            c2 += StreamingPlayer::resample(pcm.mid(i, 1000), 24000, f, 1.0, &p2, &l2);
        QCOMPARE(c2, w2);
        const double expectFrames = double(pcm.size() / 2) * 44100 / 24000;
        QVERIFY(qAbs(double(w2.size() / 4) - expectFrames) <= 2);
    }

    void resampleVolumeAndClipping()
    {
        QByteArray pcm(2 * 4, '\0');
        auto *s = reinterpret_cast<qint16 *>(pcm.data());
        s[0] = 16000; s[1] = 16000; s[2] = 16000; s[3] = 16000;
        QAudioFormat f;
        f.setSampleRate(24000);
        f.setChannelCount(1);
        f.setSampleFormat(QAudioFormat::Int16);
        double p = 0;
        qint16 last = 0;
        const QByteArray half = StreamingPlayer::resample(pcm, 24000, f, 0.5, &p, &last);
        QVERIFY(half.size() >= 4);
        QVERIFY(qAbs(reinterpret_cast<const qint16 *>(half.constData())[0] - 8000) <= 2);
        p = 0;
        last = 0;
        const QByteArray loud = StreamingPlayer::resample(pcm, 24000, f, 1.5, &p, &last);
        QVERIFY(qAbs(reinterpret_cast<const qint16 *>(loud.constData())[0] - 24000) <= 2);
        p = 0;
        last = 0;
        s[0] = s[1] = s[2] = s[3] = 30000;
        const QByteArray clipped = StreamingPlayer::resample(pcm, 24000, f, 1.5, &p, &last);
        QCOMPARE(reinterpret_cast<const qint16 *>(clipped.constData())[0], qint16(32767));
        QVERIFY(StreamingPlayer::resample(QByteArray(), 24000, f, 1.0, &p, &last).isEmpty());
    }

    void playerDrainsAtPlaybackSpeedAndFlushes()
    {
        StreamingPlayer player;
        player.setDeviceDisabled(true);
        QSignalSpy drained(&player, &StreamingPlayer::drained);
        QVERIFY(player.idle());
        player.write(tone(0.4, 300, 0.3, 24000));
        QVERIFY(!player.idle());
        QVERIFY(player.bufferedSeconds() > 0.25 && player.bufferedSeconds() <= 0.41);
        QTest::qWait(100);
        QCOMPARE(drained.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 3000);
        QVERIFY(player.idle());
        // A flush stops it at once, without the drained signal.
        player.write(tone(2.0, 300, 0.3, 24000));
        QVERIFY(!player.idle());
        player.flush();
        QVERIFY(player.idle());
        QCOMPARE(player.bufferedSeconds(), 0.0);
        QTest::qWait(150);
        QCOMPARE(drained.count(), 1);
        // And it plays again afterwards.
        player.write(tone(0.1, 300, 0.3, 24000));
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 2, 3000);
    }

    void aTricklingStreamIsPrebufferedAndDoesNotStutter()
    {
        StreamingPlayer player;
        player.setDeviceDisabled(true);
        QSignalSpy drained(&player, &StreamingPlayer::drained);
        // Audio arrives in 40 ms slices, 70 ms apart (slower than it plays): without a jitter
        // buffer the player would run dry between every pair and report "drained" each time.
        for (int i = 0; i < 8; ++i) {
            player.write(tone(0.04, 300, 0.3, 24000));
            QTest::qWait(70);
            QVERIFY2(drained.count() == 0, "a brief gap in a stream must not count as the end");
        }
        QVERIFY(!player.idle());
        // A last piece after a longer silence than the hold ends it.
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 3000);
        QVERIFY(player.idle());
        // A single tiny slice still plays: the prebuffer wait is bounded.
        player.write(tone(0.02, 300, 0.3, 24000));
        QVERIFY(!player.idle());
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 2, 3000);
    }

    // ---- the live session ---------------------------------------------------------------------------

    void startRefusedWithoutKeyOrWhenOff()
    {
        Rig off(false);
        QVERIFY(!off.agent.start());
        QVERIFY(off.agent.error().contains(QStringLiteral("off")));
        QCOMPARE(off.agent.state(), QStringLiteral("off"));

        Rig r;
        r.agent.setApiKey(QString());
        QVERIFY(!r.agent.start());
        QVERIFY(r.agent.error().contains(QStringLiteral("API key")));
        QCOMPARE(r.server.connections, 0);               // nothing was sent anywhere
        QCOMPARE(r.mic->starts, 0);                       // and the microphone never opened
    }

    void connectsAppliesSettingsAndStreamsTheMicrophone()
    {
        Rig r;
        QSignalSpy started(&r.agent, &DeepgramAgent::started);
        QVERIFY(r.agent.start());
        QCOMPARE(r.agent.state(), QStringLiteral("connecting"));
        QCOMPARE(r.mic->starts, 0);                       // not before the server accepted the settings
        QTRY_COMPARE(r.agent.state(), QStringLiteral("listening"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(r.mic->starts, 1);
        QCOMPARE(r.server.header("Authorization"), QByteArray("Token ") + kKey.toLatin1());
        QVERIFY(r.server.request.startsWith("GET /"));
        QCOMPARE(r.settings["agent"].toObject()["think"].toObject()["provider"].toObject()["model"].toString(),
                 QStringLiteral("claude-haiku-5-5"));
        // Nothing but the Settings message was sent before the server said Welcome and SettingsApplied.
        QCOMPARE(typeOf(r.server.texts.first()), QStringLiteral("Settings"));

        // The microphone goes up in whole 40 ms frames, as it was recorded.
        const QByteArray sound = tone(0.12);
        r.mic->feed(sound.left(500));                     // a partial frame waits
        QTest::qWait(30);
        QCOMPARE(r.server.binaries.size(), 0);
        r.mic->feed(sound.mid(500));
        QTRY_VERIFY(r.server.binaries.size() >= 3);
        for (const QByteArray &b : std::as_const(r.server.binaries))
            QCOMPARE(b.size(), 1280);
        QCOMPARE(r.server.binaries.first(), sound.left(1280));
        QVERIFY2(r.server.allMasked, "client frames must be masked");
        QVERIFY(r.agent.level() > 0.05);
    }

    void aTurnOfConversation()
    {
        Rig r;
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QSignalSpy userSaid(&r.agent, &DeepgramAgent::userSaid), agentSaid(&r.agent, &DeepgramAgent::agentSaid);
        QSignalSpy transcript(&r.agent, &DeepgramAgent::transcriptChanged);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("UserStartedSpeaking")));
        r.server.sendText(convo(QStringLiteral("user"), QStringLiteral("What is two plus two?")));
        QTRY_COMPARE(userSaid.count(), 1);
        QCOMPARE(userSaid.first().first().toString(), QStringLiteral("What is two plus two?"));
        QCOMPARE(r.agent.userText(), QStringLiteral("What is two plus two?"));
        QCOMPARE(r.agent.state(), QStringLiteral("thinking"));
        r.server.sendText(eventJson(QStringLiteral("LatencyReport"), { { QStringLiteral("total_latency"), 0.654 } }));
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("Two plus two is four.")));
        r.server.sendBinary(tone(0.5, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        QCOMPARE(agentSaid.count(), 1);
        QCOMPARE(r.agent.agentText(), QStringLiteral("Two plus two is four."));
        QCOMPARE(r.agent.latencyMs(), 654);
        QCOMPARE(r.agent.player()->totalFrames(), qint64(12000));
        QVERIFY(r.agent.speaking());
        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTRY_COMPARE_WITH_TIMEOUT(r.agent.state(), QStringLiteral("listening"), 3000);     // once the sound has played
        QVERIFY(transcript.count() >= 2);
    }

    void speakerModeMutesTheMicrophoneWhileTheAgentTalks()
    {
        Rig r;                                              // speakers is the default
        QCOMPARE(r.agent.audioMode(), QStringLiteral("speakers"));
        QVERIFY(r.startAndWaitListening());
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 1);
        QVERIFY(!allZero(r.server.binaries.last()));         // open while it listens

        r.server.sendBinary(tone(0.6, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 2);
        // Silence goes up in its place, so the agent cannot hear (and answer) itself.
        QCOMPARE(r.server.binaries.last().size(), 1280);
        QVERIFY(allZero(r.server.binaries.last()));
        QCOMPARE(r.agent.level(), 0.0);

        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTRY_COMPARE_WITH_TIMEOUT(r.agent.state(), QStringLiteral("listening"), 3000);
        QTest::qWait(400);                                   // the room's echo dies down
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 3);
        QVERIFY(!allZero(r.server.binaries.last()));
    }

    void headphoneModeNeverMutes()
    {
        Rig r;
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QVERIFY(r.startAndWaitListening());
        r.server.sendBinary(tone(0.6, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 1);
        QVERIFY(!allZero(r.server.binaries.last()));         // you can talk over it
    }

    void mutedMeansSilenceNotNothing()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.agent.setMuted(true);
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 1);           // frames keep flowing so the line stays alive
        QVERIFY(allZero(r.server.binaries.last()));
        QCOMPARE(r.agent.level(), 0.0);
        r.agent.setMuted(false);
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 2);
        QVERIFY(!allZero(r.server.binaries.last()));
    }

    void bargeInStopsTheVoiceAtOnce()
    {
        Rig r;
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QVERIFY(r.startAndWaitListening());
        r.server.sendBinary(tone(2.0, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        QVERIFY(!r.agent.player()->idle());
        r.server.sendText(eventJson(QStringLiteral("UserStartedSpeaking")));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("listening"));
        QVERIFY(r.agent.player()->idle());                   // flushed
        // A chunk of the old answer that was already on the wire is thrown away...
        const qint64 frames = r.agent.player()->totalFrames();
        r.server.sendBinary(tone(0.2, 250, 0.3, 24000));
        QTest::qWait(60);
        QCOMPARE(r.agent.player()->totalFrames(), frames);
        QVERIFY(r.agent.player()->idle());
        // ...but the next answer is heard.
        QTest::qWait(150);
        r.server.sendBinary(tone(0.2, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        QVERIFY(r.agent.player()->totalFrames() > frames);
    }

    void interruptCutsTheAgentOffInSpeakerMode()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.server.sendBinary(tone(1.0, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.agent.interrupt();
        QCOMPARE(r.agent.state(), QStringLiteral("listening"));
        QVERIFY(r.agent.player()->idle());
        // The microphone is open again straight away, and the rest of that answer is ignored.
        r.speak();
        QTRY_COMPARE(r.server.binaries.size(), 1);
        QVERIFY(!allZero(r.server.binaries.last()));
        const qint64 frames = r.agent.player()->totalFrames();
        r.server.sendBinary(tone(0.3, 250, 0.3, 24000));
        QTest::qWait(60);
        QCOMPARE(r.agent.player()->totalFrames(), frames);
    }

    void askBuddyHandsATaskToTheAgentAndBack()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested), busy(&r.agent, &DeepgramAgent::buddyBusyChanged);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "toolu_1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"Open Notepad\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        QCOMPARE(asked.first().at(0).toString(), QStringLiteral("toolu_1"));
        QCOMPARE(asked.first().at(1).toString(), QStringLiteral("Open Notepad"));
        QVERIFY(r.agent.buddyBusy());

        r.agent.answerBuddy(QStringLiteral("toolu_1"), QStringLiteral("  Opened   Notepad.\n"));
        QTRY_COMPARE(r.sent("FunctionCallResponse").size(), 1);
        const QJsonObject resp = r.sent("FunctionCallResponse").first();
        QCOMPARE(resp["id"].toString(), QStringLiteral("toolu_1"));
        QCOMPARE(resp["name"].toString(), QStringLiteral("ask_buddy"));
        QCOMPARE(resp["content"].toString(), QStringLiteral("Opened Notepad."));
        QVERIFY(!r.agent.buddyBusy());
        // Answering twice sends nothing more.
        r.agent.answerBuddy(QStringLiteral("toolu_1"), QStringLiteral("again"));
        QTest::qWait(50);
        QCOMPARE(r.sent("FunctionCallResponse").size(), 1);
    }

    void longAnswersAreShortenedForTheVoice()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "c1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"summarise\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        r.agent.answerBuddy(QStringLiteral("c1"), QString(5000, 'x'));
        QTRY_COMPARE(r.sent("FunctionCallResponse").size(), 1);
        QVERIFY(r.sent("FunctionCallResponse").first()["content"].toString().size() < 1700);
    }

    void badFunctionCallsGetAnAnswerNotSilence()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "a" }, { "name", "launch_missiles" }, { "arguments", "{}" }, { "client_side", true } },
            QJsonObject{ { "id", "b" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"  \"}" }, { "client_side", true } },
            QJsonObject{ { "id", "c" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"x\"}" }, { "client_side", false } } } } }));
        QTRY_COMPARE(r.sent("FunctionCallResponse").size(), 2);
        QCOMPARE(asked.count(), 0);                          // nothing reached Buddy
        QVERIFY(r.sent("FunctionCallResponse").at(0)["content"].toString().contains(QStringLiteral("does not exist")));
        QVERIFY(r.sent("FunctionCallResponse").at(1)["content"].toString().contains(QStringLiteral("empty")));
    }

    void theUsersSpokenAnswerIsPassedOnAndAnswered()
    {
        Rig r;
        QSignalSpy answered(&r.agent, &DeepgramAgent::buddyAnswered), asked(&r.agent, &DeepgramAgent::buddyRequested);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "a1" }, { "name", "answer_buddy" }, { "arguments", "{\"answer\":\"the second one\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(asked.count(), 0);                          // not mistaken for a new task
        QCOMPARE(answered.first().at(0).toString(), QStringLiteral("a1"));
        QCOMPARE(answered.first().at(1).toString(), QStringLiteral("the second one"));
        QVERIFY(r.agent.buddyBusy());
        r.agent.answerBuddy(QStringLiteral("a1"), QStringLiteral("Making a Word file."));
        QTRY_COMPARE(r.sent("FunctionCallResponse").size(), 1);
        const QJsonObject resp = r.sent("FunctionCallResponse").first();
        QCOMPARE(resp["name"].toString(), QStringLiteral("answer_buddy"));
        QCOMPARE(resp["content"].toString(), QStringLiteral("Making a Word file."));
        // an empty answer is sent back as an instruction to ask again, not swallowed
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "a2" }, { "name", "answer_buddy" }, { "arguments", "{\"answer\":\"  \"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(r.sent("FunctionCallResponse").size(), 2);
        QVERIFY(r.sent("FunctionCallResponse").last()["content"].toString().contains(QStringLiteral("ask the user again"), Qt::CaseInsensitive));
        QCOMPARE(answered.count(), 1);
    }

    void withFollowUpsOffTheConversationEndsAfterTheReply()
    {
        Rig r;
        r.agent.setFollowUpSeconds(0);
        r.agent.setGraceMs(250, 900);
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QSignalSpy ended(&r.agent, &DeepgramAgent::ended);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(convo(QStringLiteral("user"), QStringLiteral("what time is it")));
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("It is noon.")));
        r.server.sendBinary(tone(0.2, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 3000);     // a moment after it stopped talking
        QCOMPARE(r.agent.state(), QStringLiteral("off"));
        QVERIFY(!r.mic->open);
        QVERIFY2(r.agent.error().isEmpty(), qPrintable(r.agent.error()));
    }

    void aQuestionKeepsTheLineOpenLonger()
    {
        Rig r;
        r.agent.setFollowUpSeconds(0);
        r.agent.setGraceMs(200, 1200);
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QSignalSpy ended(&r.agent, &DeepgramAgent::ended);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("Which format do you want, PDF or Word?")));
        r.server.sendBinary(tone(0.2, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTRY_COMPARE_WITH_TIMEOUT(r.agent.state(), QStringLiteral("listening"), 3000);
        QTest::qWait(600);                                      // well past the short grace
        QCOMPARE(ended.count(), 0);                             // it asked: it waits for the answer
        r.server.sendText(convo(QStringLiteral("user"), QStringLiteral("Word")));
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("Making a Word file.")));
        r.server.sendBinary(tone(0.2, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 4000);      // answered, no question this time: the short grace
    }

    void aTaskInProgressKeepsTheLineOpenWhateverTheFollowUpSetting()
    {
        Rig r;
        r.agent.setFollowUpSeconds(0);
        r.agent.setGraceMs(150, 150);
        r.agent.setAudioMode(QStringLiteral("headphones"));
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "k1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"long job\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("On it.")));
        r.server.sendBinary(tone(0.1, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.server.sendText(eventJson(QStringLiteral("AgentAudioDone")));
        QTest::qWait(900);
        QCOMPARE(r.agent.state() == QLatin1String("off"), false);   // Buddy is still working: stay on the line
    }

    void textQueuedBeforeTheCallIsReadyIsSentOnceItIs()
    {
        Rig r;
        r.agent.queueText(QStringLiteral("too early"));            // nothing is open: dropped
        QVERIFY(r.agent.start());
        r.agent.queueText(QStringLiteral("  open   notepad "));   // the wake phrase's own request
        QTRY_COMPARE(r.agent.state(), QStringLiteral("listening"));
        QTRY_COMPARE(r.sent("InjectUserMessage").size(), 1);
        QCOMPARE(r.sent("InjectUserMessage").first()["content"].toString(), QStringLiteral("open notepad"));
        r.agent.queueText(QStringLiteral("and a calculator"));     // ready: straight away
        QTRY_COMPARE(r.sent("InjectUserMessage").size(), 2);
    }

    void cancelledCallsAreDropped()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested), cancelled(&r.agent, &DeepgramAgent::buddyCancelled);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "z1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"do it\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        r.server.sendText(eventJson(QStringLiteral("FunctionCallCancelled"), { { QStringLiteral("ids"), QJsonArray{ "z1" } } }));
        QTRY_COMPARE(cancelled.count(), 1);
        QVERIFY(!r.agent.buddyBusy());
        r.agent.answerBuddy(QStringLiteral("z1"), QStringLiteral("too late"));
        QTest::qWait(50);
        QCOMPARE(r.sent("FunctionCallResponse").size(), 0);
    }

    void callsPendingWhenTheSessionEndsAreCancelled()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested), cancelled(&r.agent, &DeepgramAgent::buddyCancelled);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "e1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"x\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        r.agent.stop();
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(cancelled.first().first().toString(), QStringLiteral("e1"));
    }

    void fatalErrorsEndTheSessionWithAReadableReason()
    {
        Rig r;
        QSignalSpy ended(&r.agent, &DeepgramAgent::ended);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("Error"), { { QStringLiteral("code"), QStringLiteral("FAILED_TO_THINK") },
                                                               { QStringLiteral("description"), QStringLiteral("`temperature` is deprecated for this model.") } }));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("off"));
        QVERIFY(r.agent.error().contains(QStringLiteral("language model")));
        QVERIFY(r.agent.error().contains(QStringLiteral("temperature")));
        QCOMPARE(ended.count(), 1);
        QCOMPARE(r.mic->open, false);                         // the microphone is closed again
        r.agent.clearError();
        QVERIFY(r.agent.error().isEmpty());
    }

    void warningsAreNoticesNotFailures()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("Warning"), { { QStringLiteral("code"), QStringLiteral("INJECT_AGENT_MESSAGE_DURING_USER_SPEECH") },
                                                                 { QStringLiteral("description"), QStringLiteral("ignored") } }));
        r.server.sendText(eventJson(QStringLiteral("Warning"), { { QStringLiteral("code"), QStringLiteral("SLOW_THINK_REQUEST") },
                                                                 { QStringLiteral("description"), QStringLiteral("slow") } }));
        QTRY_VERIFY(!r.agent.notice().isEmpty());
        QVERIFY(r.agent.notice().contains(QStringLiteral("slow")));
        QCOMPARE(r.agent.state(), QStringLiteral("listening"));
        QVERIFY(r.agent.error().isEmpty());
    }

    void aRejectedKeyIsReportedPlainly()
    {
        Rig r;
        r.server.refuseStatus = 401;
        r.server.refuseBody = "{\"err_code\":\"INVALID_AUTH\",\"err_msg\":\"Invalid credentials.\"}";
        QVERIFY(r.agent.start());
        QTRY_COMPARE(r.agent.state(), QStringLiteral("off"));
        QVERIFY2(r.agent.error().contains(QStringLiteral("rejected the API key")), qPrintable(r.agent.error()));
        QCOMPARE(r.mic->starts, 0);
    }

    void aDroppedConnectionIsResumedWithTheConversation()
    {
        Rig r;
        r.agent.setGreeting(QStringLiteral("Hello!"));
        QSignalSpy started(&r.agent, &DeepgramAgent::started), ended(&r.agent, &DeepgramAgent::ended);
        QVERIFY(r.startAndWaitListening());
        QVERIFY(r.allSettings.first()["agent"].toObject().contains("greeting"));
        r.server.sendText(convo(QStringLiteral("user"), QStringLiteral("My name is Sam.")));
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("Nice to meet you, Sam.")));
        QTRY_COMPARE(r.agent.agentText(), QStringLiteral("Nice to meet you, Sam."));
        r.server.drop();
        QTRY_VERIFY(r.agent.notice().contains(QStringLiteral("Reconnecting")));
        QCOMPARE(r.agent.state(), QStringLiteral("connecting"));
        QTRY_COMPARE_WITH_TIMEOUT(r.allSettings.size(), 2, 5000);
        QTRY_COMPARE(r.agent.state(), QStringLiteral("listening"));
        // The second Settings carries what was said and does not greet again.
        const QJsonObject again = r.allSettings.last()["agent"].toObject();
        QVERIFY(!again.contains("greeting"));
        const QJsonArray msgs = again["context"].toObject()["messages"].toArray();
        QCOMPARE(msgs.size(), 2);
        QCOMPARE(msgs.at(0).toObject()["content"].toString(), QStringLiteral("My name is Sam."));
        QCOMPARE(msgs.at(1).toObject()["role"].toString(), QStringLiteral("assistant"));
        QCOMPARE(started.count(), 1);                         // one conversation, not two
        QCOMPARE(ended.count(), 0);
        QVERIFY(r.agent.notice().isEmpty());
        QCOMPARE(r.mic->starts, 2);
    }

    void givesUpAfterTooManyReconnects()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.server.refuseStatus = 503;                          // the service is down for good
        r.server.drop();
        QTRY_COMPARE_WITH_TIMEOUT(r.agent.state(), QStringLiteral("off"), 15000);
        QVERIFY(!r.agent.error().isEmpty());
        QCOMPARE(r.mic->open, false);
    }

    void stopClosesEverything()
    {
        Rig r;
        QSignalSpy ended(&r.agent, &DeepgramAgent::ended);
        QVERIFY(r.startAndWaitListening());
        r.server.sendBinary(tone(1.0, 250, 0.3, 24000));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("speaking"));
        r.agent.stop();
        QCOMPARE(r.agent.state(), QStringLiteral("off"));
        QCOMPARE(ended.count(), 1);
        QVERIFY(!r.mic->open);
        QVERIFY(r.agent.player()->idle());
        QTRY_VERIFY(r.server.clientClosed);                   // a polite goodbye reached the server
        QCOMPARE(r.server.closeCode, quint16(1000));
        // No audio goes anywhere afterwards, and stopping twice is harmless.
        const int frames = r.server.binaries.size();
        r.speak();
        QTest::qWait(50);
        QCOMPARE(r.server.binaries.size(), frames);
        r.agent.stop();
        QCOMPARE(ended.count(), 1);
        // A new conversation can follow.
        QVERIFY(r.startAndWaitListening());
    }

    void turningTheFeatureOffHangsUp()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.agent.setEnabled(false);
        QCOMPARE(r.agent.state(), QStringLiteral("off"));
        QVERIFY(!r.mic->open);
    }

    void aQuietLineEndsItselfAndSaysSo()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.agent.setIdleTimeoutMs(250);
        r.server.sendText(convo(QStringLiteral("user"), QStringLiteral("hi")));      // activity restarts the clock
        QTRY_COMPARE_WITH_TIMEOUT(r.agent.state(), QStringLiteral("off"), 3000);
        QVERIFY(r.agent.notice().contains(QStringLiteral("quiet")));
        QVERIFY(!r.mic->open);
    }

    void waitingForBuddyIsNotIdle()
    {
        Rig r;
        QSignalSpy asked(&r.agent, &DeepgramAgent::buddyRequested);
        QVERIFY(r.startAndWaitListening());
        r.server.sendText(eventJson(QStringLiteral("FunctionCallRequest"), { { QStringLiteral("functions"), QJsonArray{
            QJsonObject{ { "id", "w1" }, { "name", "ask_buddy" }, { "arguments", "{\"request\":\"long job\"}" }, { "client_side", true } } } } }));
        QTRY_COMPARE(asked.count(), 1);
        r.agent.setIdleTimeoutMs(200);
        r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("On it.")));
        QTest::qWait(700);
        QCOMPARE(r.agent.state() == QLatin1String("off"), false);   // still on the line while Buddy works
    }

    void keepAliveGoesOutRegularly()
    {
        Rig r;
        r.agent.setKeepAliveMs(80);
        QVERIFY(r.startAndWaitListening());
        QTRY_VERIFY(r.sent("KeepAlive").size() >= 2);
    }

    void aPromptAndVoiceChangeAppliesDuringTheConversation()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        QVERIFY(!r.agent.restartNeeded());
        r.agent.setPrompt(QStringLiteral("You are a calm librarian."));
        QTRY_COMPARE(r.sent("UpdatePrompt").size(), 1);
        QCOMPARE(r.sent("UpdatePrompt").first()["prompt"].toString(), QStringLiteral("You are a calm librarian."));
        r.agent.setSpeakModel(QStringLiteral("aura-2-zeus-en"));
        QTRY_COMPARE(r.sent("UpdateSpeak").size(), 1);
        QCOMPARE(r.sent("UpdateSpeak").first()["speak"].toObject()["provider"].toObject()["model"].toString(), QStringLiteral("aura-2-zeus-en"));
        r.agent.setSpeakSpeed(1.2);
        QTRY_COMPARE(r.sent("UpdateSpeak").size(), 2);
        QVERIFY(!r.agent.restartNeeded());
        // Changing the model or the listener needs a new conversation, and says so.
        r.agent.setThinkModel(QStringLiteral("claude-sonnet-5-5"));
        QVERIFY(r.agent.restartNeeded());
        QTest::qWait(50);
        QCOMPARE(r.sent("UpdatePrompt").size(), 1);
        r.agent.setThinkModel(QStringLiteral("claude-haiku-5-5"));
        QVERIFY(!r.agent.restartNeeded());
    }

    void defaultPromptIsStoredAsDefault()
    {
        DeepgramAgent a;
        a.setPrompt(a.defaultPrompt());
        QCOMPARE(a.prompt(), QString());
        a.setPrompt(QStringLiteral("custom"));
        QCOMPARE(a.prompt(), QStringLiteral("custom"));
    }

    void settingsAreNormalised()
    {
        DeepgramAgent a;
        a.setKeyterms(QStringLiteral("Noch, buddy ;NOCH\n  Moonshine  "));
        QCOMPARE(a.keyterms(), QStringLiteral("Noch, buddy, Moonshine"));
        a.setEotThreshold(0.1);
        QCOMPARE(a.eotThreshold(), 0.5);
        a.setSpeakSpeed(9);
        QCOMPARE(a.speakSpeed(), 1.5);
        a.setFollowUpSeconds(3);
        QCOMPARE(a.followUpSeconds(), 10);
        a.setFollowUpSeconds(0);
        QCOMPARE(a.followUpSeconds(), 0);
        a.setFollowUpSeconds(45);
        QCOMPARE(a.followUpSeconds(), 45);
        a.setAudioMode(QStringLiteral("nonsense"));
        QCOMPARE(a.audioMode(), QStringLiteral("speakers"));
        a.setApiKey(QStringLiteral("  abc  "));
        QCOMPARE(a.apiKey(), QStringLiteral("abc"));
        a.setListenModel(QStringLiteral("flux-general-en"));
        QVERIFY(a.flux());
        a.setThinkModel(QStringLiteral("claude-haiku-5-5"));
        QVERIFY(!a.temperatureApplies());
        a.setThinkModel(QStringLiteral("claude-haiku-4-5"));
        QVERIFY(a.temperatureApplies());
    }

    void sayAndSendTextReachTheAgent()
    {
        Rig r;
        r.agent.say(QStringLiteral("too early"));              // nothing is open yet
        QVERIFY(r.startAndWaitListening());
        r.agent.say(QStringLiteral("I need your approval to continue."));
        r.agent.sendText(QStringLiteral("  open   notepad "));
        QTRY_COMPARE(r.sent("InjectAgentMessage").size(), 1);
        QCOMPARE(r.sent("InjectAgentMessage").first()["behavior"].toString(), QStringLiteral("queue"));
        QCOMPARE(r.sent("InjectAgentMessage").first()["message"].toString(), QStringLiteral("I need your approval to continue."));
        QTRY_COMPARE(r.sent("InjectUserMessage").size(), 1);
        QCOMPARE(r.sent("InjectUserMessage").first()["content"].toString(), QStringLiteral("open notepad"));
    }

    void microphoneFailureEndsTheConversation()
    {
        Rig r;
        r.mic->startOk = false;
        r.mic->startError = QStringLiteral("No microphone was found.");
        QVERIFY(r.agent.start());
        QTRY_COMPARE(r.agent.state(), QStringLiteral("off"));
        QCOMPARE(r.agent.error(), QStringLiteral("No microphone was found."));
    }

    void microphoneDyingMidConversationEndsIt()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.mic->breakDown(QStringLiteral("The microphone was unplugged."));
        QTRY_COMPARE(r.agent.state(), QStringLiteral("off"));
        QCOMPARE(r.agent.error(), QStringLiteral("The microphone was unplugged."));
    }

    void connectionTestChecksTheModelNotJustTheSocket()
    {
        Rig r;
        QObject::connect(&r.server, &WsTestServer::textReceived, &r.server, [&r](const QString &t) {
            if (typeOf(t) == QLatin1String("InjectUserMessage"))
                r.server.sendText(convo(QStringLiteral("assistant"), QStringLiteral("Ready.")));
        });
        QSignalSpy changed(&r.agent, &DeepgramAgent::testChanged);
        r.agent.test();
        QCOMPARE(r.agent.testState(), QStringLiteral("testing"));
        QTRY_COMPARE(r.agent.testState(), QStringLiteral("ok"));
        QVERIFY2(r.agent.testMessage().contains(QStringLiteral("claude-haiku-5-5")), qPrintable(r.agent.testMessage()));
        QCOMPARE(r.agent.state(), QStringLiteral("off"));      // the test never opens the microphone
        QCOMPARE(r.mic->starts, 0);
        QVERIFY(!r.allSettings.isEmpty());
        QVERIFY(!r.allSettings.first()["agent"].toObject().contains("greeting"));
    }

    void connectionTestReportsAModelThatFails()
    {
        Rig r;
        QObject::connect(&r.server, &WsTestServer::textReceived, &r.server, [&r](const QString &t) {
            if (typeOf(t) == QLatin1String("InjectUserMessage"))
                r.server.sendText(eventJson(QStringLiteral("Error"), { { QStringLiteral("code"), QStringLiteral("FAILED_TO_THINK") },
                                                                       { QStringLiteral("description"), QStringLiteral("model not found") } }));
        });
        r.agent.test();
        QTRY_COMPARE(r.agent.testState(), QStringLiteral("failed"));
        QVERIFY(r.agent.testMessage().contains(QStringLiteral("language model")));
    }

    void connectionTestReportsABadKeyAndMissingSetup()
    {
        Rig r;
        r.server.refuseStatus = 401;
        r.agent.test();
        QTRY_COMPARE(r.agent.testState(), QStringLiteral("failed"));
        QVERIFY(r.agent.testMessage().contains(QStringLiteral("rejected")));

        Rig none;
        none.agent.setApiKey(QString());
        none.agent.test();
        QCOMPARE(none.agent.testState(), QStringLiteral("failed"));
        QVERIFY(none.agent.testMessage().contains(QStringLiteral("API key")));
        QCOMPARE(none.server.connections, 0);
    }

    void connectionTestCannotRunDuringAConversation()
    {
        Rig r;
        QVERIFY(r.startAndWaitListening());
        r.agent.test();
        QCOMPARE(r.agent.testState(), QStringLiteral("failed"));
        QVERIFY(r.agent.testMessage().contains(QStringLiteral("End the conversation")));
        QCOMPARE(r.server.connections, 1);
    }

    void listsLoadFromDeepgram()
    {
        FakeStt think, models;
        think.reply = "{\"models\":[{\"id\":\"claude-haiku-5-5\",\"name\":\"Claude Haiku 5.5\",\"provider\":\"anthropic\"},{\"id\":\"gpt-4o-mini\",\"name\":\"GPT-4o mini\",\"provider\":\"open_ai\"}]}";
        models.reply = "{\"tts\":[{\"canonical_name\":\"aura-2-zeus-en\",\"languages\":[\"en\"],\"metadata\":{}}],\"stt\":[]}";
        DeepgramAgent a;
        a.setApiKey(kKey);
        a.setThinkModelsUrl(think.base() + QStringLiteral("/v1/agent/settings/think/models"));
        a.setApiBase(models.base());
        QSignalSpy changed(&a, &DeepgramAgent::modelsChanged);
        QVERIFY(a.voices().size() > 30);                       // the built-in list is there from the start
        a.refreshModels();
        QVERIFY(a.modelsLoading());
        QTRY_VERIFY(!a.modelsLoading());
        QCOMPARE(a.thinkModels().size(), 2);
        QCOMPARE(a.thinkModels().first().toMap()["id"].toString(), QStringLiteral("claude-haiku-5-5"));
        QCOMPARE(a.voices().size(), 1);
        QVERIFY(a.modelsProblem().isEmpty());
        QVERIFY(think.headers.contains("authorization: token " + kKey.toLatin1()));
        QVERIFY(think.requestLine.startsWith("GET /v1/agent/settings/think/models"));
    }

    void listsReportProblemsAndKeepTheOldOnes()
    {
        FakeStt think, models;
        think.status = 401;
        think.reply = "{\"err_msg\":\"nope\"}";
        models.status = 500;
        models.reply = "oops";
        DeepgramAgent a;
        a.setApiKey(kKey);
        a.setThinkModelsUrl(think.base() + QStringLiteral("/x"));
        a.setApiBase(models.base());
        const int builtin = a.voices().size();
        a.refreshModels();
        QTRY_VERIFY(!a.modelsLoading());
        QVERIFY(!a.modelsProblem().isEmpty());
        QCOMPARE(a.voices().size(), builtin);                  // a failed refresh does not empty the picker

        DeepgramAgent noKey;
        noKey.refreshModels();
        QVERIFY(noKey.modelsProblem().contains(QStringLiteral("API key")));
        QVERIFY(!noKey.modelsLoading());
    }
};

QTEST_MAIN(tst_Deepgram)
#include "tst_deepgram.moc"
