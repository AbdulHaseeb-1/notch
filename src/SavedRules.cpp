#include "SavedRules.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

QString idFor(const AgentPolicy::SavedRule &r)
{
    const QString key = r.tool + QLatin1Char('\n') + r.kind + QLatin1Char('\n') + r.pattern.toLower()
                      + QLatin1Char('\n') + r.scope.toLower();
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

} // namespace

SavedRules::SavedRules(const QString &path)
{
    if (!path.isEmpty())
        setPath(path);
}

void SavedRules::setPath(const QString &path)
{
    m_path = path;
    load();
}

bool SavedRules::acceptable(const AgentPolicy::SavedRule &r)
{
    if (!r.valid())
        return false;
    if (r.tool == QLatin1String("bash")) {
        if ((r.kind != QLatin1String("exact") && r.kind != QLatin1String("prefix")) || r.scope.isEmpty())
            return false;
        // The stored command must still be free of shell syntax and unchanged by tidying.
        return AgentPolicy::normalizeCommand(r.pattern) == r.pattern;
    }
    if (r.tool == QLatin1String("str_replace_editor"))
        return r.kind == QLatin1String("dir") && !r.pattern.endsWith(QLatin1Char('*'));
    return false;
}

void SavedRules::load()
{
    m_rules.clear();
    if (m_path.isEmpty())
        return;
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject doc = QJsonDocument::fromJson(f.readAll()).object();
    for (const QJsonValue &v : doc.value(QStringLiteral("rules")).toArray()) {
        const QJsonObject o = v.toObject();
        AgentPolicy::SavedRule r;
        r.tool = o.value(QStringLiteral("tool")).toString();
        r.kind = o.value(QStringLiteral("kind")).toString();
        r.pattern = o.value(QStringLiteral("pattern")).toString();
        r.scope = o.value(QStringLiteral("scope")).toString();
        r.label = o.value(QStringLiteral("label")).toString();
        r.createdAt = qint64(o.value(QStringLiteral("createdAt")).toDouble());
        if (!acceptable(r))
            continue;
        r.id = idFor(r);
        bool dup = false;
        for (const AgentPolicy::SavedRule &e : m_rules)
            dup = dup || e.id == r.id;
        if (!dup && m_rules.size() < kMaxRules)
            m_rules.append(r);
    }
}

bool SavedRules::save() const
{
    if (m_path.isEmpty())
        return false;
    QJsonArray arr;
    for (const AgentPolicy::SavedRule &r : m_rules) {
        arr.append(QJsonObject{ { "tool", r.tool }, { "kind", r.kind }, { "pattern", r.pattern },
                                { "scope", r.scope }, { "label", r.label }, { "createdAt", double(r.createdAt) } });
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{ { "version", 1 }, { "rules", arr } }).toJson(QJsonDocument::Indented));
    return f.commit();
}

bool SavedRules::add(AgentPolicy::SavedRule rule)
{
    if (!acceptable(rule) || m_rules.size() >= kMaxRules)
        return false;
    rule.id = idFor(rule);
    for (const AgentPolicy::SavedRule &e : m_rules)
        if (e.id == rule.id)
            return false;
    rule.createdAt = QDateTime::currentMSecsSinceEpoch();
    m_rules.append(rule);
    if (!save()) {
        m_rules.removeLast();
        return false;
    }
    return true;
}

bool SavedRules::contains(const AgentPolicy::SavedRule &rule) const
{
    const QString id = idFor(rule);
    for (const AgentPolicy::SavedRule &e : m_rules)
        if (e.id == id)
            return true;
    return false;
}

bool SavedRules::remove(const QString &id)
{
    for (int i = 0; i < m_rules.size(); ++i) {
        if (m_rules.at(i).id == id) {
            const AgentPolicy::SavedRule gone = m_rules.takeAt(i);
            if (!save()) {
                m_rules.insert(i, gone);
                return false;
            }
            return true;
        }
    }
    return false;
}

void SavedRules::clear()
{
    if (m_rules.isEmpty())
        return;
    const QList<AgentPolicy::SavedRule> old = m_rules;
    m_rules.clear();
    if (!save())
        m_rules = old;
}

QVariantList SavedRules::toVariant() const
{
    QVariantList out;
    for (const AgentPolicy::SavedRule &r : m_rules) {
        out.append(QVariantMap{ { QStringLiteral("id"), r.id }, { QStringLiteral("tool"), r.tool },
                                { QStringLiteral("kind"), r.kind }, { QStringLiteral("pattern"), r.pattern },
                                { QStringLiteral("scope"), r.scope }, { QStringLiteral("label"), r.label },
                                { QStringLiteral("createdAt"), double(r.createdAt) } });
    }
    return out;
}
