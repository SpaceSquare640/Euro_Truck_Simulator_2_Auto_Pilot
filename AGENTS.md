# AGENTS.md — Source Code Branch

Engineering rules for AI agents (OpenAI Codex) working on the `Source_Code` branch.
These rules add to, and never override, higher-priority instructions.

## Scope
- Work only inside this worktree / the `Source_Code` branch.
- Never modify the website branch. Website-related requests go to the project lead (Claude) for coordination.
- Never read, reference, copy or commit anything outside this worktree.

## Project Facts
- Platform: Windows 10/11 only.
- License: CC BY-NC-SA 4.0, copyright Kingsley. Source-available, not open source.
- Language of code, comments, docs, commits: English.
- User-facing text must be localizable (see i18n).

## Stack & Layout
| Path | Language | Responsibility |
|---|---|---|
| `native/` | C++ (MSVC, CMake) | SCS Telemetry SDK plugin (DLL), input injection |
| `python/` | Python 3 | Perception, planning, control |
| `ui/` | C# / .NET 10 WPF | Desktop UI |
| `shared/` | Spec files | IPC contract shared by all modules |
| `docs/` | Markdown | Architecture and developer docs |

See `docs/ARCHITECTURE.md`. Modules talk only through the versioned IPC contract.

## Build & Test
- CI on GitHub Actions (`windows-latest`) is the source of truth for build, test, lint.
- Do not keep long-lived build caches locally; build output goes to `build/` (git-ignored).

## Commit Rules
- Commit directly to `Source_Code` (no PR gate). Every push is checked by CI; commits are reviewed after the fact.
- Before each commit:
  1. Format / lint changed files.
  2. Confirm no secrets, keys, tokens or confidential paths are staged (`git diff --cached`).
  3. Update `Change Log.md` under `[Unreleased]`.
- Use Conventional Commits: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `ci:`, `chore:`.

## Dependencies
- Allowed licenses: MIT, BSD-2/3-Clause, Apache-2.0, Zlib, ISC.
- Forbidden: GPL, LGPL, AGPL, and anything restricting non-commercial ShareAlike redistribution. Ask before adding any other license.

## i18n
- No hard-coded user-facing strings. WPF: `.resx` resources (English default). Python: message catalog keys.

## Assets
- App images, icons and Social Preview are produced by Codex and stored in `assets/`.

## Signature
- Add `Edited by: OpenAI-Codex` in a comment or Change Log entry for files you create or modify.

<!-- Edited by: Anthropic-Claude_Code -->
