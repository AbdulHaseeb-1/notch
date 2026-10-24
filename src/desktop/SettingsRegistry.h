// SettingsRegistry: the QML-facing door to the settings schema.
//
// Main.qml's Settings object used to declare every default inline; now it
// reads them from here (`property real sizeScale:
// SettingsSchema.defaultReal("sizeScale")`), and settings pages can ask for
// a key's metadata (type, range, allowed values, secret flag) instead of
// re-deriving it. The data itself lives in src/common/SettingsSchema.h —
// this class is a thin adapter so QML can reach it, following the
// AppSecrets/Secrets pattern (see QmlSingletons.h, element SettingsSchema).
//
// The object is stateless: the table is a compile-time constant, so the
// defaults QML reads never change at runtime and an initializer binding
// never re-evaluates over a value the user (or a stored setting) set.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class SettingsRegistry : public QObject
{
    Q_OBJECT

public:
    explicit SettingsRegistry(QObject *parent = nullptr) : QObject(parent) {}

    // The fresh-install default for a key, as a QVariant. `name` is either
    // a full settings key ("ui/buddyMaxSteps") or a QML property name
    // ("buddyMaxSteps", resolved to "ui/buddyMaxSteps"). Invalid QVariant
    // for an unknown key.
    Q_INVOKABLE QVariant defaultFor(const QString &name) const;
    // Typed reads for QML property initializers; unknown keys (or a type
    // mismatch) give the zero value of the requested type.
    Q_INVOKABLE bool defaultBool(const QString &name) const;
    Q_INVOKABLE int defaultInt(const QString &name) const;
    Q_INVOKABLE double defaultReal(const QString &name) const;
    Q_INVOKABLE QString defaultString(const QString &name) const;

    // Full metadata for one key: {key, type, default, min, max, hasRange,
    // secret, values}. `type` is one of "bool", "int", "real", "string",
    // "enum", "stringlist", "bytes". Empty map for an unknown key.
    Q_INVOKABLE QVariantMap describe(const QString &name) const;
    // Every entry as describe() maps, in table order.
    Q_INVOKABLE QVariantList all() const;
    // Every registered full key, in table order.
    Q_INVOKABLE QStringList keys() const;
    // True when the key's value is a credential (metadata only; the value
    // lives in the SecretStore). Agrees with SecretKeys.h by construction.
    Q_INVOKABLE bool isSecret(const QString &name) const;
};
