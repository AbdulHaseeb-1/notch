// P5: Plan-then-act protocol. A plan is a structured JSON array of steps:
//   [{ "step": 1, "tool": "bash", "args_sketch": { "command": "ls" },
//      "verify": "directory listing shown" }, ...]
// The model produces a plan for multi-step tasks; the harness validates it
// (real tools, real arg names, non-empty verify) before execution.
#pragma once

#include "ToolSchemas.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

struct PlanStep {
    int step = 0;
    QString tool;
    QJsonObject argsSketch;
    QString verify;
    // Runtime status, not part of the model-produced JSON.
    QString status = QStringLiteral("pending");  // pending|done|failed|skipped
};

struct PlanValidation {
    bool ok = false;
    QStringList errors;
    QList<PlanStep> steps;
};

// Validate a plan parsed from the model's ```plan fenced block.
// Checks: array of objects, positive step numbers in order, tool names that
// exist in the registry, args_sketch keys that exist in the tool's schema,
// non-empty verify strings. M7: when `toolAvailable` is given, steps using
// a tool that is disabled for the active agent or not loaded by the tool
// diet are rejected here instead of failing at the gate.
PlanValidation validatePlan(const QJsonArray &plan,
                            const std::function<bool(const QString &)> &toolAvailable = {});

// Extract a ```plan fenced JSON block from model text. Returns null array
// when no plan block is present.
QJsonArray extractPlanBlock(const QString &text);
