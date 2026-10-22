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

## Commit Messages
[Conventional Commits](https://www.conventionalcommits.org/): `type(scope): summary`

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `ci`, `chore`. Scopes: `native`, `python`, `ui`, `shared`, `docs`.

## Change Log
Every user-visible change adds an entry to `Change Log.md` under `[Unreleased]`
(Added / Changed / Fixed / Removed). The Change Log is published on the website automatically.

## Dependency Licenses
Allowed: MIT, BSD-2/3-Clause, Apache-2.0, Zlib, ISC. GPL-family licenses are not allowed.

## Tasks
Planned work is tracked as GitHub Issues using the **Task** template.

<!-- Edited by: Anthropic-Claude_Code -->
