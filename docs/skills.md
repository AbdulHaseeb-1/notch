# Agent Skills

Skills teach Buddy better ways to work. They use the same format as Claude Code
and the Claude Agent SDK, so a skill written for those works here unchanged in
nearly every case.

A skill is a folder with a `SKILL.md`: YAML frontmatter with a `name` and a
`description`, then Markdown instructions. It can bring along other files
(reference notes, templates, scripts).

```
pdf-forms/
  SKILL.md          required: frontmatter + instructions
  reference.md      optional: read only when the instructions point to it
  scripts/fill.ps1  optional: run as a normal (approved) command
```

```markdown
---
name: pdf-forms
description: Fill in PDF forms. Use when the user mentions a PDF form, a fillable PDF or tax forms.
---

# Filling PDF forms

1. Open the file and list its fields first (see reference.md).
2. ...
```

## Progressive disclosure

Skills load in three steps, so a long list of skills costs almost nothing:

1. **Listing.** Only each skill's `name` and `description` go into the system
   prompt (about 300 characters per skill, 8 KB in all). Skills the model may not
   load, or that are switched off, are left out.
2. **Loading.** When a task matches a description, the model calls the `skill`
   tool with the skill's name. The result is the `SKILL.md` body (up to 64 KB)
   plus a list of the files that come with the skill.
3. **Bundled files.** The model reads one of those files only when the
   instructions refer to it, by calling `skill` again with `file` set to its path
   (text files up to 128 KB, never outside the skill's folder), or by running a
   script like any other command.

The skill list is read once at the start of a run, like the instruction files, so
the system prompt stays byte-identical for the whole run and the prompt cache
stays warm. Edits show up on the next message.

## Where skills live

Later entries win when two skills have the same name:

| Place | Source | Used for |
| --- | --- | --- |
| built into Noch (`resources/skills/`) | built in | the starter set below |
| `<AppData>/noch/skills/<name>/SKILL.md` (Settings > Agent & tools > Skills > *Open skills folder*) | yours | every workspace |
| `<workspace>/.claude/skills/<name>/SKILL.md` | this workspace | skills already in a Claude Code project |
| `<workspace>/.noch/skills/<name>/SKILL.md` | this workspace | skills for one project |

## Frontmatter

| Key | Meaning |
| --- | --- |
| `name` | Lower-case letters, digits and hyphens, up to 64 characters. Defaults to the folder name. |
| `description` | What the skill does and **when to use it**, up to 1024 characters. Most important line: it decides when the skill is used. If missing, the first line of the body is used. |
| `when_to_use` | Appended to the description. |
| `argument-hint` | Shown in `/` suggestions, for example `[file]`. |
| `disable-model-invocation: true` | Only you can run it (as `/name`); Buddy is not told about it. |
| `user-invocable: false` | Only Buddy can load it; it is not offered as `/name`. |

Other keys are read and ignored: `allowed-tools`, `model`, `context`, `agent`,
`hooks` (Noch has no use for them). The frontmatter is a small YAML subset:
plain, `'single'` and `"double"` quoted values, values wrapped over several
lines, `>` and `|` blocks.

In the body, `${CLAUDE_SKILL_DIR}` (or `${NOCH_SKILL_DIR}`) is replaced by the
skill's folder, and `$ARGUMENTS` / `$1`..`$9` by what was typed after `/name`.
`${NOCH_USER_SKILLS_DIR}` and `${NOCH_WORKSPACE_SKILLS_DIR}` are the two places a
new skill can go (the `skill-creator` skill uses them).

A skill that cannot be used is skipped, with a note on the Skills settings and in
`/skills`: no `---` frontmatter, frontmatter longer than 8 KB or never closed, an
invalid name, nothing after the frontmatter. A `SKILL.md` longer than 64 KB is cut
with a marker rather than refused. There can be 64 skills; hidden folders are
ignored.

## Using them

- **Automatically.** With the `skill` tool on (Settings > Agent & tools > Skills >
  *Use skills*), Buddy loads a skill when a task matches its description.
- **`/skills`** lists the skills, where they are, and any that were skipped.
- **`/<skill-name> [details]`** runs a skill on purpose. When Buddy has tools, the
  chat shows a short request and Buddy loads the skill itself; without tools the
  instructions go into the message. A command or built-in of the same name wins.
- **Settings.** *Use skills* is the master switch; each discovered skill has its
  own switch (name, description, where it comes from); *Open skills folder*
  creates and opens the folder; *Rescan* re-reads the folders.

## Skills never grant authority

A skill is text, usually written by you, sometimes by the author of a repository
you opened. It is guidance and nothing more:

- It cannot grant a tool or a permission. `allowed-tools` is ignored. Every tool
  call a skill leads to goes through the same permission policy, the same
  approval cards and the same hard stops as if you had asked for it yourself
  (see [agent-safety.md](agent-safety.md)).
- The `skill` tool itself only reads: a skill's own `SKILL.md` and files inside
  its folder (no absolute paths, `..` or links out).
- User and workspace skills are checked like any file content for text aimed at AI
  agents ("ignore previous instructions ..."). If it is found, the result is
  flagged and the run is treated as tainted, which tightens the policy. Noch's own
  built-in skills are trusted.
- Because a skill steers every later run, Buddy **always asks** before creating or
  changing a `SKILL.md` or anything under `.claude/skills`, `.claude/commands` or
  `.noch/`, even in Trusted mode. Noch's data folder is private to the agent
  except its `skills/` subfolder, which it may read and, with your approval,
  write to.
- Scripts in a skill run as ordinary `bash` commands: classified, approved and
  logged like any other.

## Built-in skills

| Skill | Teaches Buddy |
| --- | --- |
| `desktop-control` | Look before clicking, prefer shortcuts and commands to pixels, verify after each step. |
| `files-and-shell` | Check before changing, prefer reversible steps (backups, Recycle Bin, `-WhatIf`). |
| `research` | Search, open the sources, cross-check, cite. |
| `clarifying-questions` | When to use `ask_user_question` and how to phrase it. |
| `memory` | What to remember about you and how to word it. |
| `skill-creator` | Helps you write a new skill into your skills folder. |

Ask "write me a skill that ..." (or run `/skill-creator ...`) and Buddy drafts the
`SKILL.md`, asks to save it, and tells you its name.

## Code

`src/agent/Skills.{h,cpp}` is the loader (pure file and text handling, like
`ProjectContext`); `src/agent/BuddyProviderSkills.cpp` holds the `skill` tool,
the `/skills` and `/<name>` commands and the per-run snapshot. Tests:
`tst_skills` (parsing, discovery, override order, limits, the shipped skills) and
`tst_buddyagent` (prompt, tool, policy, slash commands).
