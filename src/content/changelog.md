# Change Log

All notable changes to this project are documented here and published on the project website.

## [Unreleased]

### Added
- Repository initialized: `.gitignore`, README, LICENSE (CC BY-NC-SA 4.0), NOTICE. (Anthropic-Claude_Code)
- Development guidelines: `AGENTS.md`, `CONTRIBUTING.md`, `docs/ARCHITECTURE.md`, task issue template, `.gitattributes`. (Anthropic-Claude_Code)
- GitHub Actions: secret scan (gitleaks), CI (C++ / Python / .NET 10, auto-skip until modules exist), dependency license check, Change Log sync to website, release skeleton; Dependabot for Actions. (Anthropic-Claude_Code)
- Versioned v1 IPC specifications for telemetry, control, and UI, including the UI JSON Schema. (OpenAI-Codex_Responsible: Source_Code)
- Dependabot npm updates targeting the website branch; major updates of typescript and astro are ignored. (OpenAI-Codex_Responsible: Source_Code)
- Release notes are built from the matching Change Log.md section with a fixed contributors block, a comparison link, and a preview-only manual run; extraction and validation scripts with local tests; Actionlint workflow on Source_Code. (OpenAI-Codex_Responsible: Source_Code)
- Phase 0 feasibility spike for the SCS SDK Input API (test plugin with scripted input injection, telemetry and timing logs, and an offline host harness) and the vendored SCS SDK 1.16 headers (MIT) in `third_party/scs_sdk`. (OpenAI-Codex_Responsible: Source_Code)
- Native telemetry writer: `telemetry_core` library (layout, conversions, validity groups, moving_backward hysteresis, seqlock, Windows shared-memory owner), the SCS SDK plugin glue exporting `scs_telemetry_init` and `scs_telemetry_shutdown`, a minimal in-repo test framework, and writer-to-reader round-trip tests. (OpenAI-Codex_Responsible: Source_Code)

### Changed
- Retained the SDK log callback by value so telemetry callbacks do not access initialization parameters after init. (OpenAI-Codex_Responsible: Source_Code)
- Agent signature in `AGENTS.md` set to `OpenAI-Codex_Responsible: Source_Code`; reporting format added. (Anthropic-Claude_Code)
- Linked the architecture IPC table to the shared contract specifications. (OpenAI-Codex_Responsible: Source_Code)
- Applied verified SDK channel names and coordinate conventions to the telemetry contract; steering is positive left. (OpenAI-Codex_Responsible: Source_Code)
- Hardened IPC specifications for seqlock ordering, 4096-byte mapping ownership and latest-callback validity, writer restart, overlapped pipe I/O and peer validation, control status state machine, UI parsing and state_revision, and moving_backward hysteresis. (OpenAI-Codex_Responsible: Source_Code)
- Removed MPL-2.0 from the dependency license-check allow-list. (OpenAI-Codex_Responsible: Source_Code)
- Release and Sync Docs workflows use least-privilege permissions; Change Log.md is validated before it is synced to the website; releases are restricted to tags on Source_Code; `generate_release_notes` is no longer used. (OpenAI-Codex_Responsible: Source_Code)
- CI module detection no longer uses `ls | grep` and writes its outputs in one block, which fixes the Actionlint shellcheck findings. (OpenAI-Codex_Responsible: Source_Code)

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
