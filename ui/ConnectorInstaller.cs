// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using Microsoft.Win32.SafeHandles;

namespace ETS2AP.Ui;

internal static class ConnectorInstaller
{
    internal const string HelperSwitch = "--install-connector-elevated";
    internal const int GameRunningExitCode = 10;

    public static async Task<int> LaunchElevatedAsync(
        string gameFolder,
        CancellationToken cancellationToken)
    {
        string executable = Environment.ProcessPath
            ?? throw new InvalidOperationException("Current application path is unavailable.");
        var start = new ProcessStartInfo(executable)
        {
            UseShellExecute = true,
            Verb = "runas",
            WorkingDirectory = Path.GetDirectoryName(executable) ?? Environment.CurrentDirectory
        };
        start.ArgumentList.Add(HelperSwitch);
        start.ArgumentList.Add(gameFolder);

        using Process process = Process.Start(start)
            ?? throw new InvalidOperationException("Elevated connector installer did not start.");
        await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        return process.ExitCode;
    }

    public static bool IsHelperInvocation(string[] args) =>
        args.Length > 0 && string.Equals(args[0], HelperSwitch, StringComparison.Ordinal);

    public static int RunElevatedHelper(string[] args)
    {
        if (args.Length != 2 || !IsHelperInvocation(args)) return 2;
        try
        {
            if (!IsAdministrator()) throw new UnauthorizedAccessException("Administrator permission is required.");
            if (IsGameRunning()) throw new GameIsRunningException();
            DownloadedConnectorPackage package = new ConnectorReleaseClient()
                .DownloadLatestAsync(CancellationToken.None).GetAwaiter().GetResult();
            Install(args[1], package);
            return 0;
        }
        catch (GameIsRunningException)
        {
            return GameRunningExitCode;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or
            ArgumentException or CryptographicException or InvalidOperationException or Win32Exception or
            HttpRequestException or TaskCanceledException or AggregateException)
        {
            return 1;
        }
    }

    private static void Install(string gameFolder, DownloadedConnectorPackage package)
    {
        if (!IsAdministrator()) throw new UnauthorizedAccessException("Administrator permission is required.");
        if (IsGameRunning()) throw new GameIsRunningException();

        string gameRoot = Path.GetFullPath(gameFolder);
        if (package.Sha256.Length != 64 || package.Sha256.Any(character => !Uri.IsHexDigit(character)))
            throw new InvalidDataException("Connector ZIP digest is invalid.");

        string packageHash = Convert.ToHexString(SHA256.HashData(package.Archive)).ToLowerInvariant();
        if (!CryptographicOperations.FixedTimeEquals(
            Convert.FromHexString(packageHash), Convert.FromHexString(package.Sha256)))
            throw new InvalidDataException("Connector ZIP digest verification failed.");

        IReadOnlyDictionary<string, byte[]> files = ConnectorPackage.ReadValidated(package.Archive, package.Version);

        string pluginDirectory = Path.Combine(gameRoot, "bin", "win_x64", "plugins");
        using DirectoryTreeLease directoryLease = DirectoryTreeLease.Acquire(pluginDirectory, createLeaf: true);
        string canonicalGameRoot = directoryLease.GetLockedPath(gameRoot);
        string canonicalWin64 = directoryLease.GetLockedPath(Path.Combine(gameRoot, "bin", "win_x64"));
        string canonicalPluginDirectory = directoryLease.GetLockedPath(pluginDirectory);
        EnsureContained(canonicalGameRoot, canonicalWin64);
        EnsureContained(canonicalGameRoot, canonicalPluginDirectory);
        string gameExecutable = Path.Combine(canonicalWin64, "eurotrucks2.exe");
        if (!File.Exists(gameExecutable)) throw new InvalidDataException("Selected folder is not an ETS2 installation.");

        string backupRoot = Path.Combine(canonicalWin64,
            $"ETS2AP_Backups_{DateTimeOffset.UtcNow:yyyyMMddTHHmmssfffZ}_{Guid.NewGuid():N}");
        var installItems = new List<InstallItem>();
        DirectoryTreeLease? backupLease = null;
        try
        {
            foreach ((string name, byte[] content) in files)
            {
                string target = Path.GetFullPath(Path.Combine(pluginDirectory, name));
                if (!string.Equals(Path.GetDirectoryName(target), pluginDirectory, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Connector target escaped the game plugins folder.");
                if (Directory.Exists(target)) throw new InvalidDataException("A connector target is a directory.");
                if (File.Exists(target) && (File.GetAttributes(target) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("A connector target cannot be a reparse point.");

                if (File.Exists(target) && string.Equals(HashFile(target),
                    Convert.ToHexString(SHA256.HashData(content)), StringComparison.OrdinalIgnoreCase))
                    continue;

                string? backup = null;
                string? originalSha256 = null;
                if (File.Exists(target))
                {
                    if (backupLease is null)
                    {
                        Directory.CreateDirectory(backupRoot);
                        backupLease = DirectoryTreeLease.Acquire(backupRoot, createLeaf: false);
                        EnsureContained(canonicalGameRoot, backupLease.GetLockedPath(backupRoot));
                    }
                    backup = Path.Combine(backupRoot, name);
                    string originalHash = HashFile(target);
                    originalSha256 = originalHash;
                    File.Copy(target, backup, overwrite: false);
                    if (!string.Equals(HashFile(backup), originalHash, StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Connector backup verification failed.");
                }
                string temporary = Path.Combine(pluginDirectory, $".{name}.{Guid.NewGuid():N}.tmp");
                installItems.Add(new InstallItem(name, target, temporary, backup, originalSha256,
                    Convert.ToHexString(SHA256.HashData(content))));
                using var stagedFile = new FileStream(temporary, FileMode.CreateNew,
                    FileAccess.Write, FileShare.Read);
                stagedFile.Write(content);
                stagedFile.Flush(flushToDisk: true);
            }

            var installed = new List<InstallItem>();
            try
            {
                foreach (InstallItem item in installItems)
                {
                    if (IsGameRunning()) throw new GameIsRunningException();
                    File.Move(item.TemporaryPath, item.TargetPath, overwrite: true);
                    installed.Add(item);
                }
            }
            catch (Exception installationError)
            {
                var rollbackErrors = new List<Exception>();
                foreach (InstallItem item in installed.AsEnumerable().Reverse())
                {
                    try
                    {
                        if (item.BackupPath is not null)
                        {
                            if (item.OriginalSha256 is null || !File.Exists(item.BackupPath) ||
                                !string.Equals(HashFile(item.BackupPath), item.OriginalSha256,
                                    StringComparison.OrdinalIgnoreCase))
                                throw new IOException("Connector backup is unavailable or failed verification.");
                            RestoreViaTemporaryFile(item.BackupPath, item.TargetPath, pluginDirectory,
                                item.OriginalSha256);
                        }
                        else if (File.Exists(item.TargetPath) &&
                            string.Equals(HashFile(item.TargetPath), item.ContentSha256,
                                StringComparison.OrdinalIgnoreCase))
                        {
                            File.Delete(item.TargetPath);
                        }
                    }
                    catch (Exception rollbackError)
                    {
                        rollbackErrors.Add(rollbackError);
                    }
                }
                if (rollbackErrors.Count > 0)
                    throw new AggregateException("Connector installation failed and rollback was incomplete.",
                        [installationError, .. rollbackErrors]);
                throw;
            }
        }
        finally
        {
            foreach (InstallItem item in installItems)
                if (File.Exists(item.TemporaryPath)) File.Delete(item.TemporaryPath);
            backupLease?.Dispose();
        }
    }

    private static void RestoreViaTemporaryFile(string backupPath, string targetPath, string pluginDirectory,
        string expectedSha256)
    {
        string temporary = Path.Combine(pluginDirectory, $".restore.{Guid.NewGuid():N}.tmp");
        try
        {
            using (var source = File.OpenRead(backupPath))
            using (var destination = new FileStream(temporary, FileMode.CreateNew,
                       FileAccess.Write, FileShare.Read))
            {
                source.CopyTo(destination);
                destination.Flush(flushToDisk: true);
            }
            if (!string.Equals(HashFile(temporary), expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new IOException("Connector rollback verification failed.");
            File.Move(temporary, targetPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporary)) File.Delete(temporary);
        }
    }

    private static bool IsAdministrator()
    {
        using WindowsIdentity identity = WindowsIdentity.GetCurrent();
        return new WindowsPrincipal(identity).IsInRole(WindowsBuiltInRole.Administrator);
    }

    internal static bool IsGameRunning()
    {
        foreach (Process process in Process.GetProcessesByName("eurotrucks2"))
        {
            using (process) return true;
        }
        return false;
    }

    private static string HashFile(string path)
    {
        using FileStream stream = File.OpenRead(path);
        return Convert.ToHexString(SHA256.HashData(stream));
    }

    private static void EnsureContained(string root, string child)
    {
        string relative = Path.GetRelativePath(root, child);
        if (Path.IsPathRooted(relative) || relative == ".." ||
            relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal) ||
            relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal))
            throw new InvalidDataException("A game installation path resolves outside the selected folder.");
    }

    private sealed record InstallItem(string Name, string TargetPath, string TemporaryPath,
        string? BackupPath, string? OriginalSha256, string ContentSha256);

    internal static IDisposable LockDirectoryTreeForChecks(string path) =>
        DirectoryTreeLease.Acquire(path, createLeaf: false);

    private sealed class DirectoryTreeLease : IDisposable
    {
        private const uint FileReadAttributes = 0x00000080;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint OpenExisting = 3;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileAttributeReparsePoint = 0x00000400;

        private readonly Dictionary<string, SafeFileHandle> handles = new(StringComparer.OrdinalIgnoreCase);

        private DirectoryTreeLease() { }

        public static DirectoryTreeLease Acquire(string path, bool createLeaf)
        {
            string fullPath = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
            string root = Path.GetPathRoot(fullPath)
                ?? throw new InvalidDataException("Game folder must be on a local drive.");
            if (root.Length != 3 || root[1] != ':' || !root.EndsWith(Path.DirectorySeparatorChar))
                throw new InvalidDataException("Game folder must be on a local drive.");

            var lease = new DirectoryTreeLease();
            try
            {
                string current = root;
                lease.OpenAndLock(current);
                string[] components = fullPath[root.Length..]
                    .Split([Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar],
                        StringSplitOptions.RemoveEmptyEntries);
                for (int index = 0; index < components.Length; index++)
                {
                    current = Path.Combine(current, components[index]);
                    bool last = index == components.Length - 1;
                    if (!Directory.Exists(current) && !(last && createLeaf))
                        throw new InvalidDataException("A game installation directory is missing.");
                    if (last && createLeaf) Directory.CreateDirectory(current);
                    lease.OpenAndLock(current);
                }
                return lease;
            }
            catch
            {
                lease.Dispose();
                throw;
            }
        }

        public string GetLockedPath(string path)
        {
            string fullPath = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
            if (!handles.ContainsKey(fullPath))
                throw new InvalidDataException("Requested game folder is not locked.");
            return fullPath;
        }

        public void Dispose()
        {
            foreach (SafeFileHandle handle in handles.Values.Reverse()) handle.Dispose();
            handles.Clear();
        }

        private void OpenAndLock(string path)
        {
            string fullPath = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
            SafeFileHandle handle = CreateFileW(fullPath, FileReadAttributes,
                FileShareRead | FileShareWrite, IntPtr.Zero, OpenExisting,
                FileFlagOpenReparsePoint | FileFlagBackupSemantics, IntPtr.Zero);
            if (handle.IsInvalid)
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "Unable to lock a game installation directory.");
            }
            if (!GetFileInformationByHandle(handle, out ByHandleFileInformation information))
            {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "Unable to inspect a game installation directory.");
            }
            if ((information.FileAttributes & FileAttributeReparsePoint) != 0)
            {
                handle.Dispose();
                throw new InvalidDataException("Game installation paths cannot contain junctions or reparse points.");
            }
            if (!handles.TryAdd(fullPath, handle)) handle.Dispose();
        }

        [DllImport("kernel32.dll", EntryPoint = "CreateFileW", CharSet = CharSet.Unicode,
            SetLastError = true, ExactSpelling = true)]
        private static extern SafeFileHandle CreateFileW(string fileName, uint desiredAccess,
            uint shareMode, IntPtr securityAttributes, uint creationDisposition,
            uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true, ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(SafeFileHandle file,
            out ByHandleFileInformation information);

        [StructLayout(LayoutKind.Sequential)]
        private struct ByHandleFileInformation
        {
            public uint FileAttributes;
            public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
            public uint VolumeSerialNumber;
            public uint FileSizeHigh;
            public uint FileSizeLow;
            public uint NumberOfLinks;
            public uint FileIndexHigh;
            public uint FileIndexLow;
        }
    }

    private sealed class GameIsRunningException : Exception { }
}
