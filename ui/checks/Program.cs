// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Collections;
using System.IO.Compression;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Globalization;
using System.Reflection;
using System.Resources;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using ETS2AP.Ui;
using ETS2AP.Ui.Resources;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--ipc-smoke")
            return CheckPipeSmoke(args[1]);
        (string Name, Action Run)[] checks =
        [
            ("Localized resources", CheckResources),
            ("Theme resources and contrast", CheckThemes),
            ("Preferences parsing and round trip", CheckPreferences),
            ("Navigation state and shortcuts", CheckNavigation),
            ("UI status protocol", CheckStatusProtocol),
            ("Connector release metadata", CheckConnectorReleaseMetadata),
            ("Connector release download", CheckConnectorReleaseDownload),
            ("Connector archive validation", CheckConnectorArchiveValidation),
            ("Connector install directory lock", CheckConnectorInstallDirectoryLock),
            ("QPC clock origin", CheckQpcClock)
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

    private static int CheckPipeSmoke(string trustedPythonImage)
    {
        using var cancellation = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var client = new UiPipeClient(trustedPythonImage);
        var observed = new TaskCompletionSource<UiStatus>(TaskCreationOptions.RunContinuationsAsynchronously);
        client.StatusChanged += status =>
        {
            if (status is not null) observed.TrySetResult(status);
        };
        Task run = client.RunAsync(cancellation.Token);
        try
        {
            UiStatus status = observed.Task.WaitAsync(cancellation.Token).GetAwaiter().GetResult();
            Require(status.State == "fault", "The development service must not report ready or engaged.");
            Console.WriteLine($"PASS UI pipe smoke: {status.ErrorCode}");
            return 0;
        }
        catch (OperationCanceledException)
        {
            Console.Error.WriteLine($"FAIL UI pipe smoke: no valid status within five seconds ({client.LastFailure ?? "none"}).");
            return 1;
        }
        finally
        {
            cancellation.Cancel();
            run.GetAwaiter().GetResult();
        }
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

    private static void CheckStatusProtocol()
    {
        const string valid = "{\"type\":\"status\",\"version\":\"1.0\",\"id\":\"p-2\",\"timestamp\":\"123\",\"payload\":{\"state\":\"fault\",\"error_code\":\"CONTROL_UNAVAILABLE\",\"state_revision\":0,\"telemetry\":{\"speed_mps\":10,\"speed_limit_mps\":20,\"navigation_distance_m\":1250,\"telemetry_age_s\":0.1}}}";
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(valid);
        Require(UiStatusProtocol.ReadType(bytes) == "status", "Valid envelope was not parsed.");
        UiStatus parsed = UiStatusProtocol.ParseStatus(bytes);
        Require(parsed.SpeedMps == 10 && parsed.ErrorCode == "CONTROL_UNAVAILABLE", "Valid status was not parsed.");
        Require(Reject(valid.Replace("\"speed_mps\":10", "\"speed_mps\":-1")),
            "Negative speed was accepted.");
        Require(Reject(valid.Replace("\"state\":\"fault\"", "\"state\":\"engaged\",\"state\":\"fault\"")),
            "Duplicate state was accepted.");
        Require(Reject(valid.Replace("\"speed_mps\":10", "\"speed_mps\":1e999")),
            "Overflowing number was accepted.");
    }

    private static void CheckQpcClock()
    {
        Require(QueryPerformanceFrequency(out long frequency) && frequency > 0, "QPC frequency unavailable.");
        Require(QueryPerformanceCounter(out long before), "QPC unavailable.");
        long observed = System.Diagnostics.Stopwatch.GetTimestamp();
        Require(QueryPerformanceCounter(out long after), "QPC unavailable.");
        Require(observed >= before && observed <= after, "Stopwatch does not share the QPC origin.");
    }

    private static void CheckConnectorReleaseMetadata()
    {
        const string digest = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        string json = JsonSerializer.Serialize(new object[]
        {
            new { draft = true, tag_name = "v2.0.0", assets = Array.Empty<object>() },
            new
            {
                draft = false,
                tag_name = "v1.2.3-beta.1",
                assets = new[]
                {
                    new
                    {
                        name = "ETS2AP_DLL_V1.2.3-beta.1.zip",
                        browser_download_url = "https://github.com/SpaceSquare640/Euro_Truck_Simulator_2_Auto_Pilot/releases/download/v1.2.3-beta.1/ETS2AP_DLL_V1.2.3-beta.1.zip",
                        size = 2048,
                        digest
                    }
                }
            }
        });
        var selected = ConnectorReleaseAssetSelector.SelectLatest(json);
        Require(selected.Version == "1.2.3-beta.1", "Prerelease version was not selected from the newest published release.");
        Require(selected.Sha256 == digest["sha256:".Length..], "Release SHA-256 digest was not parsed.");
        Require(selected.Size == 2048, "Release asset size was not retained.");

        string missingDigest = json.Replace(digest, string.Empty, StringComparison.Ordinal);
        Require(RejectRelease(missingDigest), "An asset without a SHA-256 digest was accepted.");
        string wrongHost = json.Replace("https://github.com/SpaceSquare640/", "https://attacker.example/SpaceSquare640/", StringComparison.Ordinal);
        Require(RejectRelease(wrongHost), "A release asset from an untrusted host was accepted.");
        string mismatchedName = json.Replace("ETS2AP_DLL_V1.2.3-beta.1.zip", "ETS2AP_DLL_V9.9.9.zip", StringComparison.Ordinal);
        Require(RejectRelease(mismatchedName), "An asset name that does not match the release tag was accepted.");
    }

    private static void CheckConnectorArchiveValidation()
    {
        byte[] archive = BuildConnectorArchive("1.2.3-beta.1");
        var files = ConnectorPackage.ReadValidated(archive, "1.2.3-beta.1");
        Require(files.Count == 1 && files.ContainsKey("ets2ap_plugin.dll"), "Valid connector DLL was not extracted.");

        Require(RejectPackage(BuildConnectorArchive("1.2.3"), "1.2.3-beta.1"),
            "A package for a different version was accepted.");
        Require(RejectPackage(BuildConnectorArchive("1.2.3-beta.1", dllContent: [0x4d, 0x5a], declaredHash: new string('0', 64)), "1.2.3-beta.1"),
            "A package with a mismatched DLL digest was accepted.");
        Require(RejectPackage(BuildConnectorArchive("1.2.3-beta.1", entryPath: "plugins/../escape.dll"), "1.2.3-beta.1"),
            "A package containing a traversal path was accepted.");
        Require(RejectPackage(BuildConnectorArchive("1.2.3-beta.1", name: "untrusted.dll"), "1.2.3-beta.1"),
            "A package containing an unapproved DLL name was accepted.");
    }

    private static void CheckConnectorReleaseDownload()
    {
        byte[] archive = BuildConnectorArchive("1.2.3-beta.1");
        string digest = Convert.ToHexString(SHA256.HashData(archive)).ToLowerInvariant();
        string releaseJson = JsonSerializer.Serialize(new[]
        {
            new
            {
                draft = false,
                tag_name = "v1.2.3-beta.1",
                assets = new[]
                {
                    new
                    {
                        name = "ETS2AP_DLL_V1.2.3-beta.1.zip",
                        browser_download_url = "https://github.com/SpaceSquare640/Euro_Truck_Simulator_2_Auto_Pilot/releases/download/v1.2.3-beta.1/ETS2AP_DLL_V1.2.3-beta.1.zip",
                        size = archive.Length,
                        digest = "sha256:" + digest
                    }
                }
            }
        });
        using var handler = new FakeReleaseHandler(releaseJson, archive);
        using var client = new HttpClient(handler);
        var downloader = new ConnectorReleaseClient(client);
        DownloadedConnectorPackage package = downloader.DownloadLatestAsync(CancellationToken.None)
            .GetAwaiter().GetResult();
        Require(package.Version == "1.2.3-beta.1" && package.Sha256 == digest,
            "Downloaded connector metadata did not match the GitHub release.");
        Require(package.Archive.SequenceEqual(archive), "Verified connector ZIP was not retained unchanged in memory.");
        Require(handler.RequestCount == 2, "The connector download did not make exactly two bounded GitHub requests.");

        string invalidRelease = releaseJson.Replace(digest, new string('0', 64), StringComparison.Ordinal);
        using var badHandler = new FakeReleaseHandler(invalidRelease, archive);
        using var badClient = new HttpClient(badHandler);
        var badDownloader = new ConnectorReleaseClient(badClient);
        bool rejected = false;
        try
        {
            _ = badDownloader.DownloadLatestAsync(CancellationToken.None).GetAwaiter().GetResult();
        }
        catch (InvalidDataException) { rejected = true; }
        Require(rejected, "Connector ZIP with a mismatched release digest was accepted.");
    }

    private static void CheckConnectorInstallDirectoryLock()
    {
        string parent = Path.Combine(Environment.CurrentDirectory, "build");
        Directory.CreateDirectory(parent);
        string root = Path.Combine(parent, $"ets2ap-lock-{Guid.NewGuid():N}");
        string moved = root + ".moved";
        Directory.CreateDirectory(Path.Combine(root, "bin", "win_x64", "plugins"));
        try
        {
            using (ConnectorInstaller.LockDirectoryTreeForChecks(Path.Combine(root, "bin", "win_x64", "plugins")))
            {
                bool renameBlocked = false;
                try { Directory.Move(root, moved); }
                catch (IOException) { renameBlocked = true; }
                catch (UnauthorizedAccessException) { renameBlocked = true; }
                Require(renameBlocked, "A locked installation path could still be renamed.");
            }
            Directory.Move(root, moved);
            Require(Directory.Exists(moved), "The directory chain stayed locked after lease disposal.");
        }
        finally
        {
            if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
            if (Directory.Exists(moved)) Directory.Delete(moved, recursive: true);
        }
    }

    private static bool RejectRelease(string json)
    {
        try
        {
            _ = ConnectorReleaseAssetSelector.SelectLatest(json);
            return false;
        }
        catch (InvalidDataException) { return true; }
    }

    private static bool RejectPackage(byte[] archive, string version)
    {
        try
        {
            _ = ConnectorPackage.ReadValidated(archive, version);
            return false;
        }
        catch (InvalidDataException) { return true; }
    }

    private static byte[] BuildConnectorArchive(
        string version,
        string name = "ets2ap_plugin.dll",
        string entryPath = "plugins/ets2ap_plugin.dll",
        byte[]? dllContent = null,
        string? declaredHash = null)
    {
        dllContent ??= [0x4d, 0x5a, 0x01, 0x02];
        string hash = declaredHash ?? Convert.ToHexString(SHA256.HashData(dllContent)).ToLowerInvariant();
        string manifest = JsonSerializer.Serialize(new
        {
            format = 1,
            version,
            files = new[] { new { name, sha256 = hash } }
        });
        using var output = new MemoryStream();
        using (var zip = new ZipArchive(output, ZipArchiveMode.Create, leaveOpen: true))
        {
            using (var writer = new StreamWriter(zip.CreateEntry("manifest.json").Open(), new UTF8Encoding(false)))
                writer.Write(manifest);
            using var dll = zip.CreateEntry(entryPath).Open();
            dll.Write(dllContent);
        }
        return output.ToArray();
    }

    private sealed class FakeReleaseHandler(string releaseJson, byte[] archive) : HttpMessageHandler
    {
        public int RequestCount { get; private set; }

        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken)
        {
            RequestCount++;
            HttpContent content = request.RequestUri?.Host == "api.github.com"
                ? new StringContent(releaseJson, Encoding.UTF8, "application/json")
                : new ByteArrayContent(archive);
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = content });
        }
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryPerformanceCounter(out long value);

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool QueryPerformanceFrequency(out long value);

    private static bool Reject(string json)
    {
        try
        {
            byte[] bytes = System.Text.Encoding.UTF8.GetBytes(json);
            _ = UiStatusProtocol.ReadType(bytes);
            _ = UiStatusProtocol.ParseStatus(bytes);
            return false;
        }
        catch (InvalidDataException) { return true; }
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
