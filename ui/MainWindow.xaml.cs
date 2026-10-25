// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Input;
using ETS2AP.Ui.Resources;
using Microsoft.Win32;

namespace ETS2AP.Ui;

public partial class MainWindow : Window
{
    private readonly UiPreferences _preferences = UiPreferences.Load();
    private readonly NavigationState _navigation = new();
    private readonly CancellationTokenSource _serviceCancellation = new();
    private UiPipeClient? _service;
    private bool _installingConnector;

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
        StartServiceConnection();
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
        _serviceCancellation.Cancel();
        _serviceCancellation.Dispose();
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

    private async void InstallConnector_Click(object sender, RoutedEventArgs e)
    {
        string gameFolder = GameFolderText.Text;
        if (!File.Exists(Path.Combine(gameFolder, "bin", "win_x64", "eurotrucks2.exe")))
        {
            InstallStatus.Text = Strings.FolderInvalid;
            UpdateFolderStatus();
            return;
        }
        if (ConnectorInstaller.IsGameRunning())
        {
            InstallStatus.Text = Strings.ConnectorGameMustBeClosed;
            return;
        }

        _installingConnector = true;
        InstallConnectorButton.IsEnabled = false;
        try
        {
            InstallStatus.Text = Strings.ConnectorInstallElevationPrompt;
            int result = await ConnectorInstaller.LaunchElevatedAsync(gameFolder, _serviceCancellation.Token);
            InstallStatus.Text = result switch
            {
                0 => Strings.ConnectorInstallSuccess,
                ConnectorInstaller.GameRunningExitCode => Strings.ConnectorGameMustBeClosed,
                _ => Strings.ConnectorInstallFailed
            };
        }
        catch (Win32Exception error) when (error.NativeErrorCode == 1223)
        {
            InstallStatus.Text = Strings.ConnectorInstallCancelled;
        }
        catch (OperationCanceledException)
        {
            InstallStatus.Text = Strings.ConnectorInstallCancelled;
        }
        catch (Exception error) when (error is HttpRequestException or IOException or UnauthorizedAccessException or
            ArgumentException or CryptographicException or InvalidOperationException or Win32Exception)
        {
            InstallStatus.Text = Strings.ConnectorInstallFailed;
        }
        finally
        {
            _installingConnector = false;
            UpdateFolderStatus();
        }
    }

    private void UpdateFolderStatus()
    {
        if (string.IsNullOrWhiteSpace(GameFolderText.Text))
        {
            FolderStatus.Text = Strings.NoFolderSelected;
            InstallConnectorButton.IsEnabled = false;
            return;
        }
        string executable = Path.Combine(GameFolderText.Text, "bin", "win_x64", "eurotrucks2.exe");
        FolderStatus.Text = File.Exists(executable) ? Strings.FolderAccepted : Strings.FolderInvalid;
        InstallConnectorButton.IsEnabled = !_installingConnector && File.Exists(executable);
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
        Clipboard.SetText($"{ConnectionTitle.Text}: {ConnectionDetail.Text}");
        CopyFeedback.Text = Strings.StatusCopied;
    }

    private void StartServiceConnection()
    {
        string[] args = Environment.GetCommandLineArgs();
        int index = Array.IndexOf(args, "--trusted-python-image");
        if (index < 0 || index + 1 >= args.Length) return;
        try
        {
            _service = new UiPipeClient(args[index + 1]);
            _service.StatusChanged += status =>
            {
                if (!Dispatcher.HasShutdownStarted)
                    _ = Dispatcher.BeginInvoke(new Action(() => ApplyStatus(status)));
            };
            _ = _service.RunAsync(_serviceCancellation.Token);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException)
        {
            ApplyStatus(null);
        }
    }

    private void ApplyStatus(UiStatus? status)
    {
        if (status is null)
        {
            ConnectionTitle.Text = Strings.NotConnected;
            UpdateConnectionDetail(Strings.NotConnectedDetail);
            SpeedValue.Text = SpeedLimitValue.Text = DistanceValue.Text = "--";
            GameCheck.Text = ConnectorCheck.Text = ControlCheck.Text = Strings.Pending;
            return;
        }

        ConnectionTitle.Text = Strings.ServiceConnected;
        UpdateConnectionDetail(ErrorDescription(status.ErrorCode));
        bool fresh = status.AgeS is <= 0.25 && status.SpeedMps is not null;
        SpeedValue.Text = fresh ? FormatKmh(status.SpeedMps) : "--";
        SpeedLimitValue.Text = fresh ? FormatKmh(status.SpeedLimitMps) : "--";
        DistanceValue.Text = fresh && status.DistanceM is double distance
            ? string.Format(CultureInfo.CurrentCulture, Strings.DistanceKmFormat, distance / 1000.0)
            : "--";
        GameCheck.Text = fresh ? Strings.Available : Strings.Pending;
        ConnectorCheck.Text = fresh ? Strings.Available : Strings.Pending;
        ControlCheck.Text = status.ErrorCode == "CONTROL_UNAVAILABLE" ? Strings.Unavailable : Strings.Pending;
    }

    private static string FormatKmh(double? speedMps) => speedMps is double speed
        ? Math.Round(speed * 3.6, MidpointRounding.AwayFromZero).ToString("N0", CultureInfo.CurrentCulture)
        : "--";

    private void UpdateConnectionDetail(string detail)
    {
        if (ConnectionDetail.Text == detail) return;
        ConnectionDetail.Text = detail;
        var peer = UIElementAutomationPeer.FromElement(ConnectionDetail)
            ?? UIElementAutomationPeer.CreatePeerForElement(ConnectionDetail);
        peer?.RaiseAutomationEvent(AutomationEvents.LiveRegionChanged);
    }

    private static string ErrorDescription(string? code) => code switch
    {
        "GAME_NOT_RUNNING" => Strings.ErrorGameNotRunning,
        "TELEMETRY_UNAVAILABLE" => Strings.ErrorTelemetryUnavailable,
        "TELEMETRY_STALE" => Strings.ErrorTelemetryStale,
        "CONTROL_UNAVAILABLE" => Strings.ErrorControlUnavailable,
        "MANUAL_OVERRIDE" => Strings.ErrorManualOverride,
        "COMMAND_TIMEOUT" => Strings.ErrorCommandTimeout,
        "INVALID_CONFIG" => Strings.ErrorInvalidConfig,
        "UNSUPPORTED_VERSION" => Strings.ErrorUnsupportedVersion,
        "MALFORMED_MESSAGE" => Strings.ErrorMalformedMessage,
        "STALE_STATUS" => Strings.ErrorStaleStatus,
        "INTERNAL_ERROR" => Strings.ErrorInternalError,
        _ => Strings.ServiceStatusUnknown
    };
}
