// The core-tool registry (P2): every built-in tool is defined once in
// ToolRegistry.cpp, and schemas, policy dispatch, summaries and the tool
// diet are all views over that one table. These tests pin the table's
// contents — above all the tool schemas, byte for byte, via a canonical
// dump compared against a golden captured from the pre-registry
// ToolSchemas table — so the refactor (and any future edit) cannot drift
// what the model is offered or how a call is judged.
#include <QtTest>

#include "AgentPolicy.h"
#include "ToolRegistry.h"

namespace {

// Canonical, deterministic rendering of the validation schemas: registry
// order, argument order, aliases/coercions in QMap (sorted) order.
QString canonicalSchemaDump()
{
    QStringList lines;
    for (const ToolSchema &s : toolSchemas()) {
        lines << QStringLiteral("TOOL %1").arg(s.name);
        lines << QStringLiteral("DESC %1").arg(s.description);
        for (const ToolArgDef &a : s.args) {
            lines << QStringLiteral("ARG %1|%2|%3|%4|%5")
                         .arg(a.name, a.type, a.required ? QStringLiteral("1") : QStringLiteral("0"),
                              a.enumValues.join(QLatin1Char(',')), a.description);
        }
        for (auto it = s.aliases.constBegin(); it != s.aliases.constEnd(); ++it)
            lines << QStringLiteral("ALIAS %1->%2").arg(it.key(), it.value());
        for (auto it = s.coercions.constBegin(); it != s.coercions.constEnd(); ++it)
            lines << QStringLiteral("COERCE %1=%2").arg(it.key(), it.value());
        lines << QStringLiteral("END");
    }
    return lines.join(QLatin1Char('\n'));
}

// The golden: generated from the schema table exactly as it existed
// before the registry move (see canonicalSchemaDump above).
const char kSchemaGolden[] = R"GOLDEN(TOOL bash
DESC Run a PowerShell command on the Windows desktop and return stdout/stderr.
ARG command|string|1||PowerShell command line to run on Windows.
ARG working_dir|string|0||Optional working directory path.
ARG timeout_seconds|integer|0||Optional time limit in seconds (default 30, max 300) for long jobs.
ALIAS cmd->command
ALIAS cwd->working_dir
ALIAS dir->working_dir
ALIAS timeout->timeout_seconds
ALIAS workingdir->working_dir
COERCE timeout_seconds=int
END
TOOL computer
DESC See the screen and control the mouse and keyboard.
ARG action|string|1||Action to perform (screenshot, click, type, key, etc.).
ARG coordinate|array|0||Target [x, y] in screenshot space.
ARG start_coordinate|array|0||Drag start [x, y].
ARG text|string|0||Text to type, key combo, or modifiers.
ARG scroll_direction|string|0||up, down, left, or right.
ARG scroll_amount|integer|0||Scroll clicks.
ARG duration|number|0||Hold duration in seconds.
ARG region|array|0||Zoom region [x, y, width, height].
ARG display|integer|0||Display index.
ALIAS coordinates->coordinate
ALIAS coords->coordinate
ALIAS xy->coordinate
COERCE duration=double
END
TOOL str_replace_editor
DESC View files/directories, create files, edit via str_replace/insert, or undo_edit.
ARG command|string|1||view, create, str_replace, insert, undo_edit.
ARG path|string|1||File or directory path.
ARG file_text|string|0||Content for create.
ARG old_str|string|0||Text to replace.
ARG new_str|string|0||Replacement text.
ARG insert_line|integer|0||Line number for insert (1-indexed).
ARG view_range|array|0||Optional [start_line, end_line] for view.
ALIAS cmd->command
ALIAS file->path
ALIAS filename->path
ALIAS filepath->path
COERCE insert_line=int
END
TOOL glob_grep
DESC Fast recursive file discovery and regex code/text search with line numbers.
ARG mode|string|1|glob,grep|'glob' for filenames, 'grep' for contents.
ARG pattern|string|1||Wildcard pattern for glob, regex for grep.
ARG path|string|0||Root directory or file to search in.
ARG file_glob|string|0||Filename filter for grep (e.g. '*.qml').
ARG max_results|integer|0||Maximum matches (default 25).
ALIAS query->pattern
ALIAS regex->pattern
END
TOOL memory
DESC View, create, update, delete, or rename persistent memory files across sessions.
ARG command|string|1||view, create, str_replace, insert, delete, rename.
ARG path|string|0||Memory file path.
ARG file_text|string|0||Content for create.
ARG old_str|string|0||Text to replace.
ARG new_str|string|0||Replacement text.
ARG insert_line|integer|0||Line number for insert.
ARG insert_text|string|0||Text to insert.
ARG old_path|string|0||Source for rename.
ARG new_path|string|0||Destination for rename.
ARG view_range|array|0||Optional [start_line, end_line].
ALIAS cmd->command
ALIAS file->path
ALIAS filepath->path
COERCE insert_line=int
END
TOOL web_search
DESC Search the live web for current information, news, or documentation.
ARG query|string|0||Search query.
ARG url|string|0||Fetch a specific URL instead.
ARG max_results|integer|0||Maximum results.
ALIAS q->query
END
TOOL web_fetch
DESC Retrieve a URL and extract readable text, hyperlinks, or metadata.
ARG url|string|1||URL to fetch.
ARG extract_mode|string|0||text, links, or metadata.
ALIAS link->url
ALIAS uri->url
END
TOOL todo_write
DESC Manage a live structured task checklist.
ARG action|string|1||Action (e.g. update).
ARG todos|array|0||Todo items.
ARG text|string|0||Text content.
ALIAS tasks->todos
ALIAS todos_list->todos
END
TOOL ask_user_question
DESC Ask the user multiple-choice questions when the decision is theirs.
ARG questions|array|1||Questions to ask.
END
TOOL skill
DESC Load a skill's instructions by name.
ARG name|string|1||The skill's name.
ARG file|string|0||A file that comes with the skill, relative to its folder.
ALIAS path->file
ALIAS skill->name
ALIAS skill_name->name
END
TOOL share_screenshot
DESC Take a screenshot and show it to the user in the chat.
ARG caption|string|0||Caption.
ARG display|integer|0||Display index.
END
TOOL music
DESC Control the music pill: play, pause, skip, volume, Noch's queue.
ARG action|string|1|status,play,pause,next,previous,volume,mute,unmute,queue,play_track,add,play_online|What to do.
ARG query|string|0||For play_track: words from a title or artist in the queue. For play_online: what to play from YouTube, as you would type it into its search box (artist and song, or a YouTube address).
ARG path|string|0||For add: a music file or a folder.
ARG volume|integer|0||For volume: 0 to 100.
ALIAS command->action
ALIAS file->path
ALIAS folder->path
ALIAS level->volume
ALIAS song->query
ALIAS track->query
COERCE volume=int
END
TOOL memory_search
DESC Search saved memories (facts and procedures) by relevance.
ARG query|string|1||What to recall, in words.
ARG top_k|integer|0||How many memories to return (default 5, max 10).
ALIAS q->query
END)GOLDEN";

QString resolvedIcon(const CoreToolDef &def, const QJsonObject &input)
{
    return def.iconFor ? def.iconFor(input) : def.icon;
}

QString resolvedBadge(const CoreToolDef &def, const QJsonObject &input)
{
    return def.badgeFor ? def.badgeFor(input) : def.badge;
}

QJsonObject obj(std::initializer_list<QPair<QString, QJsonValue>> items)
{
    QJsonObject o;
    for (const auto &p : items)
        o.insert(p.first, p.second);
    return o;
}

} // namespace

class TstToolRegistry : public QObject
{
    Q_OBJECT
private slots:
    void everyCoreToolRegisteredExactlyOnce()
    {
        const QList<CoreToolDef> &defs = coreToolDefs();
        QCOMPARE(defs.size(), toolSchemas().size());
        QSet<QString> seen;
        for (const CoreToolDef &d : defs) {
            QVERIFY2(!d.schema.name.isEmpty(), "a registry entry has no name");
            QVERIFY2(!seen.contains(d.schema.name), qPrintable("duplicate registration: " + d.schema.name));
            seen.insert(d.schema.name);
            // The ToolSchemas API is a view over the registry: same entry.
            QCOMPARE(coreToolDef(d.schema.name), &d);
            QCOMPARE(toolSchema(d.schema.name), &d.schema);
            QVERIFY(isCoreTool(d.schema.name));
            QVERIFY(d.summarize != nullptr);
        }
        // And every schema the API returns is a registry entry.
        for (const ToolSchema &s : toolSchemas())
            QVERIFY2(coreToolDef(s.name) != nullptr, qPrintable("schema without registry entry: " + s.name));
        QVERIFY(!isCoreTool(QStringLiteral("gmail_search_messages")));
        QCOMPARE(coreToolDef(QStringLiteral("no_such_tool")), nullptr);
    }

    void schemasMatchGolden()
    {
        QCOMPARE(canonicalSchemaDump(), QString::fromUtf8(kSchemaGolden));
    }

    void dietSets()
    {
        // This tree offers every enabled tool up front: the core set is
        // all of them, in registry order, and nothing is deferred. The
        // interaction tools are core — a past diet regression dropped
        // them, so pin them explicitly.
        QStringList all;
        for (const CoreToolDef &d : coreToolDefs())
            all << d.schema.name;
        QCOMPARE(toolNamesForDiet(ToolDiet::Core), all);
        QVERIFY(toolNamesForDiet(ToolDiet::Deferred).isEmpty());
        QVERIFY(all.contains(QStringLiteral("ask_user_question")));
        QVERIFY(all.contains(QStringLiteral("share_screenshot")));
        for (const CoreToolDef &d : coreToolDefs())
            QVERIFY(d.diet == ToolDiet::Core);
    }

    void policyKinds()
    {
        QVERIFY(coreToolDef(QStringLiteral("bash"))->policy == ToolPolicyKind::Shell);
        QVERIFY(coreToolDef(QStringLiteral("computer"))->policy == ToolPolicyKind::Computer);
        QVERIFY(coreToolDef(QStringLiteral("str_replace_editor"))->policy == ToolPolicyKind::Editor);
        QVERIFY(coreToolDef(QStringLiteral("web_fetch"))->policy == ToolPolicyKind::WebFetch);
        QVERIFY(coreToolDef(QStringLiteral("web_search"))->policy == ToolPolicyKind::WebSearch);
        QVERIFY(coreToolDef(QStringLiteral("glob_grep"))->policy == ToolPolicyKind::CodeSearch);
        QVERIFY(coreToolDef(QStringLiteral("music"))->policy == ToolPolicyKind::Music);
        QVERIFY(coreToolDef(QStringLiteral("share_screenshot"))->policy == ToolPolicyKind::AllowReadOnly);
        for (const char *n : { "memory", "memory_search", "todo_write", "ask_user_question", "skill" })
            QVERIFY(coreToolDef(QLatin1String(n))->policy == ToolPolicyKind::AllowSafeTool);
    }

    void accessClasses()
    {
        QVERIFY(coreToolDef(QStringLiteral("bash"))->access == ToolAccess::Execute);
        for (const char *n : { "computer", "str_replace_editor", "memory", "todo_write", "music" })
            QVERIFY(coreToolDef(QLatin1String(n))->access == ToolAccess::Write);
        for (const char *n : { "glob_grep", "memory_search", "web_search", "web_fetch",
                               "ask_user_question", "skill", "share_screenshot" })
            QVERIFY(coreToolDef(QLatin1String(n))->access == ToolAccess::Read);
    }

    void outputTrustAndPathArgs()
    {
        QVERIFY(coreToolDef(QStringLiteral("web_fetch"))->outputTrust == ToolOutputTrust::External);
        QVERIFY(coreToolDef(QStringLiteral("web_search"))->outputTrust == ToolOutputTrust::External);
        QVERIFY(coreToolDef(QStringLiteral("ask_user_question"))->outputTrust == ToolOutputTrust::UserOwned);
        QCOMPARE(coreToolDef(QStringLiteral("bash"))->workspacePathArg, QStringLiteral("working_dir"));
        QCOMPARE(coreToolDef(QStringLiteral("str_replace_editor"))->workspacePathArg, QStringLiteral("path"));
        for (const CoreToolDef &d : coreToolDefs()) {
            if (d.schema.name != QLatin1String("bash") && d.schema.name != QLatin1String("str_replace_editor"))
                QVERIFY2(d.workspacePathArg.isEmpty(), qPrintable(d.schema.name));
            if (d.schema.name != QLatin1String("web_fetch") && d.schema.name != QLatin1String("web_search")
                    && d.schema.name != QLatin1String("ask_user_question"))
                QVERIFY(d.outputTrust == ToolOutputTrust::Normal);
        }
    }

    void summaries()
    {
        const auto sum = [](const char *name, const QJsonObject &input) {
            return coreToolDef(QLatin1String(name))->summarize(input);
        };
        QCOMPARE(sum("bash", obj({ { "command", "git status" } })), QStringLiteral("git status"));
        QCOMPARE(sum("bash", QJsonObject()), QStringLiteral("Run PowerShell command"));
        QCOMPARE(sum("computer", obj({ { "action", "screenshot" } })), QStringLiteral("Look at the screen"));
        QCOMPARE(sum("computer", obj({ { "action", "left_click" },
                                       { "coordinate", QJsonArray{ 10, 20 } } })),
                 QStringLiteral("Left click (10, 20)"));
        QCOMPARE(sum("computer", obj({ { "action", "type" }, { "text", "hello" } })),
                 QStringLiteral("Type \"hello\""));
        QCOMPARE(sum("computer", obj({ { "action", "key" }, { "text", "Return" } })),
                 QStringLiteral("Press shortcut Return"));
        QCOMPARE(sum("computer", obj({ { "action", "wait" }, { "duration", 2.0 } })),
                 QStringLiteral("Wait 2.0s"));
        QCOMPARE(sum("computer", obj({ { "action", "scroll" } })), QStringLiteral("Scroll down"));
        QCOMPARE(sum("str_replace_editor", obj({ { "command", "view" }, { "path", "/x/y.txt" } })),
                 QStringLiteral("view y.txt"));
        QCOMPARE(sum("glob_grep", obj({ { "mode", "grep" }, { "pattern", "foo" } })),
                 QStringLiteral("Search code for \"foo\""));
        QCOMPARE(sum("glob_grep", obj({ { "mode", "glob" }, { "pattern", "*.cpp" } })),
                 QStringLiteral("Find files matching \"*.cpp\""));
        QCOMPARE(sum("memory", obj({ { "command", "view" }, { "path", "/memories/notes.md" } })),
                 QStringLiteral("view notes.md"));
        QCOMPARE(sum("memory", QJsonObject()), QStringLiteral("view /memories"));
        QCOMPARE(sum("memory_search", obj({ { "query", "deadlines" } })),
                 QStringLiteral("Search memories: deadlines"));
        QCOMPARE(sum("memory_search", QJsonObject()), QStringLiteral("Search memories"));
        QCOMPARE(sum("web_search", obj({ { "query", "news" } })), QStringLiteral("Search \"news\""));
        QCOMPARE(sum("web_search", QJsonObject()), QStringLiteral("Search live web"));
        QCOMPARE(sum("web_fetch", obj({ { "url", "https://example.com/x" } })),
                 QStringLiteral("Fetch example.com (text)"));
        QCOMPARE(sum("todo_write", obj({ { "action", "todos" },
                                          { "todos", QJsonArray{ QJsonObject(), QJsonObject(), QJsonObject() } } })),
                 QStringLiteral("Update task checklist (3 items)"));
        QCOMPARE(sum("todo_write", obj({ { "action", "clipboard_read" } })),
                 QStringLiteral("Read Windows clipboard"));
        QCOMPARE(sum("todo_write", obj({ { "action", "system_info" } })),
                 QStringLiteral("Inspect desktop & system telemetry"));
        QJsonObject askQ;
        askQ[QStringLiteral("question")] = QStringLiteral("Which format?");
        QJsonObject optPdf; optPdf[QStringLiteral("label")] = QStringLiteral("PDF");
        QJsonObject optWord; optWord[QStringLiteral("label")] = QStringLiteral("Word");
        askQ[QStringLiteral("options")] = QJsonArray{ optPdf, optWord };
        QJsonObject askObj; askObj[QStringLiteral("questions")] = QJsonArray{ askQ };
        QCOMPARE(sum("ask_user_question", askObj), QStringLiteral("Ask: Which format?"));
        QCOMPARE(sum("skill", obj({ { "name", "pdf" } })), QStringLiteral("Load skill pdf"));
        QCOMPARE(sum("skill", obj({ { "name", "pdf" }, { "file", "ref.md" } })),
                 QStringLiteral("Read ref.md of skill pdf"));
        QCOMPARE(sum("share_screenshot", QJsonObject()), QStringLiteral("Show you the screen"));
        QCOMPARE(sum("share_screenshot", obj({ { "caption", "look" } })),
                 QStringLiteral("Show you the screen: look"));
        QCOMPARE(sum("music", obj({ { "action", "play" } })), QStringLiteral("Play music"));
        QCOMPARE(sum("music", obj({ { "action", "volume" }, { "volume", 40 } })),
                 QStringLiteral("Music volume 40%"));
        QCOMPARE(sum("music", obj({ { "action", "next" } })), QStringLiteral("Next track"));
        QCOMPARE(sum("music", obj({ { "action", "play_track" }, { "query", "song" } })),
                 QStringLiteral("Play \"song\""));
    }

    void iconsAndBadges()
    {
        const auto icon = [](const char *name, const QJsonObject &input) {
            return resolvedIcon(*coreToolDef(QLatin1String(name)), input);
        };
        const auto badge = [](const char *name, const QJsonObject &input) {
            return resolvedBadge(*coreToolDef(QLatin1String(name)), input);
        };
        QCOMPARE(icon("bash", QJsonObject()), QStringLiteral("terminal"));
        QCOMPARE(icon("computer", obj({ { "action", "screenshot" } })), QStringLiteral("computer"));
        QCOMPARE(icon("computer", obj({ { "action", "left_click" } })), QStringLiteral("cursor"));
        QCOMPARE(icon("computer", obj({ { "action", "hold_key" } })), QStringLiteral("keyboard"));
        QCOMPARE(icon("ask_user_question", QJsonObject()), QStringLiteral("chat"));
        QCOMPARE(icon("share_screenshot", QJsonObject()), QStringLiteral("eye"));
        QCOMPARE(icon("music", QJsonObject()), QStringLiteral("music"));
        QCOMPARE(icon("str_replace_editor", QJsonObject()), QStringLiteral("doc"));
        QCOMPARE(icon("glob_grep", QJsonObject()), QStringLiteral("code-search"));
        QCOMPARE(icon("memory", QJsonObject()), QStringLiteral("memory"));
        QCOMPARE(icon("web_search", QJsonObject()), QStringLiteral("globe"));
        QCOMPARE(icon("web_fetch", QJsonObject()), QStringLiteral("web-fetch"));
        QCOMPARE(icon("todo_write", obj({ { "action", "todos" } })), QStringLiteral("tasks"));
        QCOMPARE(icon("todo_write", obj({ { "action", "clipboard_read" } })), QStringLiteral("clipboard"));
        QCOMPARE(icon("todo_write", obj({ { "action", "system_info" } })), QStringLiteral("activity"));
        QCOMPARE(icon("skill", QJsonObject()), QStringLiteral("spark"));
        // memory_search has no icon of its own: the caller falls back.
        QVERIFY(coreToolDef(QStringLiteral("memory_search"))->icon.isEmpty());
        QVERIFY(coreToolDef(QStringLiteral("memory_search"))->iconFor == nullptr);

        QCOMPARE(badge("bash", QJsonObject()), QStringLiteral("SHELL"));
        QCOMPARE(badge("computer", QJsonObject()), QStringLiteral("COMPUTER"));
        QCOMPARE(badge("ask_user_question", QJsonObject()), QStringLiteral("QUESTION"));
        QCOMPARE(badge("share_screenshot", QJsonObject()), QStringLiteral("SCREENSHOT"));
        QCOMPARE(badge("music", QJsonObject()), QStringLiteral("MUSIC"));
        QCOMPARE(badge("str_replace_editor", QJsonObject()), QStringLiteral("EDITOR"));
        QCOMPARE(badge("memory", QJsonObject()), QStringLiteral("MEMORY"));
        QCOMPARE(badge("memory_search", QJsonObject()), QStringLiteral("MEMORY SEARCH"));
        QCOMPARE(badge("web_search", QJsonObject()), QStringLiteral("WEB SEARCH"));
        QCOMPARE(badge("web_fetch", QJsonObject()), QStringLiteral("WEB FETCH"));
        QCOMPARE(badge("glob_grep", obj({ { "mode", "grep" } })), QStringLiteral("GREP"));
        QCOMPARE(badge("glob_grep", obj({ { "mode", "glob" } })), QStringLiteral("GLOB"));
        QCOMPARE(badge("todo_write", obj({ { "action", "todos" } })), QStringLiteral("TASKS"));
        QCOMPARE(badge("todo_write", obj({ { "action", "clipboard_write" } })), QStringLiteral("CLIPBOARD"));
        QCOMPARE(badge("todo_write", obj({ { "action", "system_info" } })), QStringLiteral("SYSTEM"));
        // skill has no badge of its own: the caller upper-cases the name.
        QVERIFY(coreToolDef(QStringLiteral("skill"))->badge.isEmpty());
        QVERIFY(coreToolDef(QStringLiteral("skill"))->badgeFor == nullptr);
    }

    void details()
    {
        const auto det = [](const char *name, const QJsonObject &input) {
            const CoreToolDef *d = coreToolDef(QLatin1String(name));
            return d->detail ? d->detail(input) : QString();
        };
        QCOMPARE(det("bash", obj({ { "command", "  ls  " } })), QStringLiteral("$ ls"));
        QCOMPARE(det("computer", obj({ { "action", "key" }, { "text", "Return" } })),
                 QStringLiteral("action: key  text: \"Return\""));
        QCOMPARE(det("str_replace_editor", obj({ { "command", "view" }, { "path", "/a/b.txt" } })),
                 QStringLiteral("view  path: /a/b.txt"));
        QCOMPARE(det("glob_grep", obj({ { "mode", "grep" }, { "pattern", "x" }, { "path", "/src" } })),
                 QStringLiteral("mode: grep  pattern: \"x\"  in: /src"));
        QCOMPARE(det("memory", obj({ { "command", "rename" },
                                     { "old_path", "/memories/a.md" },
                                     { "new_path", "/memories/b.md" } })),
                 QStringLiteral("rename /memories/a.md → /memories/b.md"));
        QCOMPARE(det("web_search", obj({ { "query", "q" } })), QStringLiteral("query: \"q\""));
        QCOMPARE(det("web_fetch", obj({ { "url", "https://e.com" } })),
                 QStringLiteral("url: https://e.com  extract: text"));
        QCOMPARE(det("todo_write", obj({ { "action", "todos" } })), QStringLiteral("action: todos"));
        QCOMPARE(det("share_screenshot", QJsonObject()), QStringLiteral("the screen Buddy is looking at"));
        QCOMPARE(det("share_screenshot", obj({ { "display", 1 } })), QStringLiteral("display 1"));
        QCOMPARE(det("music", obj({ { "action", "volume" }, { "volume", 30 } })),
                 QStringLiteral("action: volume  volume: 30"));
        QJsonObject detQ;
        detQ[QStringLiteral("question")] = QStringLiteral("Which format?");
        QJsonObject detPdf; detPdf[QStringLiteral("label")] = QStringLiteral("PDF");
        QJsonObject detWord; detWord[QStringLiteral("label")] = QStringLiteral("Word");
        detQ[QStringLiteral("options")] = QJsonArray{ detPdf, detWord };
        QJsonObject detObj; detObj[QStringLiteral("questions")] = QJsonArray{ detQ };
        QCOMPARE(det("ask_user_question", detObj), QStringLiteral("Which format? [PDF | Word]"));
        // These two have no detail body: the caller falls back to JSON.
        QVERIFY(coreToolDef(QStringLiteral("memory_search"))->detail == nullptr);
        QVERIFY(coreToolDef(QStringLiteral("skill"))->detail == nullptr);
    }

    void policyDispatchUsesRegistryKinds()
    {
        AgentPolicy policy;
        const auto rule = [&policy](const QString &tool, const QJsonObject &input) {
            return policy.evaluate(tool, input).rule;
        };
        // The metadata-driven kinds resolve through the registry.
        QCOMPARE(rule(QStringLiteral("share_screenshot"), QJsonObject()), QStringLiteral("read-only"));
        for (const char *n : { "memory", "memory_search", "todo_write", "ask_user_question", "skill" })
            QCOMPARE(rule(QLatin1String(n), QJsonObject()), QStringLiteral("safe-tool"));
        QCOMPARE(rule(QStringLiteral("glob_grep"), QJsonObject()), QStringLiteral("safe-tool"));
        QCOMPARE(rule(QStringLiteral("web_search"), QJsonObject()), QStringLiteral("safe-tool"));
        QCOMPARE(rule(QStringLiteral("web_fetch"), QJsonObject()), QStringLiteral("network-read"));
        QCOMPARE(rule(QStringLiteral("computer"), obj({ { "action", "screenshot" } })),
                 QStringLiteral("read-only"));
        QCOMPARE(rule(QStringLiteral("music"), obj({ { "action", "status" } })),
                 QStringLiteral("read-only"));
        // Unknown tools still deny, and connector tools still fall
        // through to the connector table (registered at runtime).
        QCOMPARE(rule(QStringLiteral("no_such_tool"), QJsonObject()), QStringLiteral("deny:unknown-tool"));
        policy.setConnectorTools({ { QStringLiteral("gmail_search_messages"), false } });
        QCOMPARE(rule(QStringLiteral("gmail_search_messages"), QJsonObject()),
                 QStringLiteral("connector-read"));
    }
};

QTEST_MAIN(TstToolRegistry)
#include "tst_toolregistry.moc"
