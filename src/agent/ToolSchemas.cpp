#include "ToolSchemas.h"

#include <QJsonArray>

const QList<ToolSchema> &toolSchemas()
{
    static const QList<ToolSchema> schemas = [] {
        QList<ToolSchema> out;

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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
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
            out << s;
        }

        return out;
    }();
    return schemas;
}

const ToolSchema *toolSchema(const QString &name)
{
    for (const ToolSchema &s : toolSchemas())
        if (s.name == name)
            return &s;
    return nullptr;
}

QJsonArray requiredArgsJson(const QString &toolName)
{
    QJsonArray out;
    const ToolSchema *s = toolSchema(toolName);
    if (s)
        for (const QString &r : s->requiredArgs())
            out << r;
    return out;
}
