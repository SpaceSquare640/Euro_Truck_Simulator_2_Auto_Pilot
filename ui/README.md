<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Desktop UI preview

This is the .NET 10 WPF shell for the external ETS2 Auto Pilot app. It provides Overview, Setup, Settings, and Activity screens; Windows theme following with light, dark, and high contrast palettes; keyboard navigation (Ctrl+1 through Ctrl+4); and local theme and game-folder preferences.

All displayed text is loaded from the English `Resources/Strings.resx` resource. With a verified Python UI status service, the Overview shows pipe connection state and fresh speed, speed limit, and remaining distance; unknown or stale values show `--`. ETA remains unknown because UI v1 does not carry navigation time. Auto pilot engagement stays disabled because native control status is not integrated. On the Setup page, select the game folder and choose Install connector. The elevated installer downloads the matching `ETS2AP_DLL_V<version>.zip` asset from a published GitHub Release, verifies the release SHA-256 digest and ZIP manifest in memory, then copies the supported plugin DLLs into `bin/win_x64/plugins`. Existing same-name DLLs are backed up in a sibling `ETS2AP_Backups_*` folder. During installation it holds no-delete handles on the selected local directory chain and rejects junctions or other reparse points. The installer does not edit ETS2 control bindings; automatic binding and native control remain unfinished.

For a development connection, start the [Python status service](../python/README.md) with its trusted UI executable path, then launch this UI with `--trusted-python-image <absolute path to the running python.exe>`. Without that explicit path, the UI remains offline. The application never launches a Python interpreter implicitly.

Build locally with `dotnet build ui/ETS2AP.Ui.slnx -c Release`. No third-party packages are required.

Run the headless CI checks with `dotnet run --project ui/checks/ETS2AP.Ui.Checks.csproj -c Release`. They verify English resources, theme resource loading and light/dark text contrast, preference serialization and fallback, navigation state, and UI status parsing. A separate `--ipc-smoke <absolute python.exe path>` mode checks a running Python service. These checks do not validate rendered layout or game integration.
