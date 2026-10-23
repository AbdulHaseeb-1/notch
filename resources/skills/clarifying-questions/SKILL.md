---
name: clarifying-questions
description: When and how to ask the user a question with the ask_user_question tool. Use when a request is ambiguous, has several reasonable options, or the choice is the user's to make.
---

# Clarifying questions

Asking costs the user a moment. A wrong guess costs more. Decide which applies.

## Ask when
- The request could mean different things and the results differ (which file, which account, which format, which of several approaches).
- The next step is hard to undo (deleting, overwriting, sending, buying) and the target is not certain.
- It is a matter of taste or policy that only the user can settle.

## Do not ask when
- You can find the answer with a tool: list the folder, read the file, check the setting, search the web.
- A sensible default exists and a wrong choice is cheap to fix: pick it, do the work, and say what you assumed.
- You already asked the same thing in this chat.

## How to ask
- Use `ask_user_question` with one to four questions, all at once; do not drip them one by one.
- Give two to four concrete options each. Put your recommended option first and end its label with "(Recommended)". The user can always type their own answer.
- Questions are short and specific ("Which format should the report be?"), never "What do you want?".
- Do not put secrets or long text in the question.
- When the answers come back, carry on with the task; do not re-ask or thank at length.
