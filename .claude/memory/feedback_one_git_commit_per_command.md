---
name: feedback_one_git_commit_per_command
description: Never chain multiple git commits in one shell command; commit sequentially and verify each
metadata: 
  node_type: memory
  type: feedback
  originSessionId: aaacfe53-d320-450d-a838-9ae22076e0a6
---

NEVER stage+commit multiple batches in a single shell invocation — no
`git add A && git commit … && git add B && git commit …`, not even with `set -e`,
not even in a heredoc/`&&`/`;`/newline chain.

**Why:** GPG signing can fail on the FIRST commit's Kleopatra pinentry (dismissed /
timed out / "Kein Passwort angegeben"). With the batch continuing, the still-staged
files from the failed commit get swept into the NEXT `git commit` under the wrong
message. This has happened 2–3 times and each time produced tangled commits that then
had to be reset and redone.

**How to apply:** commit ONE batch at a time, each as a SEPARATE tool call:
`git add <batch>` → `git commit` (its own call) → VERIFY it succeeded (new SHA via
`git log --oneline -1`, clean-ish `git status`) → only THEN stage the next batch.
Never proceed to the next commit until the previous one is confirmed committed.
A settings.json PreToolUse hook now hard-blocks any single Bash command containing
2+ `git commit` (see [[hook-block-multi-git-commit]]).
