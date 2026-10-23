# Contributing

Thank you for your interest. Please read [LICENSE](LICENSE) and [NOTICE](NOTICE) first:
this project is source-available under CC BY-NC-SA 4.0 and may not be used commercially.

## Branches
| Branch | Purpose |
|---|---|
| `Source_Code` | Application source (default branch) |
| `Euro_Truck_Simulator_2_Auto_Pilot_Website` | Project website (GitHub Pages), separate history |

## Workflow
- Changes are committed directly to the branch (no pull-request gate).
- Every push runs CI: build, tests, lint, secret scan and dependency license check.
- Commits are reviewed after push; problems are fixed with follow-up commits.
- Change Log.md is validated before it is synced to the website.

## Commit Messages
[Conventional Commits](https://www.conventionalcommits.org/): `type(scope): summary`

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `ci`, `chore`. Scopes: `native`, `python`, `ui`, `shared`, `docs`.

## Change Log
Every user-visible change adds an entry to `Change Log.md` under `[Unreleased]`
(Added / Changed / Fixed / Removed). The Change Log is published on the website automatically.

## Releasing
1. Make sure CI on Source_Code is green.
2. In Change Log.md, rename `## [Unreleased]` to `## [x.y.z] - YYYY-MM-DD` and add a new empty `## [Unreleased]` above it.
3. Commit with the message `docs: release x.y.z` and push.
4. Optionally preview with Actions → Release → Run workflow, using `Unreleased` or `x.y.z`. A manual run only renders the notes in the job summary and never publishes.
5. Create an annotated tag `vX.Y.Z` on that commit and push the tag. Only tags whose commit is on Source_Code are published.
6. The Release workflow extracts the matching section, appends the contributors block and comparison link, then publishes. If the section is missing, empty, or has a malformed heading, the workflow fails and nothing is published.
7. Tags and releases are created by the maintainer. Deleting or rewriting a tag is destructive and requires the maintainer's explicit approval.

## Dependency Licenses
Allowed: MIT, BSD-2/3-Clause, Apache-2.0, Zlib, ISC. GPL-family licenses are not allowed.

## Tasks
Planned work is tracked as GitHub Issues using the **Task** template.

<!-- Edited by: Anthropic-Claude_Code -->
<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
