// The settings schema registry: the table in src/common/SettingsSchema.h,
// its QML bridge, and the projection Main.qml's Settings object consumes.
//
// The golden defaults below are the values Main.qml declared before this
// refactor (extracted mechanically from the pre-refactor file); the test
// pins the registry to them so the refactor cannot silently change what a
// fresh install sees. Any future default change must update this list
// deliberately, in the same commit as the table.
#include <QtTest>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQml/qqml.h>

#include "SecretKeys.h"
#include "SettingsRegistry.h"
#include "SettingsSchema.h"

using noch::settingschema::Entry;
using noch::settingschema::Type;

namespace {

// The pre-refactor Main.qml cfg defaults: {QML property name, type, value}.
struct Golden {
    const char *name;
    Type type;
    QVariant value;
};

const QVector<Golden> golden = {
        { "sizeScale", Type::Real, QVariant(1.0) },
        { "autoHide", Type::Bool, QVariant(true) },
        { "hideDelay", Type::Int, QVariant(1200) },
        { "focusMinutes", Type::Int, QVariant(25) },
        { "hotkeyPalette", Type::Bool, QVariant(true) },
        { "suppressInFullscreen", Type::Bool, QVariant(true) },
        { "buddyEnabled", Type::Bool, QVariant(true) },
        { "buddyRingStyle", Type::String, QVariant(QStringLiteral("neon")) },
        { "buddyRingThickness", Type::Real, QVariant(1.0) },
        { "buddyRingSpeed", Type::Real, QVariant(1.0) },
        { "buddyRingShader", Type::Bool, QVariant(true) },
        { "buddyGlowStrength", Type::Real, QVariant(0.6) },
        { "buddyGlowMode", Type::String, QVariant(QStringLiteral("bloom")) },
        { "buddyGlowAlways", Type::Bool, QVariant(false) },
        { "buddyVoiceFx", Type::String, QVariant(QStringLiteral("full")) },
        { "buddyX", Type::Real, QVariant(-1.0) },
        { "buddyY", Type::Real, QVariant(-1.0) },
        { "buddyPlan", Type::String, QVariant(QStringLiteral("")) },
        { "buddyHistory", Type::String, QVariant(QStringLiteral("[]")) },
        { "buddyProvider", Type::String, QVariant(QStringLiteral("none")) },
        { "buddyApiBase", Type::String, QVariant(QStringLiteral("")) },
        { "buddyModel", Type::String, QVariant(QStringLiteral("gpt-4o-mini")) },
        { "buddyTemperature", Type::Real, QVariant(0.7) },
        { "buddyProviderConfigs", Type::String, QVariant(QStringLiteral("{}")) },
        { "buddyAgentMode", Type::Bool, QVariant(true) },
        { "buddyAllowBash", Type::Bool, QVariant(true) },
        { "buddyAllowComputer", Type::Bool, QVariant(false) },
        { "buddyCuShowCursor", Type::Bool, QVariant(true) },
        { "buddyCuShowGlow", Type::Bool, QVariant(true) },
        { "buddyCuGlowIntensity", Type::Real, QVariant(0.6) },
        { "buddyCuGlowStyle", Type::String, QVariant(QStringLiteral("soft")) },
        { "buddyAllowEditor", Type::Bool, QVariant(true) },
        { "buddyAllowCodeSearch", Type::Bool, QVariant(true) },
        { "buddyAllowMemory", Type::Bool, QVariant(true) },
        { "buddyAllowWebSearch", Type::Bool, QVariant(true) },
        { "buddyAllowTodoTask", Type::Bool, QVariant(true) },
        { "buddyOfficialComputer", Type::Bool, QVariant(true) },
        { "buddyComputerDetail", Type::String, QVariant(QStringLiteral("sharp")) },
        { "buddyMaxSteps", Type::Int, QVariant(40) },
        { "buddyAllowAskUser", Type::Bool, QVariant(true) },
        { "buddyAllowScreenshots", Type::Bool, QVariant(true) },
        { "buddyAllowMusic", Type::Bool, QVariant(true) },
        { "buddyAllowConnectors", Type::Bool, QVariant(true) },
        { "buddyAllowSkills", Type::Bool, QVariant(true) },
        { "buddyDisabledSkills", Type::String, QVariant(QStringLiteral("")) },
        { "buddyVoiceEnabled", Type::Bool, QVariant(false) },
        { "buddyVoiceHotkey", Type::Bool, QVariant(true) },
        { "buddyVoiceAutoSend", Type::Bool, QVariant(false) },
        { "buddyWakeEnabled", Type::Bool, QVariant(false) },
        { "buddyWakePhrase", Type::String, QVariant(QStringLiteral("hey buddy")) },
        { "buddyWakeThreshold", Type::Real, QVariant(0.5) },
        { "buddyWakeWords", Type::String, QVariant(QStringLiteral("hey_buddy.onnx")) },
        { "buddySpeakReports", Type::Bool, QVariant(false) },
        { "buddyTtsEngine", Type::String, QVariant(QStringLiteral("google")) },
        { "buddyTtsGoogleVoice", Type::String, QVariant(QStringLiteral("Kore")) },
        { "buddyTtsSpeed", Type::Real, QVariant(1.0) },
        { "buddyTtsSpeakAll", Type::Bool, QVariant(false) },
        { "buddyYtDlpPath", Type::String, QVariant(QStringLiteral("")) },
        { "buddyLiveEnabled", Type::Bool, QVariant(false) },
        { "buddyLiveListenModel", Type::String, QVariant(QStringLiteral("nova-3")) },
        { "buddyLiveLanguage", Type::String, QVariant(QStringLiteral("en")) },
        { "buddyLiveKeyterms", Type::String, QVariant(QStringLiteral("Buddy, Noch, Notepad")) },
        { "buddyLiveEot", Type::Real, QVariant(0.7) },
        { "buddyLiveProvider", Type::String, QVariant(QStringLiteral("anthropic")) },
        { "buddyLiveModel", Type::String, QVariant(QStringLiteral("claude-haiku-5-5")) },
        { "buddyLiveTemperature", Type::Real, QVariant(-1.0) },
        { "buddyLivePrompt", Type::String, QVariant(QStringLiteral("")) },
        { "buddyLiveEndpoint", Type::String, QVariant(QStringLiteral("")) },
        { "buddyLiveAskBuddy", Type::Bool, QVariant(true) },
        { "buddyLiveVoice", Type::String, QVariant(QStringLiteral("aura-2-thalia-en")) },
        { "buddyLiveSpeed", Type::Real, QVariant(1.0) },
        { "buddyLiveVolume", Type::Real, QVariant(1.0) },
        { "buddyLiveGreeting", Type::String, QVariant(QStringLiteral("I'm listening.")) },
        { "buddyLiveAudioMode", Type::String, QVariant(QStringLiteral("speakers")) },
        { "buddyLiveEchoCancel", Type::Bool, QVariant(true) },
        { "buddyLiveQuick", Type::Bool, QVariant(true) },
        { "buddyLiveThinkCue", Type::Bool, QVariant(true) },
        { "buddyLiveFollowUp", Type::Int, QVariant(30) },
        { "buddyPermissionMode", Type::String, QVariant(QStringLiteral("default")) },
        { "buddyWorkspaceRoot", Type::String, QVariant(QStringLiteral("")) },
        { "buddyRedactSecrets", Type::Bool, QVariant(true) },
        { "buddyKeepTraces", Type::Bool, QVariant(true) },
        { "buddyStreamReplies", Type::Bool, QVariant(true) },
        { "buddyShowThinking", Type::Bool, QVariant(true) },
        { "buddyMemEnabled", Type::Bool, QVariant(true) },
        { "buddyMemBackend", Type::String, QVariant(QStringLiteral("local")) },
        { "buddyMemUrl", Type::String, QVariant(QStringLiteral("")) },
        { "buddyMemUserId", Type::String, QVariant(QStringLiteral("noch")) },
        { "buddyMemLlmModel", Type::String, QVariant(QStringLiteral("")) },
        { "buddyMemEmbedModel", Type::String, QVariant(QStringLiteral("")) },
        { "buddyMemEmbedBase", Type::String, QVariant(QStringLiteral("")) },
        { "buddySfxEnabled", Type::Bool, QVariant(true) },
        { "buddySfxVolume", Type::Real, QVariant(0.75) },
        { "buddySfxStyle", Type::String, QVariant(QStringLiteral("warm")) },
};

} // namespace

class TstSettingsRegistry : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // One stateless bridge for the QML tests; the table never changes.
        // A singleton registered with qmlRegisterSingletonInstance is tied
        // to a single engine, so all three QML tests share this one.
        qmlRegisterSingletonInstance("Noch.Test", 1, 0, "SettingsSchema", &m_registry);
        m_engine = new QQmlEngine(this);
    }

    // ---- the table -------------------------------------------------------

    void tableHasOneEntryPerKey()
    {
        const auto &entries = noch::settingschema::entries();
        QCOMPARE(entries.size(), 114);

        QSet<QString> seen;
        for (const Entry &e : entries) {
            const QString key = QString::fromLatin1(e.key);
            QVERIFY(!key.isEmpty());
            QCOMPARE(key.count(QLatin1Char('/')), 1); // exactly "group/name"
            QVERIFY(!seen.contains(key));
            seen.insert(key);
            QVERIFY(e.defaultValue.isValid());
        }
    }

    void goldenDefaultsMatchPreRefactorQml()
    {
        for (const Golden &g : golden) {
            const Entry *e = noch::settingschema::find(QStringLiteral("ui/") + QString::fromLatin1(g.name));
            QVERIFY2(e != nullptr, g.name);
            // Enum is a String with a fixed value set: the QML declaration
            // says "string" for every enum key, the registry refines it.
            QVERIFY2(e->type == g.type || (e->type == Type::Enum && g.type == Type::String), g.name);
            QCOMPARE(e->defaultValue, g.value);
        }
        // The ui group holds exactly the golden set plus the six legacy
        // secret keys (metadata only, values in the vault) plus the one
        // post-registry feature key (Deepgram's think-override flag) —
        // nothing else.
        QSet<QString> goldenNames;
        for (const Golden &g : golden)
            goldenNames.insert(QString::fromLatin1(g.name));
        QStringList extra;
        for (const Entry &e : noch::settingschema::entries()) {
            const QString key = QString::fromLatin1(e.key);
            if (key.startsWith(QLatin1String("ui/")) && !goldenNames.contains(key.mid(3)))
                extra << key.mid(3);
        }
        QCOMPARE(extra, QStringList({ "buddyLiveThinkOverride",
                                      "buddyApiKey", "buddyDgKey", "buddyTtsGoogleKey",
                                      "buddyLiveEndpointKey", "buddyMemKey", "buddyMemEmbedKey" }));
        // The post-registry key is pinned too: Bool, default off.
        const Entry *think = noch::settingschema::find(QStringLiteral("ui/buddyLiveThinkOverride"));
        QVERIFY(think != nullptr);
        QCOMPARE(think->type, Type::Bool);
        QCOMPARE(think->defaultValue, QVariant(false));
    }

    void rangesAndEnumValuesAreSane()
    {
        for (const Entry &e : noch::settingschema::entries()) {
            if (e.type == Type::Enum) {
                QVERIFY2(!e.values.isEmpty(), e.key);
                QVERIFY2(e.values.contains(e.defaultValue.toString()), e.key);
            }
            if (e.hasRange) {
                const double d = e.defaultValue.toDouble();
                QVERIFY2(e.min <= d && d <= e.max, e.key);
            }
        }
    }

    void secretFlagsAgreeWithSecretKeys()
    {
        // Both directions: the registry must flag exactly the keys
        // SecretKeys.h calls secret — no more, no fewer.
        QStringList secretKeys = noch::secretkeys::iniKeys();
        secretKeys << noch::secretkeys::providerConfigsIniKey();
        for (const QString &key : secretKeys) {
            const Entry *e = noch::settingschema::find(key);
            QVERIFY2(e != nullptr, qPrintable(key));
            QVERIFY2(e->secret, qPrintable(key));
            QVERIFY(noch::settingschema::isSecret(key));
        }
        for (const Entry &e : noch::settingschema::entries()) {
            if (e.secret)
                QVERIFY2(secretKeys.contains(QString::fromLatin1(e.key)), e.key);
        }
        // Secret entries are metadata only: the registry never hands a
        // value out for a vault key through the settings path.
        QVERIFY(m_registry.defaultFor(QStringLiteral("buddyApiKey")).isValid());
    }

    // ---- the QML bridge --------------------------------------------------

    void bridgeTypedAccessors()
    {
        QCOMPARE(m_registry.defaultInt(QStringLiteral("buddyMaxSteps")), 40);
        QCOMPARE(m_registry.defaultInt(QStringLiteral("ui/buddyMaxSteps")), 40);
        QCOMPARE(m_registry.defaultReal(QStringLiteral("sizeScale")), 1.0);
        QCOMPARE(m_registry.defaultBool(QStringLiteral("buddyEnabled")), true);
        QCOMPARE(m_registry.defaultString(QStringLiteral("buddyModel")), QStringLiteral("gpt-4o-mini"));
        QCOMPARE(m_registry.defaultString(QStringLiteral("buddyRingStyle")), QStringLiteral("neon"));
        // Unknown keys and type mismatches give the zero value, never a crash.
        QCOMPARE(m_registry.defaultInt(QStringLiteral("noSuchKey")), 0);
        QCOMPARE(m_registry.defaultReal(QStringLiteral("buddyEnabled")), 0.0);
        QCOMPARE(m_registry.defaultString(QStringLiteral("buddyMaxSteps")), QString());
        QVERIFY(!m_registry.defaultFor(QStringLiteral("noSuchKey")).isValid());
    }

    void bridgeDescribeAndAll()
    {
        const QVariantMap d = m_registry.describe(QStringLiteral("buddyMaxSteps"));
        QCOMPARE(d.value(QStringLiteral("key")).toString(), QStringLiteral("ui/buddyMaxSteps"));
        QCOMPARE(d.value(QStringLiteral("type")).toString(), QStringLiteral("int"));
        QCOMPARE(d.value(QStringLiteral("default")).toInt(), 40);
        QCOMPARE(d.value(QStringLiteral("hasRange")).toBool(), false);
        QCOMPARE(d.value(QStringLiteral("secret")).toBool(), false);
        QCOMPARE(d.value(QStringLiteral("values")).toStringList(),
                 QStringList({ "20", "40", "70", "100" }));

        QVERIFY(m_registry.describe(QStringLiteral("noSuchKey")).isEmpty());
        QCOMPARE(m_registry.all().size(), 114);
        QCOMPARE(m_registry.keys().size(), 114);
        QVERIFY(m_registry.isSecret(QStringLiteral("buddyDgKey")));
        QVERIFY(!m_registry.isSecret(QStringLiteral("buddyModel")));
    }

    // ---- the QML projection ----------------------------------------------

    // A Settings object whose property defaults come from SettingsSchema
    // calls, exactly like Main.qml's cfg does after the refactor.
    QString settingsDoc(const QString &iniPath) const
    {
        return QStringLiteral(
                   "import QtCore\n"
                   "import Noch.Test 1.0\n"
                   "Settings {\n"
                   "  location: \"%1\"\n"
                   "  category: \"ui\"\n"
                   "  property int buddyMaxSteps: SettingsSchema.defaultInt(\"buddyMaxSteps\")\n"
                   "  property real sizeScale: SettingsSchema.defaultReal(\"sizeScale\")\n"
                   "  property bool buddyEnabled: SettingsSchema.defaultBool(\"buddyEnabled\")\n"
                   "  property string buddyModel: SettingsSchema.defaultString(\"buddyModel\")\n"
                   "}\n")
            .arg(QUrl::fromLocalFile(iniPath).toString());
    }

    QObject *loadSettings(QQmlEngine &engine, const QString &iniPath) const
    {
        QQmlComponent component(&engine);
        component.setData(settingsDoc(iniPath).toUtf8(), QUrl());
        if (component.isError()) {
            qWarning() << component.errors();
            return nullptr;
        }
        QObject *obj = component.create();
        if (component.isError())
            qWarning() << component.errors();
        return obj;
    }

    void qmlDefaultsComeFromTheRegistry()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));

        QObject *obj = loadSettings(*m_engine, ini);
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->property("buddyMaxSteps").toInt(), 40);
        QCOMPARE(obj->property("sizeScale").toDouble(), 1.0);
        QCOMPARE(obj->property("buddyEnabled").toBool(), true);
        QCOMPARE(obj->property("buddyModel").toString(), QStringLiteral("gpt-4o-mini"));
        delete obj;
    }

    void qmlStoredValuesWinOverRegistryDefaults()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));
        {
            // A user file from before: these must survive the refactor.
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(QStringLiteral("ui/buddyMaxSteps"), 7);
            settings.setValue(QStringLiteral("ui/sizeScale"), 1.33);
            settings.setValue(QStringLiteral("ui/buddyModel"), QStringLiteral("ollama-model"));
            settings.sync();
        }
        QObject *obj = loadSettings(*m_engine, ini);
        QVERIFY(obj != nullptr);
        QCOMPARE(obj->property("buddyMaxSteps").toInt(), 7);
        QCOMPARE(obj->property("sizeScale").toDouble(), 1.33);
        QCOMPARE(obj->property("buddyModel").toString(), QStringLiteral("ollama-model"));
        // An unseeded key still falls back to the registry default.
        QCOMPARE(obj->property("buddyEnabled").toBool(), true);
        delete obj;
    }

    void qmlChangesStillPersist()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ini = dir.filePath(QStringLiteral("noch.ini"));
        {
            QObject *obj = loadSettings(*m_engine, ini);
            QVERIFY(obj != nullptr);
            obj->setProperty("buddyMaxSteps", 55);
            obj->setProperty("buddyModel", QStringLiteral("changed-model"));
            delete obj; // Settings flushes on destruction
        }
        QSettings settings(ini, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("ui/buddyMaxSteps")).toInt(), 55);
        QCOMPARE(settings.value(QStringLiteral("ui/buddyModel")).toString(), QStringLiteral("changed-model"));
    }

    // ---- coverage: every QML reference names a registered key ------------

    void everyQmlSettingsReferenceIsRegistered()
    {
        const QDir qmlDir(QStringLiteral(NOCH_SOURCE_DIR) + QStringLiteral("/qml"));
        QVERIFY(qmlDir.exists());
        const QRegularExpression re(QStringLiteral(R"(\b(?:cfg|prefs|settingsStore)\.([A-Za-z][A-Za-z0-9]*))"));
        QDirIterator it(qmlDir.path(), { QStringLiteral("*.qml") }, QDir::Files,
                        QDirIterator::Subdirectories);
        QStringList missing;
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QString text = QString::fromUtf8(f.readAll());
            auto matchIt = re.globalMatch(text);
            while (matchIt.hasNext()) {
                const QString name = matchIt.next().captured(1);
                if (noch::settingschema::findUi(name) == nullptr)
                    missing << (f.fileName() + QLatin1Char(':') + name);
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(", "))));
    }

private:
    SettingsRegistry m_registry;
    QQmlEngine *m_engine = nullptr;
};

QTEST_MAIN(TstSettingsRegistry)
#include "tst_settingsregistry.moc"
