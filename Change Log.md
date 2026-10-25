# Change Log

All notable changes to this project are documented here and published on the project website.

## [Unreleased]

### Added
- Windows x64 release packaging for the WPF app and native connector, with equivalent setup EXE/MSI choices, a versioned connector ZIP, and CI install/uninstall smoke test. (OpenAI-Codex_Responsible: Source_Code)
- Setup can download the published connector ZIP from GitHub, verify the release and DLL digests, and install supported DLLs into the selected ETS2 plugins folder with an elevation prompt and backups. (OpenAI-Codex_Responsible: Source_Code)
- One-command Phase 0 in-game steering probe with continuous parked-state checks for simple automatic transmission, signed pulse verification, neutral-output acknowledgement, and offline self-test. (OpenAI-Codex_Responsible: Source_Code)
- Phase 0 probe accepts an explicit output directory and uses fresh telemetry rather than process enumeration to validate game/plugin activity. (OpenAI-Codex_Responsible: Source_Code)
- Development Python UI status service and WPF named-pipe status display with bounded framing, peer-image checks, and offline protocol checks; engagement remains disabled until native control integration. (OpenAI-Codex_Responsible: Source_Code)
- Headless WPF UI checks for localized resources, theme resources and contrast, preferences, and navigation state; CI runs them explicitly. (OpenAI-Codex_Responsible: Source_Code)
- Initial .NET 10 WPF desktop UI preview with Overview, Setup, Settings, and Activity screens; localizable English resources, theme preferences, high contrast support, and keyboard navigation. (OpenAI-Codex_Responsible: Source_Code)
- Refined desktop navigation focus and accessibility labels, and fixed the sidebar footer placement. (OpenAI-Codex_Responsible: Source_Code)
- Repository initialized: `.gitignore`, README, LICENSE (CC BY-NC-SA 4.0), NOTICE. (Anthropic-Claude_Code)
- Development guidelines: `AGENTS.md`, `CONTRIBUTING.md`, `docs/ARCHITECTURE.md`, task issue template, `.gitattributes`. (Anthropic-Claude_Code)
- GitHub Actions: secret scan (gitleaks), CI (C++ / Python / .NET 10, auto-skip until modules exist), dependency license check, Change Log sync to website, release skeleton; Dependabot for Actions. (Anthropic-Claude_Code)
- Versioned v1 IPC specifications for telemetry, control, and UI, including the UI JSON Schema. (OpenAI-Codex_Responsible: Source_Code)
- Dependabot npm updates targeting the website branch; major updates of typescript and astro are ignored. (OpenAI-Codex_Responsible: Source_Code)
- Release notes are built from the matching Change Log.md section with a fixed contributors block, a comparison link, and a preview-only manual run; extraction and validation scripts with local tests; Actionlint workflow on Source_Code. (OpenAI-Codex_Responsible: Source_Code)
- Phase 0 feasibility spike for the SCS SDK Input API (test plugin with scripted input injection, telemetry and timing logs, and an offline host harness) and the vendored SCS SDK 1.16 headers (MIT) in `third_party/scs_sdk`. (OpenAI-Codex_Responsible: Source_Code)
- Native telemetry writer: `telemetry_core` library (layout, conversions, validity groups, moving_backward hysteresis, seqlock, Windows shared-memory owner), the SCS SDK plugin glue exporting `scs_telemetry_init` and `scs_telemetry_shutdown`, a minimal in-repo test framework, and writer-to-reader round-trip tests. (OpenAI-Codex_Responsible: Source_Code)

### Changed
- Create a per-machine advertised Start Menu shortcut from the installed UI executable, avoiding mixed-scope shortcut resources. (OpenAI-Codex_Responsible: Source_Code)
- Fixed PowerShell interpolation of non-ASCII license characters so release packaging succeeds under StrictMode. (OpenAI-Codex_Responsible: Source_Code)
- License Check now audits .NET packages and uses exact-version MS-RL overrides for the two approved CI-only WiX packages whose NuGet metadata lacks SPDX expressions. (OpenAI-Codex_Responsible: Source_Code)
- Connector installation locks the selected local directory chain, rejects reparse points, verifies persistent backups, and restores through a fresh temporary file. (OpenAI-Codex_Responsible: Source_Code)
- Enabled Python CI for the existing package and tests without installing unused development dependencies. (OpenAI-Codex_Responsible: Source_Code)
- Hardened the Phase 0 Spike by retaining semantic input descriptors for the device lifetime and logging CSV write failures before disabling the affected stream. (OpenAI-Codex_Responsible: Source_Code)
- Changed the Phase 0 Spike default to the SCS semantical input device, which maps configured game mixes without a player binding UI; mix support still requires verification per ETS2 profile. (OpenAI-Codex_Responsible: Source_Code)
- Restored the generic `ETS2AP Spike` device alongside the no-bind semantic device by default, so existing ETS2 profiles recognize the assigned device while semantic input remains available. (OpenAI-Codex_Responsible: Source_Code)
- Diagnosed the Phase 0 in-game generic steering probe: axis binding is required, and the tested profile maps positive Spike steering to negative game steering. The current source reverses the generic virtual-axis value, identifies polarity mismatches, and steps the ramp to limit the observed callback surge; these fixes still require game verification. (OpenAI-Codex_Responsible: Source_Code)
- Routed Phase 0 semantical steering, forward, and backward float inputs to their corresponding scripted values and recorded actual steering-event emission; game-side mix names remain unverified. (OpenAI-Codex_Responsible: Source_Code)
- Retained the SDK log callback by value so telemetry callbacks do not access initialization parameters after init. (OpenAI-Codex_Responsible: Source_Code)
- Agent signature in `AGENTS.md` set to `OpenAI-Codex_Responsible: Source_Code`; reporting format added. (Anthropic-Claude_Code)
- Linked the architecture IPC table to the shared contract specifications. (OpenAI-Codex_Responsible: Source_Code)
- Applied verified SDK channel names and coordinate conventions to the telemetry contract; steering is positive left. (OpenAI-Codex_Responsible: Source_Code)
- Hardened IPC specifications for seqlock ordering, 4096-byte mapping ownership and latest-callback validity, writer restart, overlapped pipe I/O and peer validation, control status state machine, UI parsing and state_revision, and moving_backward hysteresis. (OpenAI-Codex_Responsible: Source_Code)
- Removed MPL-2.0 from the dependency license-check allow-list. (OpenAI-Codex_Responsible: Source_Code)
- Release and Sync Docs workflows use least-privilege permissions; Change Log.md is validated before it is synced to the website; releases are restricted to tags on Source_Code; `generate_release_notes` is no longer used. (OpenAI-Codex_Responsible: Source_Code)
- CI module detection no longer uses `ls | grep` and writes its outputs in one block, which fixes the Actionlint shellcheck findings. (OpenAI-Codex_Responsible: Source_Code)

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
