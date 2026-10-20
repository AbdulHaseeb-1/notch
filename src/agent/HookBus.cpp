#include "HookBus.h"

void HookBus::addHook(HookEvent event, const QString &name, Handler handler)
{
    m_hooks.append({ event, name, std::move(handler) });
}

HookResult HookBus::run(HookEvent event, HookContext &ctx, QStringList *ranHooks) const
{
    HookResult combined;
    for (const Entry &e : m_hooks) {
        if (e.event != event)
            continue;
        if (ranHooks)
            ranHooks->append(e.name);
        // Fail closed (P9 direction): a throwing hook is a deny, not a hole.
        HookResult r;
        try {
            r = e.handler(ctx);
        } catch (...) {
            r = HookResult::deny(QStringLiteral("hook:exception"),
                                 QStringLiteral("Hook '%1' threw and was denied.").arg(e.name));
        }
        // Chain rewrites into the context for the next hook.
        if (r.hasRewrite) {
            if (event == HookEvent::PreToolUse && !r.rewrittenInput.isEmpty()) {
                ctx.input = r.rewrittenInput;
                combined.hasRewrite = true;
                combined.rewrittenInput = r.rewrittenInput;
            } else if (event == HookEvent::PostToolUse) {
                ctx.output = r.rewrittenOutput;
                combined.hasRewrite = true;
                combined.rewrittenOutput = r.rewrittenOutput;
            }
        }
        // Strongest decision wins; keep its rule/reason.
        if (static_cast<int>(r.decision) > static_cast<int>(combined.decision)) {
            combined.decision = r.decision;
            combined.rule = r.rule;
            combined.reason = r.reason;
        }
        // A deny is final: later hooks cannot soften it, and there is no
        // point spending more work — but rewrites from earlier hooks are
        // already in the context, which is fine since nothing will run.
        if (combined.decision == HookDecision::Deny)
            break;
    }
    return combined;
}

int HookBus::hookCount(HookEvent event) const
{
    int n = 0;
    for (const Entry &e : m_hooks)
        if (e.event == event)
            ++n;
    return n;
}

QString hookEventName(HookEvent event)
{
    switch (event) {
    case HookEvent::PreToolUse: return QStringLiteral("PreToolUse");
    case HookEvent::PostToolUse: return QStringLiteral("PostToolUse");
    case HookEvent::Stop: return QStringLiteral("Stop");
    case HookEvent::SessionStart: return QStringLiteral("SessionStart");
    case HookEvent::PreCompact: return QStringLiteral("PreCompact");
    }
    return QStringLiteral("?");
}

QString hookDecisionName(HookDecision decision)
{
    switch (decision) {
    case HookDecision::Allow: return QStringLiteral("allow");
    case HookDecision::Ask: return QStringLiteral("ask");
    case HookDecision::Defer: return QStringLiteral("defer");
    case HookDecision::Deny: return QStringLiteral("deny");
    }
    return QStringLiteral("?");
}
