// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using ETS2AP.Ui.Resources;
using Microsoft.Win32;

namespace ETS2AP.Ui;

public partial class MainWindow : Window
{
    private readonly UiPreferences _preferences = UiPreferences.Load();

    public MainWindow()
    {
        InitializeComponent();
        ThemePicker.SelectedIndex = (int)_preferences.Theme;
        GameFolderText.Text = _preferences.GameFolder ?? string.Empty;
        UpdateFolderStatus();
        ThemeManager.Apply(_preferences.Theme);
        ShowPage("Overview");
        SystemParameters.StaticPropertyChanged += SystemParameters_StaticPropertyChanged;
        SystemEvents.UserPreferenceChanged += SystemEvents_UserPreferenceChanged;
        Closed += Window_Closed;
    }

    private void ShowPage(string page)
    {
        OverviewPage.Visibility = page == "Overview" ? Visibility.Visible : Visibility.Collapsed;
        SetupPage.Visibility = page == "Setup" ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = page == "Settings" ? Visibility.Visible : Visibility.Collapsed;
        ActivityPage.Visibility = page == "Activity" ? Visibility.Visible : Visibility.Collapsed;

        foreach (Button button in new[] { OverviewNav, SetupNav, SettingsNav, ActivityNav })
        {
            bool selected = button.Name.StartsWith(page, StringComparison.Ordinal);
            button.SetResourceReference(BackgroundProperty, selected ? "SelectedBrush" : "NavigationBrush");
            button.SetResourceReference(ForegroundProperty, selected ? "SelectedTextBrush" : "PrimaryTextBrush");
            AutomationProperties.SetItemStatus(button, selected ? Strings.CurrentPage : string.Empty);
        }
        if (!IsLoaded) return;
        PageScroll.ScrollToTop();
        (page switch
        {
            "Setup" => SetupHeading,
            "Settings" => SettingsHeading,
            "Activity" => ActivityHeading,
            _ => OverviewHeading
        }).Focus();
    }

    private void Overview_Click(object sender, RoutedEventArgs e) => ShowPage("Overview");
    private void Setup_Click(object sender, RoutedEventArgs e) => ShowPage("Setup");
    private void Settings_Click(object sender, RoutedEventArgs e) => ShowPage("Settings");
    private void Activity_Click(object sender, RoutedEventArgs e) => ShowPage("Activity");

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (Keyboard.Modifiers != ModifierKeys.Control) return;
        string? page = e.Key switch
        {
            Key.D1 or Key.NumPad1 => "Overview",
            Key.D2 or Key.NumPad2 => "Setup",
            Key.D3 or Key.NumPad3 => "Settings",
            Key.D4 or Key.NumPad4 => "Activity",
            _ => null
        };
        if (page is null) return;
        ShowPage(page);
        e.Handled = true;
    }

    private void Window_Activated(object sender, EventArgs e)
    {
        ThemeManager.Apply(_preferences.Theme);
    }

    private void SystemParameters_StaticPropertyChanged(object? sender, PropertyChangedEventArgs e) => QueueThemeUpdate();
    private void SystemEvents_UserPreferenceChanged(object sender, UserPreferenceChangedEventArgs e) => QueueThemeUpdate();

    private void QueueThemeUpdate()
    {
        if (Dispatcher.HasShutdownStarted) return;
        _ = Dispatcher.BeginInvoke(new Action(() => ThemeManager.Apply(_preferences.Theme)));
    }

    private void Window_Closed(object? sender, EventArgs e)
    {
        SystemParameters.StaticPropertyChanged -= SystemParameters_StaticPropertyChanged;
        SystemEvents.UserPreferenceChanged -= SystemEvents_UserPreferenceChanged;
    }

    private void Browse_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFolderDialog { Title = Strings.GameFolder };
        if (dialog.ShowDialog(this) != true) return;
        GameFolderText.Text = dialog.FolderName;
        _preferences.GameFolder = dialog.FolderName;
        _preferences.Save();
        UpdateFolderStatus();
    }

    private void UpdateFolderStatus()
    {
        if (string.IsNullOrWhiteSpace(GameFolderText.Text))
        {
            FolderStatus.Text = Strings.NoFolderSelected;
            return;
        }
        string executable = Path.Combine(GameFolderText.Text, "bin", "win_x64", "eurotrucks2.exe");
        FolderStatus.Text = File.Exists(executable) ? Strings.FolderAccepted : Strings.FolderInvalid;
    }

    private void ThemePicker_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (!IsLoaded || ThemePicker.SelectedIndex < 0) return;
        _preferences.Theme = (ThemeChoice)ThemePicker.SelectedIndex;
        ThemeManager.Apply(_preferences.Theme);
        _preferences.Save();
    }

    private void CopyStatus_Click(object sender, RoutedEventArgs e)
    {
        Clipboard.SetText(Strings.NotConnected);
        CopyFeedback.Text = Strings.StatusCopied;
    }
}
