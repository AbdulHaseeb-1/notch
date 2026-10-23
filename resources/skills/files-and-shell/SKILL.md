---
name: files-and-shell
description: Safe habits for working with files, folders and PowerShell commands. Use before creating, editing, moving, deleting or installing anything, or running a command that changes the system.
---

# Files and shell

Look, then change, then check. Prefer steps you can undo.

## Before you change anything
- **Look at the target.** `Get-ChildItem` the folder, `view` the file, `Test-Path` the destination. Do not assume a path exists or is empty.
- **Search with the right tool.** Use `glob_grep` to find files or text; use `Get-ChildItem -Recurse` only for things it cannot do.
- **Read before you edit.** View a file, then change it with `str_replace` on the smallest unique piece. Do not rewrite a whole file to change one line.

## Prefer reversible steps
- Copy or rename to a backup (`file.bak`) before overwriting something that matters.
- Send files to the Recycle Bin rather than `Remove-Item`: `Add-Type -AssemblyName Microsoft.VisualBasic; [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($path,'OnlyErrorDialogs','SendToRecycleBin')`.
- Test a destructive or bulk command with `-WhatIf` (or list what it would touch) first, then run it for real.
- Never use wildcards or `-Recurse -Force` on a folder you have not just listed. Never touch system folders, other users' folders, or the registry unless the user asked for exactly that.

## Running commands
- Each `bash` call is a fresh PowerShell: pass full paths or `working_dir`; do not rely on a previous `cd`.
- Quote paths with spaces. Check `$LASTEXITCODE` or the output instead of assuming success.
- Keep output small: `Select-Object -First 20`, `-Tail`, or a filter.
- For long jobs set `timeout_seconds`. If a command fails, read the error and change something; do not run it again unchanged.

## Finish
Re-check the result (the file exists with the right content, the exit code is 0) and tell the user exactly what changed and where, including anything you did not do.
