#pragma once

// P1: Single source of truth for tool schemas.
//
// The registry defines every tool once: its description, arguments (with
// types, descriptions, required flags), common misnamings (aliases), and
// scalar coercions. Two consumers share it:
//
//  * BuddyProviderPrompts.cpp builds the provider-facing JSON schemas from
//    it (instead of hand-writing parallel definitions).
//  * The repair pipeline (normalizeToolCall) validates against it and
//    produces field-level errors.
//
// Adding a tool means adding one entry here; both sides stay in sync.

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct ToolArgDef {
    QString name;
    QString type;          // "string", "integer", "number", "boolean", "array", "object"
    QString description;
    bool required = false;
    QStringList enumValues{}; // optional: allowed values
};

struct ToolSchema {
    QString name;
    QString description;
    QList<ToolArgDef> args;
    // Common misnamings the repair pipeline fixes: wrong -> correct.
    QMap<QString, QString> aliases;
    // "arg" -> "int" | "double" | "bool": stringified scalars are converted
    // when they parse cleanly, otherwise left alone.
    QMap<QString, QString> coercions;

    QStringList requiredArgs() const {
        QStringList out;
        for (const ToolArgDef &a : args)
            if (a.required) out << a.name;
        return out;
    }
    QStringList knownArgs() const {
        QStringList out;
        for (const ToolArgDef &a : args) out << a.name;
        return out;
    }
    const ToolArgDef *findArg(const QString &argName) const {
        for (const ToolArgDef &a : args)
            if (a.name == argName) return &a;
        return nullptr;
    }
};

// The full registry. Tools without required arguments accept empty input.
const QList<ToolSchema> &toolSchemas();

// Lookup by name; nullptr for unknown tools.
const ToolSchema *toolSchema(const QString &name);

// P1: Provider schemas in BuddyProviderPrompts.cpp pull their required lists
// from the registry so the validator and the advertised schema cannot drift.
class QJsonArray;
QJsonArray requiredArgsJson(const QString &toolName);
