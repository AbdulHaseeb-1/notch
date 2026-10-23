---
name: skill-creator
description: Helps the user write a new Buddy skill (a SKILL.md folder). Use when the user wants to teach Buddy a repeatable way of working, turn a workflow into a skill, or fix or improve an existing skill.
argument-hint: "[what the skill should teach]"
---

# Writing a skill

A skill is a folder containing a `SKILL.md`. Buddy sees only its name and description until a task matches, then loads the rest. Skills written for Claude Code and the Claude Agent SDK work here too.

## 1. Understand it
Ask (with `ask_user_question` if choices help) what job the skill should do, when it should apply, and what a good result looks like. Prefer one skill per job. If the user showed you a workflow just now, capture that.

## 2. Write `SKILL.md`
```
---
name: pdf-report-style
description: What it does and WHEN to use it, in one or two sentences.
---

# Title

Short, concrete steps. Imperative voice.
```
- `name`: lower-case letters, digits and hyphens, up to 64 characters; make it the folder name.
- `description`: the most important line. Say what the skill does and the situations or words that should trigger it ("Use when the user asks for ..."). Up to 1024 characters.
- Body: the steps, checks and pitfalls. Keep it under about 100 lines; put long reference material in extra files and point to them (`See reference.md`). Buddy reads those only when needed.
- Optional keys: `argument-hint`, `disable-model-invocation: true` (only the user can run it, as `/name`), `user-invocable: false`. `allowed-tools` is ignored in Noch: a skill cannot grant permissions.
- Never put secrets in a skill.

## 3. Save it
- For every workspace, in the user's skills folder: `${NOCH_USER_SKILLS_DIR}\<name>\SKILL.md`.
- Only for the current workspace: `${NOCH_WORKSPACE_SKILLS_DIR}\<name>\SKILL.md`.

Ask which one if it is not clear. Create the folder and file with your file tools; the user is asked to approve the write. Scripts go beside it (for example `scripts\check.ps1`) and are run as normal commands, which also ask first.

## 4. Check it
Read the file back and check the frontmatter. Tell the user the skill's name, where it is, and that `/skills` lists it, `/<name>` runs it, and Settings > Agent & tools > Skills can switch it off. If a skill never triggers, make its description more specific about when to use it.
