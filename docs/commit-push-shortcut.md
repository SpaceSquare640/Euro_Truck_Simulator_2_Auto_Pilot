# Manual Commit and Push Shortcut

Use this checklist from the `Source_Code` worktree. These are manual commands: review each result before continuing. Do not use `git add -A`, force-push, or commit secrets, local paths, generated files, or unrelated changes.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->

## 1. Open the correct worktree and check its state

In PowerShell, replace the placeholder with the path to your local `Source Code` folder:

```powershell
Set-Location '<path-to-Source Code-worktree>'
git branch --show-current
git status --short --branch
```

Continue only when the branch is `Source_Code`. Review every modified and untracked path. Stop if the worktree contains unrelated changes you do not intend to include.

## 2. Update from the remote

```powershell
git pull --ff-only origin Source_Code
```

If this fails, stop and resolve the reported issue before staging. Do not use merge, rebase, reset, or force-push as a shortcut.

## 3. Stage only the intended paths

Replace the placeholders with the exact files or directories approved for this commit:

```powershell
git add -- <path-1> <path-2>
git status --short
git diff --cached --check
git diff --cached --stat
git diff --cached
```

Inspect the complete staged diff. Confirm it contains only intended changes and no secrets, credentials, personal machine paths, confidential folders, or generated build output. If the staged set is wrong, stop and correct it before committing.

For documentation or source changes, update `Change Log.md` under `[Unreleased]` as required by the project instructions, and stage that file explicitly when applicable.

## 4. Commit after review

Choose a Conventional Commit message that describes the staged changes:

```powershell
git commit -m "<type>: <short description>"
```

Examples of `<type>` include `feat`, `fix`, `docs`, `test`, and `ci`. Read the commit output and confirm it created the expected commit.

## 5. Push and verify

```powershell
git push origin Source_Code
git status --short --branch
git log -1 --oneline
git ls-remote origin refs/heads/Source_Code
```

Confirm the worktree is clean and the remote branch SHA matches the commit you intended to push. If push is rejected, stop and inspect the remote/local state; never force-push to make it succeed.

## Before running

- Read the commands and replace placeholders yourself.
- Review the staged diff before every commit.
- Keep each commit limited to one coherent change.
- GitHub Actions run after a successful push; check their results separately.
