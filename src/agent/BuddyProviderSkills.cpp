#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

// Agent Skills (see Skills.h): the settings, the `skill` tool and /<skill-name>.
// The list in the system prompt is read once per run, like the instruction files,
// so an edit mid-run cannot spoil the prompt cache.

namespace {

QStringList splitNames(const QString &names)
{
    QStringList out;
    for (const QString &n : names.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString t = n.trimmed().toLower();
        if (!t.isEmpty() && !out.contains(t))
            out << t;
    }
    return out;
}

QString sourceLabel(const skills::Skill &s)
{
    return s.source == QLatin1String("builtin") ? QObject::tr("built in")
         : s.source == QLatin1String("user") ? QObject::tr("yours")
         : QObject::tr("this workspace");
}

} // namespace

void BuddyProvider::setAllowSkills(bool on)
{
    if (m_allowSkills == on)
        return;
    m_allowSkills = on;
    emit agentConfigChanged();
    emit skillsChanged();
}

void BuddyProvider::setDisabledSkills(const QString &names)
{
    const QString normalised = splitNames(names).join(QLatin1Char(','));
    if (m_disabledSkills == normalised)
        return;
    m_disabledSkills = normalised;
    emit agentConfigChanged();
    emit skillsChanged();
}

skills::Catalog BuddyProvider::loadSkillCatalog() const
{
    return skills::loadSkills(effectiveWorkspace(), userDataDir(), skills::builtinSkillsDir(), splitNames(m_disabledSkills));
}

void BuddyProvider::refreshSkillsForRun()
{
    m_skills = m_allowSkills ? loadSkillCatalog() : skills::Catalog();
    m_skillsSection = skills::systemPromptSection(m_skills);
    if (!m_skills.skills.isEmpty() || !m_skills.notes.isEmpty()) {
        QJsonArray names;
        for (const skills::Skill &s : m_skills.listed())
            names.append(s.name);
        addSpan(QJsonObject{ { "type", "skills" }, { "listed", names }, { "skipped", int(m_skills.notes.size()) },
                             { "chars", m_skillsSection.size() } });
    }
}

QVariantList BuddyProvider::skillList() const
{
    QVariantList out;
    for (const skills::Skill &s : loadSkillCatalog().skills)
        out.append(QVariantMap{ { QStringLiteral("name"), s.name }, { QStringLiteral("description"), s.description },
                                { QStringLiteral("source"), s.source }, { QStringLiteral("sourceLabel"), sourceLabel(s) },
                                { QStringLiteral("enabled"), s.enabled }, { QStringLiteral("modelInvocable"), s.modelInvocable },
                                { QStringLiteral("userInvocable"), s.userInvocable },
                                { QStringLiteral("argumentHint"), s.argumentHint }, { QStringLiteral("overrides"), s.overrides },
                                { QStringLiteral("path"), QDir::toNativeSeparators(s.file) } });
    return out;
}

QStringList BuddyProvider::skillNotes() const
{
    return loadSkillCatalog().notes;
}

QString BuddyProvider::ensureSkillsFolder() const
{
    const QString dir = skills::userSkillsDir(userDataDir());
    QDir().mkpath(dir);
    return QDir::toNativeSeparators(dir);
}

QString BuddyProvider::skillsReport() const
{
    const skills::Catalog cat = loadSkillCatalog();
    QStringList lines;
    lines << tr("**Skills** — ways of working Buddy loads when a task matches.");
    if (!m_allowSkills)
        lines << tr("Skills are switched off (Settings > Agent & tools > Skills).");
    if (cat.skills.isEmpty())
        lines << tr("No skills found.");
    for (const skills::Skill &s : cat.skills) {
        QString tail = QStringLiteral(" (%1)").arg(sourceLabel(s));
        if (!s.enabled)
            tail += tr(" — off");
        else if (!s.userInvocable)
            tail += tr(" — Buddy only");
        else if (!s.modelInvocable)
            tail += tr(" — you only");
        lines << QStringLiteral("- `/%1%2` — %3%4").arg(s.name, s.argumentHint.isEmpty() ? QString() : QLatin1Char(' ') + s.argumentHint,
                                                        s.description.left(140), tail);
    }
    for (const QString &n : cat.notes)
        lines << tr("- Skipped: %1").arg(n);
    lines << QString();
    lines << tr("Your own skills are folders with a `SKILL.md` in `%1`, or in `.noch/skills` of the workspace (`.claude/skills` works too). "
                "Ask Buddy to “write a skill that …” to get one made.")
                 .arg(QDir::toNativeSeparators(skills::userSkillsDir(userDataDir())));
    return lines.join(QLatin1Char('\n'));
}

QVariantMap BuddyProvider::resolveSkillSlash(const QString &name, const QString &args) const
{
    const skills::Catalog cat = loadSkillCatalog();
    const skills::Skill *s = cat.find(name);
    if (!s || !s->userInvocable)
        return QVariantMap();
    if (!m_allowSkills || !s->enabled)
        return QVariantMap{ { QStringLiteral("kind"), QStringLiteral("unknown") }, { QStringLiteral("name"), name },
                            { QStringLiteral("suggestion"), QString() },
                            { QStringLiteral("message"), tr("The skill “%1” is switched off. Turn it on in Settings > Agent & tools > Skills.").arg(name) } };
    QString prompt;
    if (s->modelInvocable && hasAnyToolEnabled()) {
        // Buddy loads it with the skill tool, so the chat shows a short request, not the whole file.
        prompt = tr("Use the “%1” skill for this: load it with the skill tool first, then follow it.").arg(s->name);
        prompt += args.isEmpty() ? tr("\n\nNo details were given; if something essential is missing, ask.")
                                 : QStringLiteral("\n\n") + args;
    } else {
        const QString body = skills::skillBody(*s, args);
        if (body.isEmpty())
            return QVariantMap{ { QStringLiteral("kind"), QStringLiteral("unknown") }, { QStringLiteral("name"), name },
                                { QStringLiteral("suggestion"), QString() },
                                { QStringLiteral("message"), tr("The skill “%1” could not be read.").arg(name) } };
        prompt = tr("Follow the “%1” skill below for this request.\n\n%2").arg(s->name, body);
    }
    return QVariantMap{ { QStringLiteral("kind"), QStringLiteral("prompt") }, { QStringLiteral("name"), s->name },
                        { QStringLiteral("args"), args }, { QStringLiteral("prompt"), prompt },
                        { QStringLiteral("source"), QStringLiteral("skill") }, { QStringLiteral("description"), s->description } };
}

QVariantList BuddyProvider::skillSlashCommands(const QString &prefix, const QStringList &taken) const
{
    QVariantList out;
    if (!m_allowSkills)
        return out;
    for (const skills::Skill &s : loadSkillCatalog().skills) {
        if (!s.enabled || !s.userInvocable || taken.contains(s.name))
            continue;
        if (!prefix.isEmpty() && !s.name.startsWith(prefix))
            continue;
        out.append(QVariantMap{ { QStringLiteral("name"), s.name }, { QStringLiteral("description"), s.description.left(120) },
                                { QStringLiteral("argumentHint"), s.argumentHint }, { QStringLiteral("source"), QStringLiteral("skill") } });
    }
    return out;
}

// The `skill` tool: the SKILL.md body (and its file list), or one of its bundled
// files. It reads only inside the skill's own folder, and what it returns is
// guidance: it grants nothing, every tool call it leads to is checked on its own.
void BuddyProvider::executeSkillTool(const PendingToolCall &call)
{
    if (!m_allowSkills) {
        finishCurrentToolCall(call, QStringLiteral("Error: skills are switched off in Settings > Agent & tools."), QString(), true);
        return;
    }
    const QString name = call.input.value(QStringLiteral("name")).toString().trimmed();
    const skills::Skill *s = m_skills.find(name);
    if (!s || !s->enabled || !s->modelInvocable) {
        QStringList names;
        for (const skills::Skill &k : m_skills.listed())
            names << k.name;
        finishCurrentToolCall(call, QStringLiteral("Error: there is no skill “%1” you can load. Available: %2.")
                                        .arg(name.left(60), names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "))),
                              QString(), true);
        return;
    }
    const QString file = call.input.value(QStringLiteral("file")).toString().trimmed();
    QString out;
    if (file.isEmpty()) {
        out = skills::toolResult(*s);
        if (out.isEmpty()) {
            finishCurrentToolCall(call, QStringLiteral("Error: the skill “%1” could not be read.").arg(s->name), QString(), true);
            return;
        }
        addSpan(QJsonObject{ { "type", "skill" }, { "name", s->name }, { "source", s->source }, { "chars", out.size() } });
    } else {
        QString error;
        out = skills::readBundledFile(*s, file, &error);
        if (out.isEmpty() && !error.isEmpty()) {
            finishCurrentToolCall(call, QStringLiteral("Error: ") + error, QString(), true);
            return;
        }
    }
    finishCurrentToolCall(call, out, QString(), false);
}
