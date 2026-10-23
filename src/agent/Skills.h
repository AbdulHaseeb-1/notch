#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Agent Skills for Buddy, in the format of Claude Code and the Claude Agent SDK.
//
// A skill is a folder with a SKILL.md: YAML frontmatter (`name`, `description`)
// and then Markdown instructions, plus any files or scripts it brings along.
// Skills load in three steps so a long list costs almost nothing:
//   1. only each skill's name and description go into the system prompt;
//   2. when a task matches, the model calls the `skill` tool and gets the
//      SKILL.md body (and a list of the bundled files);
//   3. bundled files are read only when the instructions point at them.
//
// Where they are found (later wins on a name clash): Noch's built-in skills
// (resources), <data folder>/skills/<name>/, <workspace>/.claude/skills/<name>/
// and <workspace>/.noch/skills/<name>/.
//
// A skill is the user's text, but it never carries authority: it cannot grant a
// tool or loosen the permission mode. `allowed-tools` (and the other Claude
// Code keys Noch has no use for: model, context, agent, hooks) is read and
// ignored. Everything a skill leads Buddy to do goes through AgentPolicy and its
// approval cards like any other step.
//
// Pure file and text handling (no event loop), unit tested.
namespace skills {

constexpr qint64 kMaxSkillFileBytes = 64 * 1024;     // a SKILL.md longer than this is cut
constexpr qint64 kMaxFrontmatterBytes = 8 * 1024;    // a longer frontmatter is malformed
constexpr qint64 kMaxBundledFileBytes = 128 * 1024;  // the most one `file` read returns
constexpr int kMaxSkills = 64;
constexpr int kMaxNameChars = 64;
constexpr int kMaxDescriptionChars = 1024;
constexpr int kMaxListedDescriptionChars = 300;      // per skill, in the system prompt
constexpr int kMaxListingBytes = 8 * 1024;           // the whole list in the system prompt
constexpr int kMaxBundledFiles = 40;
constexpr int kMaxBundledDepth = 3;

struct Skill {
    QString name;                 // lower case letters, digits, hyphens
    QString description;          // one line; when_to_use is appended to it
    QString argumentHint;         // "[topic]"
    QString source;               // "builtin" | "user" | "project"
    QString dir;                  // the skill's folder
    QString file;                 // its SKILL.md
    qint64 bytes = 0;             // size of the SKILL.md
    bool truncated = false;       // SKILL.md was longer than kMaxSkillFileBytes
    bool userInvocable = true;    // may be run as /name
    bool modelInvocable = true;   // listed for the model and loadable by the skill tool
    bool enabled = true;          // switched off in Settings -> not listed, not loadable
    QString overrides;            // source of the same-named skill this one replaced
    QStringList ignoredKeys;      // frontmatter keys that were read and ignored (allowed-tools ...)
    QString userRoot;             // where the user's own skills live (for ${NOCH_USER_SKILLS_DIR})
    QString workspaceRoot;        // where this workspace's skills go (for ${NOCH_WORKSPACE_SKILLS_DIR})
    bool builtin() const { return source == QLatin1String("builtin"); }
};

struct Catalog {
    QList<Skill> skills;          // sorted by name; disabled ones included
    QStringList notes;            // why a folder was skipped ("user: broken/SKILL.md: ...")
    const Skill *find(const QString &name) const;
    // Skills the model is told about: enabled and not model-invocation-disabled.
    QList<Skill> listed() const;
    bool anyListed() const { return !listed().isEmpty(); }
};

// Built-in skills live in the app's resources.
QString builtinSkillsDir();
// <data folder>/skills
QString userSkillsDir(const QString &userDir);

// Finds the skills. Any folder may be empty. `disabled` are the names switched
// off in Settings: they stay in the catalog with enabled = false.
Catalog loadSkills(const QString &workspace, const QString &userDir, const QString &builtinDir,
                   const QStringList &disabled = QStringList());

bool validName(const QString &name);

// ---- frontmatter ------------------------------------------------------------------
// The small YAML subset skills use: `key: value` lines (plain, 'single' or "double"
// quoted, wrapped over several lines, or a > / | block), and anything indented
// under a key (a list or a metadata map) kept as raw text. Exposed for tests.
struct Frontmatter {
    QStringList keys;             // lower case, in file order
    QStringList values;           // parallel to keys
    QString body;                 // the Markdown after the closing ---
    bool ok = false;
    QString error;                // why not ok
    QString value(const QString &key) const;
    bool has(const QString &key) const { return keys.contains(key); }
};
Frontmatter parseFrontmatter(const QString &raw);

// ---- what the model sees ------------------------------------------------------------

// The block for the system prompt ("" when no skill is listed). The same skills
// give the same text, so the prompt stays cacheable.
QString systemPromptSection(const Catalog &catalog);

// The SKILL.md body ready to give to the model: ${CLAUDE_SKILL_DIR} /
// ${NOCH_SKILL_DIR} replaced by the skill's folder, ${NOCH_USER_SKILLS_DIR} and
// ${NOCH_WORKSPACE_SKILLS_DIR} by the two places a new skill can go; `$ARGUMENTS` /
// `$1`.. filled from `args` when given. Empty when the file cannot be read any more.
QString skillBody(const Skill &skill, const QString &args = QString());

// What the `skill` tool returns: the body plus the bundled files.
QString toolResult(const Skill &skill);

// Files shipped inside the skill folder, relative with `/`, SKILL.md left out.
QStringList bundledFiles(const Skill &skill);

// One text file from inside the skill's folder (for the tool's `file` argument).
// Never leaves the folder: no absolute paths, no `..`, no links out. On failure
// returns an empty string and sets *error.
QString readBundledFile(const Skill &skill, const QString &relativePath, QString *error);

// A starting SKILL.md for a new skill.
QString skillTemplate(const QString &name);

} // namespace skills
