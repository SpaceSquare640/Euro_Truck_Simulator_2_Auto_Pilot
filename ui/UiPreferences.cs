// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.IO;
using System.Text.Json;

namespace ETS2AP.Ui;

internal sealed class UiPreferences
{
    public ThemeChoice Theme { get; set; } = ThemeChoice.System;
    public string? GameFolder { get; set; }

    private static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ETS2AP", "ui-settings.json");

    public static UiPreferences Load()
    {
        try
        {
            if (File.Exists(SettingsPath))
                return FromJson(File.ReadAllText(SettingsPath));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // A damaged or inaccessible settings file must not prevent the UI from opening.
        }
        return new UiPreferences();
    }

    internal static UiPreferences FromJson(string json)
    {
        try
        {
            var preferences = JsonSerializer.Deserialize<UiPreferences>(json) ?? new UiPreferences();
            if (!Enum.IsDefined(preferences.Theme)) preferences.Theme = ThemeChoice.System;
            return preferences;
        }
        catch (JsonException)
        {
            return new UiPreferences();
        }
    }

    internal string ToJson() => JsonSerializer.Serialize(this);

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(SettingsPath, ToJson());
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Preferences are optional; the current UI session remains usable.
        }
    }
}
