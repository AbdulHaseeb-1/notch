#include "SettingsRegistry.h"

#include "SettingsSchema.h"

namespace {

const noch::settingschema::Entry *resolve(const QString &name)
{
    if (name.isEmpty())
        return nullptr;
    if (name.contains(QLatin1Char('/')))
        return noch::settingschema::find(name);
    return noch::settingschema::findUi(name);
}

QString typeName(noch::settingschema::Type type)
{
    using Type = noch::settingschema::Type;
    switch (type) {
    case Type::Bool: return QStringLiteral("bool");
    case Type::Int: return QStringLiteral("int");
    case Type::Real: return QStringLiteral("real");
    case Type::String: return QStringLiteral("string");
    case Type::Enum: return QStringLiteral("enum");
    case Type::StringList: return QStringLiteral("stringlist");
    case Type::Bytes: return QStringLiteral("bytes");
    }
    return QStringLiteral("string");
}

QVariantMap describeEntry(const noch::settingschema::Entry &e)
{
    QVariantMap out;
    out.insert(QStringLiteral("key"), QString::fromLatin1(e.key));
    out.insert(QStringLiteral("type"), typeName(e.type));
    out.insert(QStringLiteral("default"), e.defaultValue);
    out.insert(QStringLiteral("min"), e.min);
    out.insert(QStringLiteral("max"), e.max);
    out.insert(QStringLiteral("hasRange"), e.hasRange);
    out.insert(QStringLiteral("secret"), e.secret);
    out.insert(QStringLiteral("values"), e.values);
    return out;
}

} // namespace

QVariant SettingsRegistry::defaultFor(const QString &name) const
{
    const auto *e = resolve(name);
    return e ? e->defaultValue : QVariant();
}

bool SettingsRegistry::defaultBool(const QString &name) const
{
    const auto *e = resolve(name);
    return e && e->type == noch::settingschema::Type::Bool ? e->defaultValue.toBool() : false;
}

int SettingsRegistry::defaultInt(const QString &name) const
{
    const auto *e = resolve(name);
    return e && e->type == noch::settingschema::Type::Int ? e->defaultValue.toInt() : 0;
}

double SettingsRegistry::defaultReal(const QString &name) const
{
    const auto *e = resolve(name);
    return e && e->type == noch::settingschema::Type::Real ? e->defaultValue.toDouble() : 0.0;
}

QString SettingsRegistry::defaultString(const QString &name) const
{
    const auto *e = resolve(name);
    return e && (e->type == noch::settingschema::Type::String || e->type == noch::settingschema::Type::Enum)
        ? e->defaultValue.toString()
        : QString();
}

QVariantMap SettingsRegistry::describe(const QString &name) const
{
    const auto *e = resolve(name);
    return e ? describeEntry(*e) : QVariantMap();
}

QVariantList SettingsRegistry::all() const
{
    QVariantList out;
    for (const auto &e : noch::settingschema::entries())
        out << describeEntry(e);
    return out;
}

QStringList SettingsRegistry::keys() const
{
    return noch::settingschema::keys();
}

bool SettingsRegistry::isSecret(const QString &name) const
{
    const auto *e = resolve(name);
    return e ? e->secret : noch::settingschema::isSecret(name);
}
