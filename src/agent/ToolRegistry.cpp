// The core-tool registry: every built-in tool defined once (schema,
// policy kind, access, diet, output trust, workspace path argument, step
// icon/badge/summary/detail). Consumers — ToolSchemas' API, AgentPolicy,
// BuddyProvider — read from here instead of keeping per-tool copies.
//
// The summary/detail bodies below are the per-tool step text that used
// to live as name branches in BuddyProviderTools.cpp; they are pure
// functions of the call's arguments.
#include "ToolRegistry.h"

#include "UserQuestions.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>

namespace {

// ---- step summaries ----------------------------------------------------

QString summarizeBash(const QJsonObject &input)
{
    QString cmd = input.value(QStringLiteral("command")).toString().simplified();
    if (cmd.size() > 44)
        cmd = cmd.left(41) + QStringLiteral("…");
    return cmd.isEmpty() ? QStringLiteral("Run PowerShell command") : cmd;
}

QString summarizeComputer(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString();
    const QJsonArray c = input.value(QStringLiteral("coordinate")).toArray();
    const QString coordStr = c.size() >= 2
        ? QStringLiteral(" (%1, %2)").arg(c.at(0).toInt()).arg(c.at(1).toInt())
        : QString();
    if (act == QLatin1String("screenshot"))
        return QStringLiteral("Look at the screen");
    if (act == QLatin1String("zoom"))
        return QStringLiteral("Zoom into part of the screen");
    if (act == QLatin1String("cursor_position"))
        return QStringLiteral("Read cursor coordinates");
    if (act == QLatin1String("mouse_move"))
        return QStringLiteral("Move cursor%1").arg(coordStr);
    const QString mods = input.value(QStringLiteral("text")).toString().trimmed();
    const QString withMods = (!mods.isEmpty() && act.contains(QLatin1String("click")) && act != QLatin1String("type"))
        ? QStringLiteral(" with %1").arg(mods) : QString();
    if (act == QLatin1String("left_mouse_down"))
        return QStringLiteral("Hold the mouse button%1").arg(coordStr);
    if (act == QLatin1String("left_mouse_up"))
        return QStringLiteral("Release the mouse button%1").arg(coordStr);
    if (act == QLatin1String("hold_key"))
        return QStringLiteral("Hold %1 for %2s").arg(mods).arg(input.value(QStringLiteral("duration")).toDouble(1.0), 0, 'f', 1);
    if (act == QLatin1String("left_click"))
        return QStringLiteral("Left click%1%2").arg(coordStr, withMods);
    if (act == QLatin1String("right_click"))
        return QStringLiteral("Right click%1").arg(coordStr);
    if (act == QLatin1String("double_click"))
        return QStringLiteral("Double click%1").arg(coordStr);
    if (act == QLatin1String("triple_click"))
        return QStringLiteral("Triple click%1").arg(coordStr);
    if (act == QLatin1String("left_click_drag"))
        return QStringLiteral("Drag cursor to%1").arg(coordStr);
    if (act == QLatin1String("scroll"))
        return QStringLiteral("Scroll %1").arg(input.value(QStringLiteral("scroll_direction")).toString(QStringLiteral("down")));
    if (act == QLatin1String("type")) {
        QString t = input.value(QStringLiteral("text")).toString().simplified();
        if (t.size() > 28)
            t = t.left(25) + QStringLiteral("…");
        return QStringLiteral("Type \"%1\"").arg(t);
    }
    if (act == QLatin1String("key")) {
        const int rep = input.value(QStringLiteral("repeat")).toInt(1);
        return rep > 1 ? QStringLiteral("Press shortcut %1 ×%2").arg(input.value(QStringLiteral("text")).toString()).arg(rep)
                       : QStringLiteral("Press shortcut %1").arg(input.value(QStringLiteral("text")).toString());
    }
    if (act == QLatin1String("wait"))
        return QStringLiteral("Wait %1s").arg(input.value(QStringLiteral("duration")).toDouble(1.0), 0, 'f', 1);
    return act;
}

QString summarizeAskUserQuestion(const QJsonObject &input)
{
    return uq::summary(uq::parse(input).questions);
}

QString summarizeMusic(const QJsonObject &input)
{
    const QString action = input.value(QStringLiteral("action")).toString().trimmed().toLower();
    if (action == QLatin1String("play_track") || action == QLatin1String("play_online"))
        return QStringLiteral("Play \"%1\"").arg(input.value(QStringLiteral("query")).toString().simplified().left(40));
    if (action == QLatin1String("add"))
        return QStringLiteral("Add %1 to the music queue")
            .arg(QFileInfo(input.value(QStringLiteral("path")).toString()).fileName());
    if (action == QLatin1String("volume"))
        return QStringLiteral("Music volume %1%").arg(input.value(QStringLiteral("volume")).toInt());
    if (action == QLatin1String("next"))
        return QStringLiteral("Next track");
    if (action == QLatin1String("previous"))
        return QStringLiteral("Previous track");
    if (action == QLatin1String("pause"))
        return QStringLiteral("Pause the music");
    if (action == QLatin1String("play"))
        return QStringLiteral("Play music");
    return QStringLiteral("Music: %1").arg(action);
}

QString summarizeShareScreenshot(const QJsonObject &input)
{
    const QString caption = input.value(QStringLiteral("caption")).toString().simplified().left(60);
    return caption.isEmpty() ? QStringLiteral("Show you the screen") : QStringLiteral("Show you the screen: %1").arg(caption);
}

QString summarizeEditor(const QJsonObject &input)
{
    const QString cmd = input.value(QStringLiteral("command")).toString();
    const QString path = QFileInfo(input.value(QStringLiteral("path")).toString()).fileName();
    return QStringLiteral("%1 %2").arg(cmd, path.isEmpty() ? input.value(QStringLiteral("path")).toString() : path);
}

QString summarizeGlobGrep(const QJsonObject &input)
{
    const QString mode = input.value(QStringLiteral("mode")).toString(QStringLiteral("glob"));
    const QString pat = input.value(QStringLiteral("pattern")).toString();
    return mode == QLatin1String("grep")
        ? QStringLiteral("Search code for \"%1\"").arg(pat.left(32))
        : QStringLiteral("Find files matching \"%1\"").arg(pat.left(32));
}

QString summarizeMemory(const QJsonObject &input)
{
    const QString cmd = input.value(QStringLiteral("command")).toString(QStringLiteral("view"));
    const QString rawPath = input.value(QStringLiteral("path")).toString(
        input.value(QStringLiteral("old_path")).toString(QStringLiteral("/memories")));
    const QString baseName = QFileInfo(rawPath).fileName();
    const QString target = (baseName.isEmpty() || baseName == QLatin1String("memories"))
        ? QStringLiteral("/memories")
        : baseName;
    return QStringLiteral("%1 %2").arg(cmd, target);
}

QString summarizeMemorySearch(const QJsonObject &input)
{
    const QString q = input.value(QStringLiteral("query")).toString().simplified();
    const QString shown = q.size() > 40 ? q.left(37) + QStringLiteral("…") : q;
    return shown.isEmpty() ? QStringLiteral("Search memories")
                           : QStringLiteral("Search memories: %1").arg(shown);
}

QString summarizeWebSearch(const QJsonObject &input)
{
    QString q = input.value(QStringLiteral("query")).toString().simplified();
    if (q.isEmpty())
        q = input.value(QStringLiteral("url")).toString().simplified();
    if (q.size() > 38)
        q = q.left(35) + QStringLiteral("…");
    return q.isEmpty() ? QStringLiteral("Search live web")
                       : QStringLiteral("Search \"%1\"").arg(q);
}

QString summarizeWebFetch(const QJsonObject &input)
{
    const QUrl u(input.value(QStringLiteral("url")).toString());
    const QString host = u.host().isEmpty() ? input.value(QStringLiteral("url")).toString().left(34) : u.host();
    const QString mode = input.value(QStringLiteral("extract_mode")).toString(QStringLiteral("text"));
    return QStringLiteral("Fetch %1 (%2)").arg(host, mode);
}

QString summarizeTodoWrite(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString(QStringLiteral("todos"));
    if (act == QLatin1String("clipboard_read"))
        return QStringLiteral("Read Windows clipboard");
    if (act == QLatin1String("clipboard_write"))
        return QStringLiteral("Copy text to clipboard");
    if (act == QLatin1String("system_info"))
        return QStringLiteral("Inspect desktop & system telemetry");
    const int count = input.value(QStringLiteral("todos")).toArray().size();
    return QStringLiteral("Update task checklist (%1 items)").arg(count);
}

QString summarizeSkill(const QJsonObject &input)
{
    const QString file = input.value(QStringLiteral("file")).toString().simplified().left(40);
    const QString skill = input.value(QStringLiteral("name")).toString().simplified().left(40);
    return file.isEmpty() ? QStringLiteral("Load skill %1").arg(skill) : QStringLiteral("Read %1 of skill %2").arg(file, skill);
}

// ---- argument-dependent icons and badges --------------------------------

QString iconComputer(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString();
    if (act.contains(QLatin1String("click")) || act.contains(QLatin1String("mouse")) || act == QLatin1String("cursor_position"))
        return QStringLiteral("cursor");
    if (act == QLatin1String("hold_key"))
        return QStringLiteral("keyboard");
    return QStringLiteral("computer");
}

QString iconTodoWrite(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString(QStringLiteral("todos"));
    if (act.startsWith(QLatin1String("clipboard")))
        return QStringLiteral("clipboard");
    if (act == QLatin1String("system_info"))
        return QStringLiteral("activity");
    return QStringLiteral("tasks");
}

QString badgeGlobGrep(const QJsonObject &input)
{
    const QString mode = input.value(QStringLiteral("mode")).toString(QStringLiteral("glob")).toUpper();
    return mode == QLatin1String("GREP") ? QStringLiteral("GREP") : QStringLiteral("GLOB");
}

QString badgeTodoWrite(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString(QStringLiteral("todos"));
    if (act.startsWith(QLatin1String("clipboard")))
        return QStringLiteral("CLIPBOARD");
    if (act == QLatin1String("system_info"))
        return QStringLiteral("SYSTEM");
    return QStringLiteral("TASKS");
}

// ---- approval-card details ----------------------------------------------

QString detailBash(const QJsonObject &input)
{
    return QStringLiteral("$ ") + input.value(QStringLiteral("command")).toString().trimmed();
}

QString detailComputer(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString();
    const QJsonArray c = input.value(QStringLiteral("coordinate")).toArray();
    if (!input.value(QStringLiteral("text")).toString().isEmpty())
        return QStringLiteral("action: %1  text: \"%2\"").arg(act, input.value(QStringLiteral("text")).toString());
    if (c.size() >= 2)
        return QStringLiteral("action: %1  coord: [%2, %3]").arg(act).arg(c.at(0).toInt()).arg(c.at(1).toInt());
    return QStringLiteral("action: %1").arg(act);
}

QString detailMusic(const QJsonObject &input)
{
    QStringList parts{ QStringLiteral("action: %1").arg(input.value(QStringLiteral("action")).toString()) };
    for (const char *key : { "query", "path", "volume" })
        if (input.contains(QLatin1String(key)))
            parts << QStringLiteral("%1: %2").arg(QLatin1String(key), input.value(QLatin1String(key)).toVariant().toString());
    return parts.join(QStringLiteral("  "));
}

QString detailShareScreenshot(const QJsonObject &input)
{
    const int display = input.value(QStringLiteral("display")).toInt(-1);
    return display >= 0 ? QStringLiteral("display %1").arg(display) : QStringLiteral("the screen Buddy is looking at");
}

QString detailAskUserQuestion(const QJsonObject &input)
{
    QStringList lines;
    for (const uq::Question &q : uq::parse(input).questions) {
        QStringList labels;
        for (const uq::Option &o : q.options)
            labels << o.label;
        lines << QStringLiteral("%1 [%2]").arg(q.question, labels.join(QStringLiteral(" | ")));
    }
    return lines.join(QLatin1Char('\n'));
}

QString detailEditor(const QJsonObject &input)
{
    return QStringLiteral("%1  path: %2").arg(input.value(QStringLiteral("command")).toString(),
                                              input.value(QStringLiteral("path")).toString());
}

QString detailGlobGrep(const QJsonObject &input)
{
    const QString mode = input.value(QStringLiteral("mode")).toString(QStringLiteral("glob"));
    const QString pat = input.value(QStringLiteral("pattern")).toString();
    const QString path = input.value(QStringLiteral("path")).toString(QStringLiteral("."));
    return QStringLiteral("mode: %1  pattern: \"%2\"  in: %3").arg(mode, pat, path);
}

QString detailMemory(const QJsonObject &input)
{
    const QString cmd = input.value(QStringLiteral("command")).toString(QStringLiteral("view"));
    if (cmd == QLatin1String("rename")) {
        return QStringLiteral("rename %1 → %2")
            .arg(input.value(QStringLiteral("old_path")).toString(input.value(QStringLiteral("path")).toString()),
                 input.value(QStringLiteral("new_path")).toString());
    }
    return QStringLiteral("%1  %2").arg(cmd, input.value(QStringLiteral("path")).toString(QStringLiteral("/memories")));
}

QString detailWebSearch(const QJsonObject &input)
{
    const QString q = input.value(QStringLiteral("query")).toString().trimmed();
    const QString u = input.value(QStringLiteral("url")).toString().trimmed();
    if (!q.isEmpty() && !u.isEmpty())
        return QStringLiteral("query: \"%1\"  url: %2").arg(q, u);
    if (!u.isEmpty())
        return QStringLiteral("url: %1").arg(u);
    return QStringLiteral("query: \"%1\"").arg(q);
}

QString detailWebFetch(const QJsonObject &input)
{
    return QStringLiteral("url: %1  extract: %2")
        .arg(input.value(QStringLiteral("url")).toString(),
             input.value(QStringLiteral("extract_mode")).toString(QStringLiteral("text")));
}

QString detailTodoWrite(const QJsonObject &input)
{
    const QString act = input.value(QStringLiteral("action")).toString(QStringLiteral("todos"));
    return QStringLiteral("action: %1").arg(act);
}

} // namespace

// ---- the registry ---------------------------------------------------------
const QList<CoreToolDef> &coreToolDefs()
{
    static const QList<CoreToolDef> defs = [] {
        QList<CoreToolDef> out;
        // bash
        {
            ToolSchema s;
            s.name = QStringLiteral("bash");
            s.description = QStringLiteral("Run a PowerShell command on the Windows desktop and return stdout/stderr.");
            s.args = {
                { QStringLiteral("command"), QStringLiteral("string"),
                  QStringLiteral("PowerShell command line to run on Windows."), true },
                { QStringLiteral("working_dir"), QStringLiteral("string"),
                  QStringLiteral("Optional working directory path."), false },
                { QStringLiteral("timeout_seconds"), QStringLiteral("integer"),
                  QStringLiteral("Optional time limit in seconds (default 30, max 300) for long jobs."), false },
            };
            s.aliases = {
                { QStringLiteral("timeout"), QStringLiteral("timeout_seconds") },
                { QStringLiteral("cmd"), QStringLiteral("command") },
                { QStringLiteral("cwd"), QStringLiteral("working_dir") },
                { QStringLiteral("dir"), QStringLiteral("working_dir") },
                { QStringLiteral("workingdir"), QStringLiteral("working_dir") },
            };
            s.coercions = {
                { QStringLiteral("timeout_seconds"), QStringLiteral("int") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::Shell;
            def.access = ToolAccess::Execute;
            def.workspacePathArg = QStringLiteral("working_dir");
            def.icon = QStringLiteral("terminal");
            def.badge = QStringLiteral("SHELL");
            def.summarize = summarizeBash;
            def.detail = detailBash;
            out << def;
        }

        // computer
        {
            ToolSchema s;
            s.name = QStringLiteral("computer");
            s.description = QStringLiteral("See the screen and control the mouse and keyboard.");
            s.args = {
                { QStringLiteral("action"), QStringLiteral("string"),
                  QStringLiteral("Action to perform (screenshot, click, type, key, etc.)."), true },
                { QStringLiteral("coordinate"), QStringLiteral("array"),
                  QStringLiteral("Target [x, y] in screenshot space."), false },
                { QStringLiteral("start_coordinate"), QStringLiteral("array"),
                  QStringLiteral("Drag start [x, y]."), false },
                { QStringLiteral("text"), QStringLiteral("string"),
                  QStringLiteral("Text to type, key combo, or modifiers."), false },
                { QStringLiteral("scroll_direction"), QStringLiteral("string"),
                  QStringLiteral("up, down, left, or right."), false },
                { QStringLiteral("scroll_amount"), QStringLiteral("integer"),
                  QStringLiteral("Scroll clicks."), false },
                { QStringLiteral("duration"), QStringLiteral("number"),
                  QStringLiteral("Hold duration in seconds."), false },
                { QStringLiteral("region"), QStringLiteral("array"),
                  QStringLiteral("Zoom region [x, y, width, height]."), false },
                { QStringLiteral("display"), QStringLiteral("integer"),
                  QStringLiteral("Display index."), false },
            };
            s.aliases = {
                { QStringLiteral("coords"), QStringLiteral("coordinate") },
                { QStringLiteral("coordinates"), QStringLiteral("coordinate") },
                { QStringLiteral("xy"), QStringLiteral("coordinate") },
            };
            s.coercions = {
                { QStringLiteral("duration"), QStringLiteral("double") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::Computer;
            def.access = ToolAccess::Write;
            def.iconFor = iconComputer;
            def.badge = QStringLiteral("COMPUTER");
            def.summarize = summarizeComputer;
            def.detail = detailComputer;
            out << def;
        }

        // str_replace_editor
        {
            ToolSchema s;
            s.name = QStringLiteral("str_replace_editor");
            s.description = QStringLiteral("View files/directories, create files, edit via str_replace/insert, or undo_edit.");
            s.args = {
                { QStringLiteral("command"), QStringLiteral("string"),
                  QStringLiteral("view, create, str_replace, insert, undo_edit."), true },
                { QStringLiteral("path"), QStringLiteral("string"),
                  QStringLiteral("File or directory path."), true },
                { QStringLiteral("file_text"), QStringLiteral("string"),
                  QStringLiteral("Content for create."), false },
                { QStringLiteral("old_str"), QStringLiteral("string"),
                  QStringLiteral("Text to replace."), false },
                { QStringLiteral("new_str"), QStringLiteral("string"),
                  QStringLiteral("Replacement text."), false },
                { QStringLiteral("insert_line"), QStringLiteral("integer"),
                  QStringLiteral("Line number for insert (1-indexed)."), false },
                { QStringLiteral("view_range"), QStringLiteral("array"),
                  QStringLiteral("Optional [start_line, end_line] for view."), false },
            };
            s.aliases = {
                { QStringLiteral("cmd"), QStringLiteral("command") },
                { QStringLiteral("filepath"), QStringLiteral("path") },
                { QStringLiteral("file"), QStringLiteral("path") },
                { QStringLiteral("filename"), QStringLiteral("path") },
            };
            s.coercions = {
                { QStringLiteral("insert_line"), QStringLiteral("int") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::Editor;
            def.access = ToolAccess::Write;
            def.workspacePathArg = QStringLiteral("path");
            def.icon = QStringLiteral("doc");
            def.badge = QStringLiteral("EDITOR");
            def.summarize = summarizeEditor;
            def.detail = detailEditor;
            out << def;
        }

        // glob_grep
        {
            ToolSchema s;
            s.name = QStringLiteral("glob_grep");
            s.description = QStringLiteral("Fast recursive file discovery and regex code/text search with line numbers.");
            s.args = {
                { QStringLiteral("mode"), QStringLiteral("string"),
                  QStringLiteral("'glob' for filenames, 'grep' for contents."), true,
                  { QStringLiteral("glob"), QStringLiteral("grep") } },
                { QStringLiteral("pattern"), QStringLiteral("string"),
                  QStringLiteral("Wildcard pattern for glob, regex for grep."), true },
                { QStringLiteral("path"), QStringLiteral("string"),
                  QStringLiteral("Root directory or file to search in."), false },
                { QStringLiteral("file_glob"), QStringLiteral("string"),
                  QStringLiteral("Filename filter for grep (e.g. '*.qml')."), false },
                { QStringLiteral("max_results"), QStringLiteral("integer"),
                  QStringLiteral("Maximum matches (default 25)."), false },
            };
            s.aliases = {
                { QStringLiteral("query"), QStringLiteral("pattern") },
                { QStringLiteral("regex"), QStringLiteral("pattern") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::CodeSearch;
            def.access = ToolAccess::Read;
            def.icon = QStringLiteral("code-search");
            def.badgeFor = badgeGlobGrep;
            def.summarize = summarizeGlobGrep;
            def.detail = detailGlobGrep;
            out << def;
        }

        // memory
        {
            ToolSchema s;
            s.name = QStringLiteral("memory");
            s.description = QStringLiteral("View, create, update, delete, or rename persistent memory files across sessions.");
            s.args = {
                { QStringLiteral("command"), QStringLiteral("string"),
                  QStringLiteral("view, create, str_replace, insert, delete, rename."), true },
                { QStringLiteral("path"), QStringLiteral("string"),
                  QStringLiteral("Memory file path."), false },
                { QStringLiteral("file_text"), QStringLiteral("string"),
                  QStringLiteral("Content for create."), false },
                { QStringLiteral("old_str"), QStringLiteral("string"),
                  QStringLiteral("Text to replace."), false },
                { QStringLiteral("new_str"), QStringLiteral("string"),
                  QStringLiteral("Replacement text."), false },
                { QStringLiteral("insert_line"), QStringLiteral("integer"),
                  QStringLiteral("Line number for insert."), false },
                { QStringLiteral("insert_text"), QStringLiteral("string"),
                  QStringLiteral("Text to insert."), false },
                { QStringLiteral("old_path"), QStringLiteral("string"),
                  QStringLiteral("Source for rename."), false },
                { QStringLiteral("new_path"), QStringLiteral("string"),
                  QStringLiteral("Destination for rename."), false },
                { QStringLiteral("view_range"), QStringLiteral("array"),
                  QStringLiteral("Optional [start_line, end_line]."), false },
            };
            s.aliases = {
                { QStringLiteral("cmd"), QStringLiteral("command") },
                { QStringLiteral("filepath"), QStringLiteral("path") },
                { QStringLiteral("file"), QStringLiteral("path") },
            };
            s.coercions = {
                { QStringLiteral("insert_line"), QStringLiteral("int") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowSafeTool;
            def.access = ToolAccess::Write;
            def.icon = QStringLiteral("memory");
            def.badge = QStringLiteral("MEMORY");
            def.summarize = summarizeMemory;
            def.detail = detailMemory;
            out << def;
        }

        // web_search
        {
            ToolSchema s;
            s.name = QStringLiteral("web_search");
            s.description = QStringLiteral("Search the live web for current information, news, or documentation.");
            s.args = {
                { QStringLiteral("query"), QStringLiteral("string"),
                  QStringLiteral("Search query."), false },
                { QStringLiteral("url"), QStringLiteral("string"),
                  QStringLiteral("Fetch a specific URL instead."), false },
                { QStringLiteral("max_results"), QStringLiteral("integer"),
                  QStringLiteral("Maximum results."), false },
            };
            s.aliases = {
                { QStringLiteral("q"), QStringLiteral("query") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::WebSearch;
            def.access = ToolAccess::Read;
            def.outputTrust = ToolOutputTrust::External;
            def.icon = QStringLiteral("globe");
            def.badge = QStringLiteral("WEB SEARCH");
            def.summarize = summarizeWebSearch;
            def.detail = detailWebSearch;
            out << def;
        }

        // web_fetch
        {
            ToolSchema s;
            s.name = QStringLiteral("web_fetch");
            s.description = QStringLiteral("Retrieve a URL and extract readable text, hyperlinks, or metadata.");
            s.args = {
                { QStringLiteral("url"), QStringLiteral("string"),
                  QStringLiteral("URL to fetch."), true },
                { QStringLiteral("extract_mode"), QStringLiteral("string"),
                  QStringLiteral("text, links, or metadata."), false },
            };
            s.aliases = {
                { QStringLiteral("link"), QStringLiteral("url") },
                { QStringLiteral("uri"), QStringLiteral("url") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::WebFetch;
            def.access = ToolAccess::Read;
            def.outputTrust = ToolOutputTrust::External;
            def.icon = QStringLiteral("web-fetch");
            def.badge = QStringLiteral("WEB FETCH");
            def.summarize = summarizeWebFetch;
            def.detail = detailWebFetch;
            out << def;
        }

        // todo_write
        {
            ToolSchema s;
            s.name = QStringLiteral("todo_write");
            s.description = QStringLiteral("Manage a live structured task checklist.");
            s.args = {
                { QStringLiteral("action"), QStringLiteral("string"),
                  QStringLiteral("Action (e.g. update)."), true },
                { QStringLiteral("todos"), QStringLiteral("array"),
                  QStringLiteral("Todo items."), false },
                { QStringLiteral("text"), QStringLiteral("string"),
                  QStringLiteral("Text content."), false },
            };
            s.aliases = {
                { QStringLiteral("tasks"), QStringLiteral("todos") },
                { QStringLiteral("todos_list"), QStringLiteral("todos") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowSafeTool;
            def.access = ToolAccess::Write;
            def.iconFor = iconTodoWrite;
            def.badgeFor = badgeTodoWrite;
            def.summarize = summarizeTodoWrite;
            def.detail = detailTodoWrite;
            out << def;
        }

        // ask_user_question
        {
            ToolSchema s;
            s.name = QStringLiteral("ask_user_question");
            s.description = QStringLiteral("Ask the user multiple-choice questions when the decision is theirs.");
            s.args = {
                { QStringLiteral("questions"), QStringLiteral("array"),
                  QStringLiteral("Questions to ask."), true },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowSafeTool;
            def.access = ToolAccess::Read;
            def.outputTrust = ToolOutputTrust::UserOwned;
            def.icon = QStringLiteral("chat");
            def.badge = QStringLiteral("QUESTION");
            def.summarize = summarizeAskUserQuestion;
            def.detail = detailAskUserQuestion;
            out << def;
        }

        // skill
        {
            ToolSchema s;
            s.name = QStringLiteral("skill");
            s.description = QStringLiteral("Load a skill's instructions by name.");
            s.args = {
                { QStringLiteral("name"), QStringLiteral("string"),
                  QStringLiteral("The skill's name."), true },
                { QStringLiteral("file"), QStringLiteral("string"),
                  QStringLiteral("A file that comes with the skill, relative to its folder."), false },
            };
            s.aliases = {
                { QStringLiteral("skill_name"), QStringLiteral("name") },
                { QStringLiteral("skill"), QStringLiteral("name") },
                { QStringLiteral("path"), QStringLiteral("file") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowSafeTool;
            def.access = ToolAccess::Read;
            def.icon = QStringLiteral("spark");
            def.summarize = summarizeSkill;
            out << def;
        }

        // share_screenshot
        {
            ToolSchema s;
            s.name = QStringLiteral("share_screenshot");
            s.description = QStringLiteral("Take a screenshot and show it to the user in the chat.");
            s.args = {
                { QStringLiteral("caption"), QStringLiteral("string"),
                  QStringLiteral("Caption."), false },
                { QStringLiteral("display"), QStringLiteral("integer"),
                  QStringLiteral("Display index."), false },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowReadOnly;
            def.access = ToolAccess::Read;
            def.icon = QStringLiteral("eye");
            def.badge = QStringLiteral("SCREENSHOT");
            def.summarize = summarizeShareScreenshot;
            def.detail = detailShareScreenshot;
            out << def;
        }

        // music: the music pill on the desktop
        {
            ToolSchema s;
            s.name = QStringLiteral("music");
            s.description = QStringLiteral("Control the music pill: play, pause, skip, volume, Noch's queue.");
            s.args = {
                { QStringLiteral("action"), QStringLiteral("string"),
                  QStringLiteral("What to do."), true,
                  { QStringLiteral("status"), QStringLiteral("play"), QStringLiteral("pause"),
                    QStringLiteral("next"), QStringLiteral("previous"), QStringLiteral("volume"),
                    QStringLiteral("mute"), QStringLiteral("unmute"), QStringLiteral("queue"),
                    QStringLiteral("play_track"), QStringLiteral("add"), QStringLiteral("play_online") } },
                { QStringLiteral("query"), QStringLiteral("string"),
                  QStringLiteral("For play_track: words from a title or artist in the queue. For play_online: what to play from "
                                 "YouTube, as you would type it into its search box (artist and song, or a YouTube address)."), false },
                { QStringLiteral("path"), QStringLiteral("string"),
                  QStringLiteral("For add: a music file or a folder."), false },
                { QStringLiteral("volume"), QStringLiteral("integer"),
                  QStringLiteral("For volume: 0 to 100."), false },
            };
            s.aliases = {
                { QStringLiteral("command"), QStringLiteral("action") },
                { QStringLiteral("track"), QStringLiteral("query") },
                { QStringLiteral("song"), QStringLiteral("query") },
                { QStringLiteral("file"), QStringLiteral("path") },
                { QStringLiteral("folder"), QStringLiteral("path") },
                { QStringLiteral("level"), QStringLiteral("volume") },
            };
            s.coercions = { { QStringLiteral("volume"), QStringLiteral("int") } };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::Music;
            def.access = ToolAccess::Write;
            def.icon = QStringLiteral("music");
            def.badge = QStringLiteral("MUSIC");
            def.summarize = summarizeMusic;
            def.detail = detailMusic;
            out << def;
        }

        // memory_search (P8: BM25 retrieval over /memories)
        {
            ToolSchema s;
            s.name = QStringLiteral("memory_search");
            s.description = QStringLiteral("Search saved memories (facts and procedures) by relevance.");
            s.args = {
                { QStringLiteral("query"), QStringLiteral("string"),
                  QStringLiteral("What to recall, in words."), true },
                { QStringLiteral("top_k"), QStringLiteral("integer"),
                  QStringLiteral("How many memories to return (default 5, max 10)."), false },
            };
            s.aliases = {
                { QStringLiteral("q"), QStringLiteral("query") },
            };
            CoreToolDef def;
            def.schema = s;
            def.policy = ToolPolicyKind::AllowSafeTool;
            def.access = ToolAccess::Read;
            def.badge = QStringLiteral("MEMORY SEARCH");
            def.summarize = summarizeMemorySearch;
            out << def;
        }

        return out;
    }();
    return defs;
}

const CoreToolDef *coreToolDef(const QString &name)
{
    for (const CoreToolDef &d : coreToolDefs())
        if (d.schema.name == name)
            return &d;
    return nullptr;
}

bool isCoreTool(const QString &name)
{
    return coreToolDef(name) != nullptr;
}

QStringList toolNamesForDiet(ToolDiet diet)
{
    QStringList names;
    for (const CoreToolDef &d : coreToolDefs())
        if (d.diet == diet)
            names << d.schema.name;
    return names;
}
