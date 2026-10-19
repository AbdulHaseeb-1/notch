#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Project instruction files and slash commands for Buddy.
//
// Instructions: a NOCH.md in the workspace folder (or .noch/NOCH.md; AGENTS.md
// and CLAUDE.md are read too when there is no NOCH.md, so a repository that
// already has one works), plus an optional NOCH.md of the user's own in Noch's
// data folder. They are standing instructions added to the system prompt of
// every run in that workspace.
//
// Slash commands: built-ins (/help, /new, /undo ...) and the user's own
// prompt templates, one Markdown file each in <workspace>/.noch/commands/ or
// <data folder>/commands/, run as /name arguments.
//
// Pure file and text handling (no event loop), unit tested. The workspace
// files can be written by the agent itself, which is why AgentPolicy always
// asks before it changes one (see isInstructionPath).
namespace pc {

constexpr qint64 kMaxInstructionFileBytes = 16 * 1024;
constexpr qint64 kMaxInstructionTotalBytes = 24 * 1024;
constexpr qint64 kMaxCommandFileBytes = 16 * 1024;
constexpr int kMaxCommands = 100;
constexpr int kMaxCommandNameChars = 32;

struct InstructionSource {
    QString name;      // "NOCH.md (workspace)"
    QString path;
    qint64 bytes = 0;
    bool truncated = false;
};

struct Instructions {
    QString text;                      // all sources, ready for the prompt; empty when there are none
    QList<InstructionSource> sources;
    bool truncated = false;
    bool empty() const { return text.trimmed().isEmpty(); }
};

// Reads the user-level file, then the workspace file. Either folder may be empty.
Instructions loadInstructions(const QString &workspace, const QString &userDir);

// The fenced block that goes into the system prompt ("" when there are no instructions).
QString systemPromptSection(const Instructions &instructions);

// A starting NOCH.md for /init.
QString instructionsTemplate();

// Is this path one of the files whose content steers Buddy from now on
// (instruction files and anything under a .noch folder)?
bool isInstructionPath(const QString &path);

// ---- slash commands -------------------------------------------------------------

struct Command {
    QString name;          // lower case, [a-z0-9_-], no slash
    QString description;
    QString argumentHint;  // "[focus]"
    QString body;          // the prompt template (empty for built-ins)
    QString source;        // "builtin" | "project" | "user"
    bool builtin() const { return source == QLatin1String("builtin"); }
};

QList<Command> builtinCommands();
// Built-ins first, then the user's: project files override user files; a file
// cannot replace a built-in.
QList<Command> loadCommands(const QString &workspace, const QString &userDir);

struct Slash {
    bool isCommand = false;   // the text starts with /name
    QString name;
    QString args;
};
// "/review the parser" -> { review, "the parser" }. A path like "/usr/bin" or a
// lone "/" is not a command.
Slash parseSlash(const QString &text);

// $ARGUMENTS is all the arguments, $1..$9 the words (quotes group them). If the
// template mentions none of them, the arguments are appended on a new line.
QString expand(const Command &command, const QString &args);

// The closest command name for a typo ("" when nothing is close).
QString suggestCommand(const QList<Command> &commands, const QString &name);

// Frontmatter: leading `---` lines of `key: value`. Exposed for tests.
struct Parsed {
    QString description;
    QString argumentHint;
    QString body;
};
Parsed parseCommandFile(const QString &raw);

} // namespace pc
