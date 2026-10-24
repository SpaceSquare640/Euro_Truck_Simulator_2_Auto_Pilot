// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Globalization;
using System.Resources;

namespace ETS2AP.Ui.Resources;

public static class Strings
{
    private static readonly ResourceManager Manager = new("ETS2AP.Ui.Resources.Strings", typeof(Strings).Assembly);
    private static string Get(string key) => Manager.GetString(key, CultureInfo.CurrentUICulture) ?? key;

    public static string AppTitle => Get(nameof(AppTitle));
    public static string Overview => Get(nameof(Overview));
    public static string Setup => Get(nameof(Setup));
    public static string Settings => Get(nameof(Settings));
    public static string Activity => Get(nameof(Activity));
    public static string DrivingOverview => Get(nameof(DrivingOverview));
    public static string OverviewDescription => Get(nameof(OverviewDescription));
    public static string NotConnected => Get(nameof(NotConnected));
    public static string NotConnectedDetail => Get(nameof(NotConnectedDetail));
    public static string SetUpConnection => Get(nameof(SetUpConnection));
    public static string DrivingInformation => Get(nameof(DrivingInformation));
    public static string Speed => Get(nameof(Speed));
    public static string SpeedLimit => Get(nameof(SpeedLimit));
    public static string DistanceRemaining => Get(nameof(DistanceRemaining));
    public static string Eta => Get(nameof(Eta));
    public static string Kmh => Get(nameof(Kmh));
    public static string ConnectionChecklist => Get(nameof(ConnectionChecklist));
    public static string Game => Get(nameof(Game));
    public static string Connector => Get(nameof(Connector));
    public static string ControlService => Get(nameof(ControlService));
    public static string Pending => Get(nameof(Pending));
    public static string Engage => Get(nameof(Engage));
    public static string EngageUnavailable => Get(nameof(EngageUnavailable));
    public static string RecentActivity => Get(nameof(RecentActivity));
    public static string NoRecentActivity => Get(nameof(NoRecentActivity));
    public static string ActivityAfterConnect => Get(nameof(ActivityAfterConnect));
    public static string DriverPriority => Get(nameof(DriverPriority));
    public static string Theme => Get(nameof(Theme));
    public static string SystemTheme => Get(nameof(SystemTheme));
    public static string LightTheme => Get(nameof(LightTheme));
    public static string DarkTheme => Get(nameof(DarkTheme));
    public static string SetupTitle => Get(nameof(SetupTitle));
    public static string SetupDescription => Get(nameof(SetupDescription));
    public static string GameFolder => Get(nameof(GameFolder));
    public static string Browse => Get(nameof(Browse));
    public static string NoFolderSelected => Get(nameof(NoFolderSelected));
    public static string FolderAccepted => Get(nameof(FolderAccepted));
    public static string FolderInvalid => Get(nameof(FolderInvalid));
    public static string ConnectorInstallation => Get(nameof(ConnectorInstallation));
    public static string ConnectorInstallationDescription => Get(nameof(ConnectorInstallationDescription));
    public static string InstallConnector => Get(nameof(InstallConnector));
    public static string ConnectorUnavailable => Get(nameof(ConnectorUnavailable));
    public static string ServiceUnavailable => Get(nameof(ServiceUnavailable));
    public static string SettingsTitle => Get(nameof(SettingsTitle));
    public static string Appearance => Get(nameof(Appearance));
    public static string ThemeDescription => Get(nameof(ThemeDescription));
    public static string HighContrastNote => Get(nameof(HighContrastNote));
    public static string Language => Get(nameof(Language));
    public static string English => Get(nameof(English));
    public static string KeyboardShortcuts => Get(nameof(KeyboardShortcuts));
    public static string ShortcutsDescription => Get(nameof(ShortcutsDescription));
    public static string ActivityTitle => Get(nameof(ActivityTitle));
    public static string ActivityDescription => Get(nameof(ActivityDescription));
    public static string CopyStatus => Get(nameof(CopyStatus));
    public static string StatusCopied => Get(nameof(StatusCopied));
    public static string AppVersion => Get(nameof(AppVersion));
    public static string CurrentPage => Get(nameof(CurrentPage));
}
