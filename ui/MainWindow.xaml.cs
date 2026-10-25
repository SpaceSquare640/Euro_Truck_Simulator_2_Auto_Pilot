// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
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
    private readonly ObservableCollection<ActivityItem> _activityEntries = new();
    private readonly ObservableCollection<ActivityItem> _recentActivityEntries = new();
    private UiPipeClient? _service;
    private bool _installingConnector;
    private bool _hasObservedServiceConnection;
    private bool _serviceConnected;
    private bool? _lastTelemetryFresh;
    private string? _lastLoggedState;
    private string? _lastLoggedErrorCode;
    private UiStatus? _latestStatus;

    public MainWindow()
    {
        InitializeComponent();
        ActivityList.ItemsSource = _activityEntries;
        RecentActivityList.ItemsSource = _recentActivityEntries;
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
        InstallProgress.Visibility = Visibility.Visible;
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
            InstallProgress.Visibility = Visibility.Collapsed;
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
        try
        {
            Clipboard.SetText(BuildStatusSnapshot());
            CopyFeedback.Text = Strings.StatusCopied;
        }
        catch (ExternalException)
        {
            CopyFeedback.Text = Strings.StatusCopyFailed;
        }
    }

    private string BuildStatusSnapshot()
    {
        UiStatus? status = _latestStatus;
        string age = status?.AgeS is double seconds
            ? string.Format(CultureInfo.CurrentCulture, Strings.TelemetryAgeFormat, seconds)
            : Strings.NotProvided;
        return string.Join(Environment.NewLine, new[]
        {
            $"{Strings.Connection}: {ConnectionTitle.Text}",
            $"{Strings.Status}: {ConnectionDetail.Text}",
            $"{Strings.AutomationState}: {(status is null ? Strings.OfflineState : StateDescription(status.State))}",
            $"{Strings.TelemetryAge}: {age}",
            $"{Strings.Speed}: {SpeedValue.Text} {Strings.Kmh}",
            $"{Strings.SpeedLimit}: {SpeedLimitValue.Text} {Strings.Kmh}",
            $"{Strings.DistanceRemaining}: {DistanceValue.Text}"
        });
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
        _latestStatus = status;
        if (status is null)
        {
            RecordStatus(null, telemetryFresh: false);
            ConnectionTitle.Text = Strings.NotConnected;
            UpdateConnectionDetail(Strings.NotConnectedDetail);
            SpeedValue.Text = SpeedLimitValue.Text = DistanceValue.Text = "--";
            GameCheck.Text = ConnectorCheck.Text = Strings.Pending;
            ControlCheck.Text = Strings.Unavailable;
            AutomationStateValue.Text = Strings.OfflineState;
            return;
        }

        ConnectionTitle.Text = Strings.ServiceConnected;
        AutomationStateValue.Text = StateDescription(status.State);
        bool fresh = status.AgeS is <= 0.25 && status.SpeedMps is not null;
        RecordStatus(status, fresh);
        string? errorCode = status.ErrorCode;
        if (errorCode is null && !fresh)
            errorCode = status.AgeS is null || status.SpeedMps is null
                ? "TELEMETRY_UNAVAILABLE"
                : "TELEMETRY_STALE";
        UpdateConnectionDetail(errorCode is null
            ? StateDescription(status.State)
            : ErrorDescription(errorCode));
        SpeedValue.Text = fresh ? FormatKmh(status.SpeedMps) : "--";
        SpeedLimitValue.Text = fresh ? FormatKmh(status.SpeedLimitMps) : "--";
        DistanceValue.Text = fresh && status.DistanceM is double distance
            ? string.Format(CultureInfo.CurrentCulture, Strings.DistanceKmFormat, distance / 1000.0)
            : "--";
        GameCheck.Text = status.ErrorCode == "GAME_NOT_RUNNING"
            ? Strings.Unavailable
            : fresh ? Strings.Available : Strings.Pending;
        ConnectorCheck.Text = fresh
            ? Strings.Available
            : status.ErrorCode is "TELEMETRY_UNAVAILABLE" or "TELEMETRY_STALE"
                ? Strings.Unavailable
                : Strings.Pending;
        ControlCheck.Text = Strings.Unavailable;
    }

    private void RecordStatus(UiStatus? status, bool telemetryFresh)
    {
        bool connected = status is not null;
        if (connected && (!_hasObservedServiceConnection || !_serviceConnected))
            AddActivity(Strings.ActivityServiceConnected);
        else if (!connected && _hasObservedServiceConnection && _serviceConnected)
            AddActivity(Strings.ActivityServiceDisconnected);
        _hasObservedServiceConnection = true;
        _serviceConnected = connected;

        if (status is null)
        {
            _lastTelemetryFresh = false;
            _lastLoggedState = null;
            _lastLoggedErrorCode = null;
            AnnounceActivityUpdate();
            return;
        }

        if (_lastTelemetryFresh != telemetryFresh)
            AddActivity(telemetryFresh ? Strings.ActivityTelemetryAvailable : Strings.ActivityTelemetryUnavailable);
        _lastTelemetryFresh = telemetryFresh;

        if (!string.Equals(_lastLoggedState, status.State, StringComparison.Ordinal))
            AddActivity(string.Format(CultureInfo.CurrentCulture, Strings.ActivityStateChangedFormat,
                StateDescription(status.State)));
        _lastLoggedState = status.State;

        if (!string.IsNullOrEmpty(status.ErrorCode) &&
            !string.Equals(_lastLoggedErrorCode, status.ErrorCode, StringComparison.Ordinal))
            AddActivity(string.Format(CultureInfo.CurrentCulture, Strings.ActivityIssueFormat,
                ErrorDescription(status.ErrorCode)));
        _lastLoggedErrorCode = status.ErrorCode;
        AnnounceActivityUpdate();
    }

    private void AddActivity(string message)
    {
        var entry = new ActivityItem(DateTime.Now.ToString("t", CultureInfo.CurrentCulture), message);
        _activityEntries.Insert(0, entry);
        _recentActivityEntries.Insert(0, entry);
        while (_activityEntries.Count > 50) _activityEntries.RemoveAt(_activityEntries.Count - 1);
        while (_recentActivityEntries.Count > 3) _recentActivityEntries.RemoveAt(_recentActivityEntries.Count - 1);
        ActivityEmpty.Visibility = Visibility.Collapsed;
        RecentActivityEmpty.Visibility = Visibility.Collapsed;
    }

    private void AnnounceActivityUpdate()
    {
        ItemsControl? liveRegion = _navigation.IsSelected(UiPage.Activity)
            ? ActivityList
            : _navigation.IsSelected(UiPage.Overview) ? RecentActivityList : null;
        if (liveRegion is null) return;
        var peer = UIElementAutomationPeer.FromElement(liveRegion)
            ?? UIElementAutomationPeer.CreatePeerForElement(liveRegion);
        peer?.RaiseAutomationEvent(AutomationEvents.LiveRegionChanged);
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

    private static string StateDescription(string state) => state switch
    {
        "ready" => Strings.StateReady,
        "engaged" => Strings.StateEngaged,
        "manual_override" => Strings.StateManualOverride,
        "fault" => Strings.StateFault,
        _ => Strings.ServiceStatusUnknown
    };
}

// Edited by: OpenAI-Codex_Responsible: Source_Code
internal sealed record ActivityItem(string Time, string Message);
