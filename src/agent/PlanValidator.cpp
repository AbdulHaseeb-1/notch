#include "PlanValidator.h"

#include <QJsonDocument>
#include <QRegularExpression>

PlanValidation validatePlan(const QJsonArray &plan,
                            const std::function<bool(const QString &)> &toolAvailable)
{
    PlanValidation result;
    if (plan.isEmpty()) {
        result.errors << QStringLiteral("Plan is empty.");
        return result;
    }
    if (plan.size() > 20) {
        result.errors << QStringLiteral("Plan has %1 steps; maximum is 20. Split the task.").arg(plan.size());
        return result;
    }

    int expectedStep = 1;
    for (int i = 0; i < plan.size(); ++i) {
        const QJsonValue v = plan.at(i);
        const QString where = QStringLiteral("Step %1").arg(i + 1);
        if (!v.isObject()) {
            result.errors << where + QStringLiteral(": must be an object.");
            continue;
        }
        const QJsonObject o = v.toObject();
        PlanStep step;

        // step number
        const int n = o.value(QStringLiteral("step")).toInt(-1);
        if (n != expectedStep) {
            result.errors << where + QStringLiteral(": 'step' should be %1, got %2.")
                                                 .arg(expectedStep).arg(n);
        }
        step.step = n;
        ++expectedStep;

        // tool name: must be a real tool in the registry
        step.tool = o.value(QStringLiteral("tool")).toString().trimmed();
        const ToolSchema *schema = toolSchema(step.tool);
        if (step.tool.isEmpty()) {
            result.errors << where + QStringLiteral(": missing 'tool'.");
        } else if (!schema) {
            result.errors << where + QStringLiteral(": unknown tool '%1'.").arg(step.tool);
        } else if (toolAvailable && !toolAvailable(step.tool)) {
            // M7: a plan naming a disabled/unloaded tool would fail every
            // step at the gate; reject it here so the model replans with
            // tools that can actually run.
            result.errors << where + QStringLiteral(": tool '%1' is not available in this run "
                                                    "(disabled or not loaded by the tool diet).").arg(step.tool);
        }

        // args_sketch: keys must exist in the tool's schema
        const QJsonValue sketchV = o.value(QStringLiteral("args_sketch"));
        if (!sketchV.isObject()) {
            result.errors << where + QStringLiteral(": 'args_sketch' must be an object.");
        } else {
            step.argsSketch = sketchV.toObject();
            if (schema) {
                const QStringList known = schema->knownArgs();
                for (auto it = step.argsSketch.constBegin(); it != step.argsSketch.constEnd(); ++it) {
                    if (!known.contains(it.key()))
                        result.errors << where + QStringLiteral(": unknown argument '%1' for tool '%2'.")
                                                             .arg(it.key(), step.tool);
                }
            }
        }

        // verify: must be a non-empty checkable condition
        step.verify = o.value(QStringLiteral("verify")).toString().trimmed();
        if (step.verify.isEmpty())
            result.errors << where + QStringLiteral(": 'verify' must describe how to check the step worked.");

        result.steps.append(step);
    }

    result.ok = result.errors.isEmpty();
    return result;
}

QJsonArray extractPlanBlock(const QString &text)
{
    // L4/L5: accepts ```plan followed by a newline OR inline JSON on the
    // same line (single-line blocks from small models), and ```json with a
    // top-level array. The old comment claimed a top-level "plan" key is
    // accepted — it never was; only arrays are.
    static const QRegularExpression planRx(
        QStringLiteral("```plan\\s*(?:\\n(.*?)|([^`\\n]*))\\n?```"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = planRx.match(text);
    QString jsonText;
    if (m.hasMatch()) {
        jsonText = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2);
    } else {
        static const QRegularExpression jsonRx(
            QStringLiteral("```json\\s*(?:\\n(.*?)|([^`\\n]*))\\n?```"),
            QRegularExpression::DotMatchesEverythingOption);
        m = jsonRx.match(text);
        if (!m.hasMatch())
            return QJsonArray();
        jsonText = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2);
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return QJsonArray();
    return doc.array();
}
