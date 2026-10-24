#include "DeepgramSettings.h"

#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

#include <algorithm>

#include "DeepgramProtocol.h"

namespace {

// The factory think choice, used when neither an override nor a usable
// Buddy configuration exists — the live agent's historical out-of-box
// behaviour (Deepgram runs Anthropic's small model).
DeepgramSettings::ThinkConfig factoryThink()
{
    return { QStringLiteral("anthropic"), QStringLiteral("claude-haiku-5-5"), -1.0, QString(), QString() };
}

// Buddy's endpoint for a provider Deepgram cannot run as a managed model:
// the chat-completions address under Buddy's configured base. Bases custom
// providers are saved with vary (some end in /v1, some in the host root),
// so only the missing tail is appended.
QString buddyChatEndpoint(const QString &base)
{
    QString url = base.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    if (url.endsWith(QLatin1String("/chat/completions")))
        return url;
    if (url.endsWith(QLatin1String("/v1")))
        return url + QStringLiteral("/chat/completions");
    return url + QStringLiteral("/v1/chat/completions");
}

bool usableHttpsEndpoint(const QString &base)
{
    const QUrl u(base.trimmed());
    return u.isValid() && u.scheme() == QLatin1String("https") && !u.host().isEmpty();
}

} // namespace

DeepgramSettings::DeepgramSettings(QObject *parent)
    : QObject(parent)
{
}

void DeepgramSettings::setString(QString &field, const QString &value)
{
    const QString v = value.trimmed();
    if (v == field)
        return;
    field = v;
    emit configChanged();
}

void DeepgramSettings::setTemperatureField(double &field, double t)
{
    const double v = t < 0 ? -1.0 : std::clamp(t, 0.0, 2.0);
    if (qFuzzyCompare(v + 2.0, field + 2.0))
        return;
    field = v;
    emit configChanged();
}

void DeepgramSettings::setApiKey(const QString &key) { setString(m_apiKey, key); }
void DeepgramSettings::setListenModel(const QString &model) { setString(m_listenModel, model); }
void DeepgramSettings::setListenLanguage(const QString &language) { setString(m_listenLanguage, language); }
void DeepgramSettings::setKeyterms(const QString &terms)
{
    // Keyterms are edited as free text; keep the text as typed (the list
    // form is derived), only the stored value changes matter here.
    if (terms == m_keyterms)
        return;
    m_keyterms = terms;
    emit configChanged();
}
void DeepgramSettings::setSpeakVoice(const QString &voice) { setString(m_speakVoice, voice); }

void DeepgramSettings::setThinkProvider(const QString &provider) { setString(m_thinkProvider, provider); }
void DeepgramSettings::setThinkModel(const QString &model) { setString(m_thinkModel, model); }
void DeepgramSettings::setThinkTemperature(double t) { setTemperatureField(m_thinkTemperature, t); }
void DeepgramSettings::setThinkEndpointUrl(const QString &url) { setString(m_thinkEndpointUrl, url); }
void DeepgramSettings::setThinkEndpointKey(const QString &key) { setString(m_thinkEndpointKey, key); }

void DeepgramSettings::setBuddyProvider(const QString &provider) { setString(m_buddyProvider, provider); }
void DeepgramSettings::setBuddyModel(const QString &model) { setString(m_buddyModel, model); }
void DeepgramSettings::setBuddyTemperature(double t)
{
    // Buddy's own temperature is a plain 0..2 value (no "default" sentinel).
    if (qFuzzyCompare(t + 2.0, m_buddyTemperature + 2.0))
        return;
    m_buddyTemperature = t;
    emit configChanged();
}
void DeepgramSettings::setBuddyApiBase(const QString &base) { setString(m_buddyApiBase, base); }
void DeepgramSettings::setBuddyApiKey(const QString &key) { setString(m_buddyApiKey, key); }

void DeepgramSettings::setThinkOverride(bool on)
{
    if (on == m_thinkOverride)
        return;
    if (on) {
        // Turning the override on starts from what is in effect right now,
        // so the fields the user is about to edit show the live values and
        // nothing changes until they actually edit one.
        const ThinkConfig now = effectiveThink();
        m_thinkProvider = now.provider;
        m_thinkModel = now.model;
        m_thinkTemperature = now.temperature;
        m_thinkEndpointUrl = now.endpointUrl;
        m_thinkEndpointKey = now.endpointKey;
    }
    m_thinkOverride = on;
    emit configChanged();
}

DeepgramSettings::ThinkConfig DeepgramSettings::effectiveThink() const
{
    if (m_thinkOverride)
        return { m_thinkProvider, m_thinkModel, m_thinkTemperature, m_thinkEndpointUrl, m_thinkEndpointKey };

    // Follow Buddy's chat configuration. Providers Deepgram runs itself map
    // to its managed provider types (it uses its own keys for those, exactly
    // as the built-in default always did); any other configured provider is
    // reached through Buddy's own endpoint, which Deepgram's servers call
    // with Buddy's key.
    const QString provider = m_buddyProvider.trimmed().toLower();
    const QString model = m_buddyModel.trimmed();
    if (!model.isEmpty()) {
        if (provider == QLatin1String("anthropic"))
            return { QStringLiteral("anthropic"), model, m_buddyTemperature, QString(), QString() };
        if (provider == QLatin1String("openai"))
            return { QStringLiteral("open_ai"), model, m_buddyTemperature, QString(), QString() };
        if (provider == QLatin1String("groq"))
            return { QStringLiteral("groq"), model, m_buddyTemperature, QString(), QString() };
        if (!provider.isEmpty() && provider != QLatin1String("none") && usableHttpsEndpoint(m_buddyApiBase))
            return { QStringLiteral("open_ai"), model, m_buddyTemperature,
                     buddyChatEndpoint(m_buddyApiBase), m_buddyApiKey };
    }
    return factoryThink();
}

QString DeepgramSettings::thinkSource() const
{
    if (m_thinkOverride)
        return QStringLiteral("override");
    const ThinkConfig t = effectiveThink();
    const ThinkConfig f = factoryThink();
    const bool isFactory = t.provider == f.provider && t.model == f.model && t.endpointUrl.isEmpty();
    return isFactory ? QStringLiteral("default") : QStringLiteral("buddy");
}

bool DeepgramSettings::effectiveTemperatureApplies() const
{
    const ThinkConfig t = effectiveThink();
    return deepgram::modelAcceptsTemperature(t.provider, t.model);
}

QStringList DeepgramSettings::parseKeyterms(const QString &text)
{
    QStringList list;
    for (const QString &t : text.split(QRegularExpression(QStringLiteral("[,\\n;]")), Qt::SkipEmptyParts)) {
        const QString term = t.simplified();
        if (!term.isEmpty() && !list.contains(term, Qt::CaseInsensitive))
            list << term;
    }
    return list;
}

DeepgramListenSettings DeepgramSettings::listenSettings() const
{
    DeepgramListenSettings s;
    s.apiKey = m_apiKey;
    s.model = m_listenModel;
    s.language = m_listenLanguage;
    s.keyterms = keytermList();
    return s;
}

void DeepgramSettings::applyToAgentConfig(deepgram::AgentConfig &cfg) const
{
    const ThinkConfig think = effectiveThink();
    cfg.apiKey = m_apiKey;
    cfg.listenModel = m_listenModel;
    cfg.listenLanguage = m_listenLanguage;
    cfg.keyterms = keytermList();
    cfg.thinkProvider = think.provider;
    cfg.thinkModel = think.model;
    cfg.temperature = think.temperature;
    cfg.endpointUrl = think.endpointUrl;
    cfg.endpointKey = think.endpointKey;
    cfg.speakModel = m_speakVoice;
}

void migrateDeepgramSettings(QSettings &settings)
{
    const QString flagKey = QStringLiteral("ui/buddyLiveThinkOverride");
    if (settings.contains(flagKey))
        return;   // migrated (or chosen) already: never second-guess it

    const QString provider = settings.value(QStringLiteral("ui/buddyLiveProvider"),
                                            QStringLiteral("anthropic")).toString();
    const QString model = settings.value(QStringLiteral("ui/buddyLiveModel"),
                                         QStringLiteral("claude-haiku-5-5")).toString();
    const double temperature = settings.value(QStringLiteral("ui/buddyLiveTemperature"), -1.0).toDouble();
    const QString endpoint = settings.value(QStringLiteral("ui/buddyLiveEndpoint")).toString();

    const bool customized = provider.trimmed() != QLatin1String("anthropic")
        || model.trimmed() != QLatin1String("claude-haiku-5-5")
        || temperature >= 0
        || !endpoint.trimmed().isEmpty();
    settings.setValue(flagKey, customized);
}
