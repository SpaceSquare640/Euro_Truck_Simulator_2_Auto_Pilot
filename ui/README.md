<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Desktop UI preview

This is the .NET 10 WPF shell for the external ETS2 Auto Pilot app. It provides Overview, Setup, Settings, and Activity screens; Windows theme following with light, dark, and high contrast palettes; keyboard navigation (Ctrl+1 through Ctrl+4); and local theme and game-folder preferences.

All displayed text is loaded from the English `Resources/Strings.resx` resource. The UI shows unknown telemetry as `--` and keeps auto pilot engagement disabled until the Python control service and IPC integration are available. Selecting a game folder only checks for `bin/win_x64/eurotrucks2.exe`; this preview does not modify the game installation or install a connector.

Build locally with `dotnet build ui/ETS2AP.Ui.slnx -c Release`. No third-party packages are required.

Run the headless CI checks with `dotnet run --project ui/checks/ETS2AP.Ui.Checks.csproj -c Release`. They verify English resources, theme resource loading and light/dark text contrast, preference serialization and fallback, and navigation state. These checks do not validate rendered layout or game integration.
