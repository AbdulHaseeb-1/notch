// Shared path helpers for the AgentPolicy implementation files.
// This header is private to src/agent and is not installed or exposed to QML.
#pragma once

#include "AgentPolicy.h"

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace noch::agentpolicy {

using RX = QRegularExpression;
constexpr auto CI = QRegularExpression::CaseInsensitiveOption;

QString unquote(QString path);
QString expandVars(QString path);
QString lexical(const QString &raw, const QString &base);
QStringList pathKeys(const QString &raw, const QString &base);
bool under(const QString &path, const QString &dir);
// True when a shell word resolves without consulting the process cwd
// (drive-absolute, root-absolute, UNC, or home/env-var rooted).
bool isAbsoluteShellPath(const QString &w);

} // namespace noch::agentpolicy
