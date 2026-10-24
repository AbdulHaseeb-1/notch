// P3: Hook bus. Modelled on the Anthropic hooks system: named handlers
// subscribe to lifecycle events and answer allow/deny/ask/defer, optionally
// rewriting the tool input (PreToolUse) or the result (PostToolUse).
// Conflicts resolve deny > defer > ask > allow; rewrites chain in
// registration order. Existing harness pieces are reframed as hooks:
// AgentPolicy.evaluate is the PreToolUse policy hook, guardToolOutput the
// PostToolUse output-guard hook.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QList>
#include <functional>

enum class HookEvent { PreToolUse, PostToolUse };

// Strength order matters for resolution: Deny beats everything.
enum class HookDecision { Allow = 0, Ask = 1, Defer = 2, Deny = 3 };

struct HookContext {
    HookEvent event = HookEvent::PreToolUse;
    QString toolName;
    QJsonObject input;        // PreToolUse: current (possibly rewritten) input
    QString output;           // PostToolUse: tool output to guard/transform
    bool isError = false;     // PostToolUse
};

struct HookResult {
    HookDecision decision = HookDecision::Allow;
    QString rule;             // id of the deciding hook/rule (for spans)
    QString reason;           // human-readable explanation
    bool hasRewrite = false;  // PreToolUse: input replaced; PostToolUse: output replaced
    QJsonObject rewrittenInput;
    QString rewrittenOutput;

    static HookResult allow() { return HookResult{}; }
    static HookResult deny(const QString &rule, const QString &reason)
    {
        HookResult r; r.decision = HookDecision::Deny; r.rule = rule; r.reason = reason; return r;
    }
    static HookResult ask(const QString &rule, const QString &reason)
    {
        HookResult r; r.decision = HookDecision::Ask; r.rule = rule; r.reason = reason; return r;
    }
    static HookResult rewriteInput(const QJsonObject &input)
    {
        HookResult r; r.hasRewrite = true; r.rewrittenInput = input; return r;
    }
    static HookResult rewriteOutput(const QString &output)
    {
        HookResult r; r.hasRewrite = true; r.rewrittenOutput = output; return r;
    }
};

class HookBus {
public:
    using Handler = std::function<HookResult(const HookContext &)>;

    void addHook(HookEvent event, const QString &name, Handler handler);

    // Runs every hook for the event in registration order. Rewrites chain:
    // each hook sees the context with previous rewrites already applied.
    // The combined decision is the strongest any hook returned; the deciding
    // hook's rule/reason are kept for traces.
    HookResult run(HookEvent event, HookContext &ctx, QStringList *ranHooks = nullptr) const;

    int hookCount(HookEvent event) const;

private:
    struct Entry { HookEvent event; QString name; Handler handler; };
    QList<Entry> m_hooks;
};

// Human-readable names for traces/logs.
QString hookEventName(HookEvent event);
QString hookDecisionName(HookDecision decision);
