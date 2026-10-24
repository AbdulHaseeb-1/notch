// DeepgramSettings: the ONE Deepgram configuration for the whole app.
//
// Deepgram used to be configured three times over: push-to-talk (VoiceInput)
// held the listen model/language/keyterms, the live agent (DeepgramAgent)
// held the same listen fields again plus the API key, the "think" model and
// the speak voice, and spoken answers (SpeechOut) held the key and the voice
// a third time. The copies could drift, and the settings were spread over
// three QML binding fans. Now there is one object:
//
//   * apiKey               the Deepgram key (the value arrives from the
//                          Secrets vault; it is never stored in settings),
//   * listenModel / listenLanguage / keyterms
//                          speech-to-text, shared by push-to-talk, the wake
//                          word follow-up and the live agent,
//   * speakVoice           the Aura voice, shared by the live agent and
//                          spoken answers,
//   * the "think" (LLM) choice for the live agent, below.
//
// VoiceInput, DeepgramAgent and SpeechOut each take this object through
// setDeepgramSettings() and read from it; none of them keeps its own copy.
//
// The think choice: by default the live conversation uses the SAME model as
// Buddy's chat (BuddyProvider's provider/model/temperature and, for a
// provider Deepgram cannot run itself, Buddy's own endpoint, which
// Deepgram's servers call). The Deepgram-specific think fields in this
// object are an explicit override (thinkOverride): they apply only when the
// user has turned "use a different model for voice calls" on. When Buddy has
// no usable model configured either, the historical built-in default
// (Anthropic / claude-haiku-5-5, run by Deepgram) applies, so a fresh install
// with only a Deepgram key behaves exactly as it always did.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "Transcriber.h"

class QSettings;

namespace deepgram {
struct AgentConfig;
}

class DeepgramSettings : public QObject
{
    Q_OBJECT
    // the shared service settings
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY configChanged)
    Q_PROPERTY(QString listenModel READ listenModel WRITE setListenModel NOTIFY configChanged)
    Q_PROPERTY(QString listenLanguage READ listenLanguage WRITE setListenLanguage NOTIFY configChanged)
    Q_PROPERTY(QString keyterms READ keyterms WRITE setKeyterms NOTIFY configChanged)
    Q_PROPERTY(QString speakVoice READ speakVoice WRITE setSpeakVoice NOTIFY configChanged)
    // the think override (only applies while thinkOverride is on)
    Q_PROPERTY(bool thinkOverride READ thinkOverride WRITE setThinkOverride NOTIFY configChanged)
    Q_PROPERTY(QString thinkProvider READ thinkProvider WRITE setThinkProvider NOTIFY configChanged)
    Q_PROPERTY(QString thinkModel READ thinkModel WRITE setThinkModel NOTIFY configChanged)
    Q_PROPERTY(double thinkTemperature READ thinkTemperature WRITE setThinkTemperature NOTIFY configChanged)
    Q_PROPERTY(QString thinkEndpointUrl READ thinkEndpointUrl WRITE setThinkEndpointUrl NOTIFY configChanged)
    Q_PROPERTY(QString thinkEndpointKey READ thinkEndpointKey WRITE setThinkEndpointKey NOTIFY configChanged)
    // Buddy's chat configuration, fed in so the effective think choice can
    // follow it (see the class comment). Not user settings of this object.
    Q_PROPERTY(QString buddyProvider READ buddyProvider WRITE setBuddyProvider NOTIFY configChanged)
    Q_PROPERTY(QString buddyModel READ buddyModel WRITE setBuddyModel NOTIFY configChanged)
    Q_PROPERTY(double buddyTemperature READ buddyTemperature WRITE setBuddyTemperature NOTIFY configChanged)
    Q_PROPERTY(QString buddyApiBase READ buddyApiBase WRITE setBuddyApiBase NOTIFY configChanged)
    Q_PROPERTY(QString buddyApiKey READ buddyApiKey WRITE setBuddyApiKey NOTIFY configChanged)
    // the think choice as it will actually be used
    Q_PROPERTY(QString effectiveThinkProvider READ effectiveThinkProvider NOTIFY configChanged)
    Q_PROPERTY(QString effectiveThinkModel READ effectiveThinkModel NOTIFY configChanged)
    Q_PROPERTY(double effectiveTemperature READ effectiveTemperature NOTIFY configChanged)
    Q_PROPERTY(QString effectiveThinkEndpointUrl READ effectiveThinkEndpointUrl NOTIFY configChanged)
    Q_PROPERTY(QString effectiveThinkEndpointKey READ effectiveThinkEndpointKey NOTIFY configChanged)
    Q_PROPERTY(bool effectiveTemperatureApplies READ effectiveTemperatureApplies NOTIFY configChanged)
    // "override" | "buddy" | "default": where the effective think choice comes from.
    Q_PROPERTY(QString thinkSource READ thinkSource NOTIFY configChanged)

public:
    explicit DeepgramSettings(QObject *parent = nullptr);

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &key);
    QString listenModel() const { return m_listenModel; }
    void setListenModel(const QString &model);
    QString listenLanguage() const { return m_listenLanguage; }
    void setListenLanguage(const QString &language);
    // The keyterms as the settings pages edit them (comma separated text).
    QString keyterms() const { return m_keyterms; }
    void setKeyterms(const QString &terms);
    QString speakVoice() const { return m_speakVoice; }
    void setSpeakVoice(const QString &voice);

    bool thinkOverride() const { return m_thinkOverride; }
    void setThinkOverride(bool on);
    QString thinkProvider() const { return m_thinkProvider; }
    void setThinkProvider(const QString &provider);
    QString thinkModel() const { return m_thinkModel; }
    void setThinkModel(const QString &model);
    double thinkTemperature() const { return m_thinkTemperature; }
    void setThinkTemperature(double t);
    QString thinkEndpointUrl() const { return m_thinkEndpointUrl; }
    void setThinkEndpointUrl(const QString &url);
    QString thinkEndpointKey() const { return m_thinkEndpointKey; }
    void setThinkEndpointKey(const QString &key);

    QString buddyProvider() const { return m_buddyProvider; }
    void setBuddyProvider(const QString &provider);
    QString buddyModel() const { return m_buddyModel; }
    void setBuddyModel(const QString &model);
    double buddyTemperature() const { return m_buddyTemperature; }
    void setBuddyTemperature(double t);
    QString buddyApiBase() const { return m_buddyApiBase; }
    void setBuddyApiBase(const QString &base);
    QString buddyApiKey() const { return m_buddyApiKey; }
    void setBuddyApiKey(const QString &key);

    // The think choice resolved: the override when on, else Buddy's chat
    // configuration, else the built-in default.
    struct ThinkConfig {
        QString provider;
        QString model;
        double temperature = -1.0;   // < 0 = the model's own default
        QString endpointUrl;
        QString endpointKey;
    };
    ThinkConfig effectiveThink() const;
    QString effectiveThinkProvider() const { return effectiveThink().provider; }
    QString effectiveThinkModel() const { return effectiveThink().model; }
    double effectiveTemperature() const { return effectiveThink().temperature; }
    QString effectiveThinkEndpointUrl() const { return effectiveThink().endpointUrl; }
    QString effectiveThinkEndpointKey() const { return effectiveThink().endpointKey; }
    bool effectiveTemperatureApplies() const;
    QString thinkSource() const;

    // The keyterms as a list (split on commas/newlines/semicolons, tidied,
    // de-duplicated) — the form both listen paths send.
    QStringList keytermList() const { return parseKeyterms(m_keyterms); }
    static QStringList parseKeyterms(const QString &text);

    // The push-to-talk transcriber's settings (apiBase stays empty; tests
    // override it on VoiceInput, not here).
    DeepgramListenSettings listenSettings() const;
    // Fills the Deepgram-owned fields of a live-agent configuration (key,
    // listen, effective think, speak voice) from this object. The agent's
    // own fields (prompt, greeting, end-of-turn, speak speed, ...) are not
    // touched.
    void applyToAgentConfig(deepgram::AgentConfig &cfg) const;

signals:
    void configChanged();

private:
    void setString(QString &field, const QString &value);
    void setTemperatureField(double &field, double t);

    QString m_apiKey;
    QString m_listenModel = QStringLiteral("nova-3");
    QString m_listenLanguage = QStringLiteral("en");
    QString m_keyterms;
    QString m_speakVoice = QStringLiteral("aura-2-thalia-en");

    bool m_thinkOverride = false;
    QString m_thinkProvider = QStringLiteral("anthropic");
    QString m_thinkModel = QStringLiteral("claude-haiku-5-5");
    double m_thinkTemperature = -1.0;
    QString m_thinkEndpointUrl;
    QString m_thinkEndpointKey;

    QString m_buddyProvider = QStringLiteral("none");
    QString m_buddyModel;
    double m_buddyTemperature = 0.7;
    QString m_buddyApiBase;
    QString m_buddyApiKey;
};

// One startup sweep, in the spirit of the secrets migration: older versions
// had no "follow Buddy" choice, so a live-agent think configuration the
// user actually changed (provider, model, temperature or endpoint different
// from the factory defaults) becomes an explicit override, and one they
// never touched starts following Buddy. Writes ui/buddyLiveThinkOverride
// only when it is not already there, so it runs once and never overrides
// the user's later choice. Call before QML loads its settings.
void migrateDeepgramSettings(QSettings &settings);
