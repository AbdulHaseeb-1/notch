#include "BuddyProviderInternal.h"

using namespace noch::buddyprovider;

QString BuddyProvider::checkForPlan(const QString &text)
{
    const QJsonArray planArr = extractPlanBlock(text);
    if (planArr.isEmpty())
        return QString();

    // M7: validate availability for THIS run (agent profile + tool diet),
    // not just registry existence: a step may only name a tool that
    // buildToolsArray() offers the model in this run.
    const bool anthropic = m_provider == QLatin1String("anthropic");
    QStringList offered;
    for (const QJsonValue &tv : buildToolsArray()) {
        const QJsonObject t = tv.toObject();
        if (anthropic && t.value(QStringLiteral("type")).toString().startsWith(QLatin1String("computer_toolset_")))
            offered << QStringLiteral("computer");  // the computer toolset is one unnamed entry
        else if (anthropic)
            offered << t.value(QStringLiteral("name")).toString();
        else
            offered << t.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString();
    }
    const PlanValidation v = validatePlan(planArr, [&offered](const QString &tool) {
        return offered.contains(tool);
    });
    if (!v.ok) {
        addSpan(QJsonObject{ { QStringLiteral("type"), QStringLiteral("plan") },
                             { QStringLiteral("event"), QStringLiteral("invalid") },
                             { QStringLiteral("errors"), QJsonArray::fromStringList(v.errors) } });
        return QStringLiteral("\n[Plan rejected: %1 — please output a corrected ```plan block.]")
            .arg(v.errors.join(QStringLiteral("; ")));
    }

    // A new valid plan replaces any current one (this is also the replan path).
    m_plan = v.steps;
    m_planActive = true;
    m_planIndex = 0;
    m_consecutivePlanFailures = 0;
    addSpan(QJsonObject{ { QStringLiteral("type"), QStringLiteral("plan") },
                         { QStringLiteral("event"), QStringLiteral("accepted") },
                         { QStringLiteral("steps"), v.steps.size() },
                         { QStringLiteral("replans"), m_replanCount } });
    return QStringLiteral("\n[Plan accepted (%1 steps). Execute step 1 now: %2 — verify: %3]")
        .arg(v.steps.size())
        .arg(v.steps.first().tool, v.steps.first().verify);
}
