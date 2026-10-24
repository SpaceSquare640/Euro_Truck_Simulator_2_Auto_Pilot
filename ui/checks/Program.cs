// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Collections;
using System.Globalization;
using System.Reflection;
using System.Resources;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using ETS2AP.Ui;
using ETS2AP.Ui.Resources;

internal static class Program
{
    [STAThread]
    private static int Main()
    {
        (string Name, Action Run)[] checks =
        [
            ("Localized resources", CheckResources),
            ("Theme resources and contrast", CheckThemes),
            ("Preferences parsing and round trip", CheckPreferences),
            ("Navigation state and shortcuts", CheckNavigation)
        ];

        int failed = 0;
        foreach (var (name, run) in checks)
        {
            try
            {
                run();
                Console.WriteLine($"PASS {name}");
            }
            catch (Exception error)
            {
                failed++;
                Console.Error.WriteLine($"FAIL {name}: {error.Message}");
            }
        }
        Console.WriteLine($"UI checks: {checks.Length - failed} passed, {failed} failed");
        return failed == 0 ? 0 : 1;
    }

    private static void CheckResources()
    {
        var manager = new ResourceManager("ETS2AP.Ui.Resources.Strings", typeof(Strings).Assembly);
        ResourceSet resources = manager.GetResourceSet(CultureInfo.InvariantCulture, true, true)
            ?? throw new InvalidOperationException("Default resource set is missing.");
        var keys = resources.Cast<DictionaryEntry>()
            .Select(entry => (string)entry.Key)
            .ToHashSet(StringComparer.Ordinal);
        var properties = typeof(Strings).GetProperties(BindingFlags.Public | BindingFlags.Static);
        Require(keys.Count > 0, "No localized strings were found.");
        Require(keys.SetEquals(properties.Select(property => property.Name)), "Resource keys and Strings properties differ.");
        foreach (var property in properties)
        {
            string? value = property.GetValue(null) as string;
            Require(!string.IsNullOrWhiteSpace(value), $"Localized value is empty: {property.Name}");
            Require(value == manager.GetString(property.Name, CultureInfo.InvariantCulture),
                $"Localized property does not match its resource: {property.Name}");
        }
    }

    private static void CheckThemes()
    {
        _ = new Application();
        string[] keys =
        [
            "WindowBrush", "NavigationBrush", "SurfaceBrush", "BorderBrush",
            "PrimaryTextBrush", "SecondaryTextBrush", "AccentBrush", "AccentTextBrush",
            "SelectedBrush", "SelectedTextBrush", "InfoBrush", "InfoTextBrush",
            "DisabledBrush", "DisabledTextBrush"
        ];
        foreach (string theme in new[] { "Light", "Dark", "HighContrast" })
        {
            var uri = new Uri($"/ETS2AP.Ui;component/Themes/{theme}.xaml", UriKind.Relative);
            var resources = Application.LoadComponent(uri) as ResourceDictionary
                ?? throw new InvalidOperationException($"Theme could not load: {theme}");
            foreach (string key in keys)
                Require(resources[key] is SolidColorBrush, $"Theme {theme} has no brush named {key}.");

            if (theme == "HighContrast") continue;
            foreach (var (text, background) in new[]
            {
                ("PrimaryTextBrush", "WindowBrush"),
                ("PrimaryTextBrush", "SurfaceBrush"),
                ("SecondaryTextBrush", "SurfaceBrush"),
                ("SelectedTextBrush", "SelectedBrush"),
                ("InfoTextBrush", "InfoBrush")
            })
            {
                var foreground = ((SolidColorBrush)resources[text]).Color;
                var surface = ((SolidColorBrush)resources[background]).Color;
                Require(Contrast(foreground, surface) >= 4.5,
                    $"Theme {theme} has insufficient contrast for {text} on {background}.");
            }
        }
    }

    private static void CheckPreferences()
    {
        var selected = new UiPreferences { Theme = ThemeChoice.Dark, GameFolder = "sample-game-folder" };
        var restored = UiPreferences.FromJson(selected.ToJson());
        Require(restored.Theme == ThemeChoice.Dark && restored.GameFolder == selected.GameFolder,
            "Preferences did not survive serialization.");
        Require(UiPreferences.FromJson("{invalid").Theme == ThemeChoice.System,
            "Malformed preferences did not fall back to System.");
        Require(UiPreferences.FromJson("{\"Theme\":999}").Theme == ThemeChoice.System,
            "Unknown theme did not fall back to System.");
    }

    private static void CheckNavigation()
    {
        var navigation = new NavigationState();
        Require(navigation.IsSelected(UiPage.Overview), "Overview is not the initial page.");
        foreach (UiPage page in Enum.GetValues<UiPage>())
        {
            navigation.Navigate(page);
            Require(Enum.GetValues<UiPage>().Count(navigation.IsSelected) == 1,
                $"Page {page} did not become the only selected page.");
        }
        foreach (var (key, page) in new[]
        {
            (Key.D1, UiPage.Overview), (Key.D2, UiPage.Setup),
            (Key.D3, UiPage.Settings), (Key.D4, UiPage.Activity),
            (Key.NumPad1, UiPage.Overview), (Key.NumPad4, UiPage.Activity)
        })
            Require(NavigationState.FromShortcut(key, ModifierKeys.Control) == page,
                $"Ctrl+{key} did not select {page}.");
        Require(NavigationState.FromShortcut(Key.D1, ModifierKeys.None) is null,
            "A key without Ctrl unexpectedly navigated.");
        Require(NavigationState.FromShortcut(Key.D5, ModifierKeys.Control) is null,
            "An unassigned shortcut unexpectedly navigated.");
    }

    private static double Contrast(Color first, Color second)
    {
        double a = Luminance(first);
        double b = Luminance(second);
        return (Math.Max(a, b) + 0.05) / (Math.Min(a, b) + 0.05);
    }

    private static double Luminance(Color color)
    {
        static double Linear(byte component)
        {
            double value = component / 255.0;
            return value <= 0.04045 ? value / 12.92 : Math.Pow((value + 0.055) / 1.055, 2.4);
        }
        return 0.2126 * Linear(color.R) + 0.7152 * Linear(color.G) + 0.0722 * Linear(color.B);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
