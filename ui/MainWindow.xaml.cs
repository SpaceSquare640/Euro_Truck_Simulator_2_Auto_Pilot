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
    private readonly NavigationState _navigation = new();

    public MainWindow()
    {
        InitializeComponent();
        ThemePicker.SelectedIndex = (int)_preferences.Theme;
        GameFolderText.Text = _preferences.GameFolder ?? string.Empty;
        UpdateFolderStatus();
        ThemeManager.Apply(_preferences.Theme);
        ShowPage(UiPage.Overview);
        SystemParameters.StaticPropertyChanged += SystemParameters_StaticPropertyChanged;
        SystemEvents.UserPreferenceChanged += SystemEvents_UserPreferenceChanged;
        Closed += Window_Closed;
    }

    private void ShowPage(UiPage page)
    {
        _navigation.Navigate(page);
        OverviewPage.Visibility = _navigation.IsSelected(UiPage.Overview) ? Visibility.Visible : Visibility.Collapsed;
        SetupPage.Visibility = _navigation.IsSelected(UiPage.Setup) ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = _navigation.IsSelected(UiPage.Settings) ? Visibility.Visible : Visibility.Collapsed;
        ActivityPage.Visibility = _navigation.IsSelected(UiPage.Activity) ? Visibility.Visible : Visibility.Collapsed;

        foreach (var (button, buttonPage) in new[]
        {
            (OverviewNav, UiPage.Overview), (SetupNav, UiPage.Setup),
            (SettingsNav, UiPage.Settings), (ActivityNav, UiPage.Activity)
        })
        {
            bool selected = _navigation.IsSelected(buttonPage);
            button.SetResourceReference(BackgroundProperty, selected ? "SelectedBrush" : "NavigationBrush");
            button.SetResourceReference(ForegroundProperty, selected ? "SelectedTextBrush" : "PrimaryTextBrush");
            AutomationProperties.SetItemStatus(button, selected ? Strings.CurrentPage : string.Empty);
        }
        if (!IsLoaded) return;
        PageScroll.ScrollToTop();
        (page switch
        {
            UiPage.Setup => SetupHeading,
            UiPage.Settings => SettingsHeading,
            UiPage.Activity => ActivityHeading,
            _ => OverviewHeading
        }).Focus();
    }

    private void Overview_Click(object sender, RoutedEventArgs e) => ShowPage(UiPage.Overview);
    private void Setup_Click(object sender, RoutedEventArgs e) => ShowPage(UiPage.Setup);
    private void Settings_Click(object sender, RoutedEventArgs e) => ShowPage(UiPage.Settings);
    private void Activity_Click(object sender, RoutedEventArgs e) => ShowPage(UiPage.Activity);

    private void Window_PreviewKeyDown(object sender, KeyEventArgs e)
    {
        UiPage? page = NavigationState.FromShortcut(e.Key, Keyboard.Modifiers);
        if (page is null) return;
        ShowPage(page.Value);
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
