#pragma once

// P2: the core-tool registry — every built-in tool defined exactly once.
//
// Connector tools already worked this way (connectors::ToolDef: one
// definition flows to schemas, policy and UI). Core tools were hand-wired
// in five places instead: the schema table, a 13-branch dispatch chain in
// BuddyProviderRun.cpp, per-tool policy branches in AgentPolicyEvaluate,
// per-tool summary/badge/icon functions in BuddyProviderTools.cpp, and
// assorted name checks (output fencing, the path-normalize hook). This
// registry is the single definition those consumers now share:
//
//   schema        name, description, arguments, aliases, coercions — the
//                 ToolSchema the repair pipeline validates against
//                 (ToolSchemas.h's API is a view over this registry).
//   policy        which evaluator AgentPolicy runs for the tool.
//   access        coarse Read / Write / Execute classification, mirroring
//                 connectors::Access for core tools.
//   diet          Core tools are offered with their schema up front;
//                 Deferred tools would be loaded on demand. (This tree
//                 has no deferred core tools — every enabled tool is
//                 sent; the field is where a future diet would live.)
//   outputTrust   how BuddyProvider treats the tool's output: web tools
//                 return External content (fenced as untrusted), the
//                 question tool returns the user's own words (never
//                 fenced), everything else is Normal.
//   workspacePathArg  the argument the path-normalize hook resolves
//                 against the workspace before the gate judges the call
//                 ("" when the tool has none).
//   icon / badge  step icon and badge; the *For functions override them
//                 when the value depends on the arguments.
//   summarize     one-line summary of a call for the step list.
//   detail        the approval card's detail text; null = compact JSON.
//
// Handlers stay with BuddyProvider (they are its member functions); its
// dispatch table binds one handler per registry entry, and a test asserts
// the two never drift apart.

#include "ToolSchemas.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

// Which evaluator AgentPolicy::evaluate runs for a core tool. The deep
// per-tool logic (shell analysis, computer actions, editor paths, ...)
// stays in AgentPolicy; this kind is the dispatch key that replaces the
// per-tool name branches.
enum class ToolPolicyKind {
    Shell,          // bash: evaluateShell
    Computer,       // computer: action allowlist, screen-taint rules
    Editor,         // str_replace_editor: path-based read/edit rules
    WebFetch,       // web_fetch: URL checks, then allow "network-read"
    WebSearch,      // web_search: URL checks, then allow "safe-tool"
    CodeSearch,     // glob_grep: private/credential path checks on the root
    Music,          // music: per-action rules
    AllowReadOnly,  // share_screenshot: always allow, rule "read-only"
    AllowSafeTool,  // sandboxed / read-only tools: allow, rule "safe-tool"
};

// Coarse classification, mirroring connectors::Access plus Execute for
// the shell: Read never changes state outside the conversation, Write
// changes files/settings/playback, Execute runs arbitrary commands.
enum class ToolAccess { Read, Write, Execute };

// Whether the tool's schema is part of the up-front offer (Core) or
// loaded on demand (Deferred).
enum class ToolDiet { Core, Deferred };

// How a tool's output is treated by the output guard.
enum class ToolOutputTrust {
    Normal,     // Noch's own output; fenced only if it looks injected
    UserOwned,  // the user's own words (their answers): never fenced
    External,   // content from outside the machine: always fenced
};

// A step summary / detail / icon / badge computed from the call's input.
using ToolTextFn = std::function<QString(const QJsonObject &)>;

struct CoreToolDef {
    ToolSchema schema;
    ToolPolicyKind policy;
    ToolAccess access = ToolAccess::Read;
    ToolDiet diet = ToolDiet::Core;
    ToolOutputTrust outputTrust = ToolOutputTrust::Normal;
    QString workspacePathArg;
    QString icon;
    QString badge;
    ToolTextFn iconFor;    // overrides `icon` when set
    ToolTextFn badgeFor;   // overrides `badge` when set
    ToolTextFn summarize;  // always set for core tools
    ToolTextFn detail;     // null = the caller falls back to compact JSON

    const QString &name() const { return schema.name; }
};

// Every core tool, in the registry's canonical order (the order
// toolSchemas() has always returned).
const QList<CoreToolDef> &coreToolDefs();

// Lookup by name; nullptr for unknown (e.g. connector) tools.
const CoreToolDef *coreToolDef(const QString &name);
bool isCoreTool(const QString &name);

// The names of the tools in one diet class, in registry order.
QStringList toolNamesForDiet(ToolDiet diet);
