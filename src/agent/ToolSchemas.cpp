#include "ToolSchemas.h"

#include "ToolRegistry.h"

#include <QJsonArray>

// The schema table is a projection of the core-tool registry: the
// definitions live in ToolRegistry.cpp, one entry per tool.
const QList<ToolSchema> &toolSchemas()
{
    static const QList<ToolSchema> schemas = [] {
        QList<ToolSchema> out;
        for (const CoreToolDef &def : coreToolDefs())
            out << def.schema;
        return out;
    }();
    return schemas;
}

const ToolSchema *toolSchema(const QString &name)
{
    const CoreToolDef *def = coreToolDef(name);
    return def ? &def->schema : nullptr;
}

QJsonArray requiredArgsJson(const QString &toolName)
{
    QJsonArray out;
    const ToolSchema *s = toolSchema(toolName);
    if (s)
        for (const QString &r : s->requiredArgs())
            out << r;
    return out;
}
