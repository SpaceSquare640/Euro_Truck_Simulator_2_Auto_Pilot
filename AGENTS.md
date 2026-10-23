# AGENTS.md — Website Branch

Engineering rules for AI agents (OpenAI Codex) working on the `Euro_Truck_Simulator_2_Auto_Pilot_Website` branch.
These rules add to, and never override, higher-priority instructions.

## Scope
- Work only inside this worktree / the website branch.
- Never modify the `Source_Code` branch. Source-related requests go to the project lead (Claude) for coordination.
- Content may only come from public sources: this branch and the `Source_Code` branch.

## Purpose
The website lets users understand the whole project through visuals, real-time interaction
and an in-browser simulator. All project READMEs link here.

## Stack
- Astro + TypeScript, static output, deployed to GitHub Pages by GitHub Actions.
- Interactive parts (simulator, demos) as Astro islands; keep pages fast without JavaScript where possible.

## Pages
| Section | Content source |
|---|---|
| Home | Project overview, visuals |
| Docs / README | Written here |
| Change Log | Auto-synced from `Source_Code/Change Log.md` — do not edit the synced copy by hand |
| Wiki | Written here (Markdown) |
| Simulator | Interactive demo of the app's behavior |

## i18n
- English is the default and source language.
- No hard-coded UI strings; put them in `src/i18n/<locale>.json`. Use Astro i18n routing (`/en/`, later others).

## Assets
- Images, icons and Social Preview are produced by Codex and stored under `public/` or `src/assets/`.
- Optimize images (WebP/AVIF) and keep each under 500 KB where possible.

## Accessibility
- Semantic HTML, alt text for every image, keyboard-navigable interactive elements, sufficient color contrast.

## Commit Rules
- Commit directly to this branch (no PR gate). Every push runs CI; commits are reviewed after the fact.
- Before each commit: build locally if changed, confirm no secrets or confidential paths are staged.
- Conventional Commits: `feat:`, `fix:`, `docs:`, `style:`, `ci:`, `chore:`.
- License: CC BY-NC-SA 4.0, copyright Kingsley.

## Dependency Policy
- Runtime dependencies must use one of the licenses in the `allowed` list in [scripts/license-policy.json](scripts/license-policy.json).
- Build-time-only dev dependencies may use a non-allowlisted license only through an exact package-name and license-string exception in [scripts/license-policy.json](scripts/license-policy.json). They must remain unmodified and must not be distributed.
- Keep the exception list explicit. Do not broaden the allowlist or exceptions to make a failed check pass.

### Dependency Notes
- Keep Astro on the 7.x line; the pinned project version is 7.4.2.
- Keep TypeScript on the 6.x line; `@astrojs/check` 0.9.12 declares a TypeScript peer range of `^5 || ^6`, so TypeScript 7.x is incompatible.

## Signature
- Add `Edited by: OpenAI-Codex_Responsible: Website` in a comment for files you create or modify.

## Reporting
- Every report must be a single copyable ```text block starting with `[REPORT] OpenAI-Codex_Responsible: Website`, with fields: Task, Status, Changed files, Commits, Skills used, Verification, Issues / Questions.
- Status may be `Done` only after every acceptance condition has been verified. Include actual numeric check outputs, including the dependency count and exception counts.

<!-- Edited by: OpenAI-Codex_Responsible: Website -->
<!-- Edited by: Anthropic-Claude_Code -->
