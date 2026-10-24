// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Windows;
using Microsoft.Win32;

namespace ETS2AP.Ui;

internal enum ThemeChoice { System, Light, Dark }

internal static class ThemeManager
{
    private static string? _activeTheme;

    public static void Apply(ThemeChoice choice)
    {
        string theme = SystemParameters.HighContrast ? "HighContrast" : choice switch
        {
            ThemeChoice.Light => "Light",
            ThemeChoice.Dark => "Dark",
            _ => IsSystemDark() ? "Dark" : "Light"
        };
        if (_activeTheme == theme) return;
        var dictionaries = Application.Current.Resources.MergedDictionaries;
        dictionaries.Clear();
        dictionaries.Add(new ResourceDictionary
        {
            Source = new Uri($"Themes/{theme}.xaml", UriKind.Relative)
        });
        _activeTheme = theme;
    }

    private static bool IsSystemDark()
    {
        object? value = Registry.GetValue(
            @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
            "AppsUseLightTheme", 1);
        return value is int setting && setting == 0;
    }
}
