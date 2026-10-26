// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Diagnostics;
using System.IO;

namespace ETS2AP.Ui;

internal static class PythonServiceLauncher
{
    internal static string RuntimePath(string installRoot) =>
        Path.GetFullPath(Path.Combine(installRoot, "runtime", "python.exe"));

    internal static bool IsAvailable(string installRoot, string uiExecutable)
    {
        string root = Path.GetFullPath(installRoot);
        return File.Exists(RuntimePath(root)) &&
            File.Exists(Path.Combine(root, "python", "ets2ap", "ui_server.py")) &&
            File.Exists(uiExecutable);
    }

    internal static ProcessStartInfo CreateStartInfo(string installRoot, string uiExecutable,
        string? trustedControlServerImage = null)
    {
        string root = Path.GetFullPath(installRoot);
        var start = new ProcessStartInfo
        {
            FileName = RuntimePath(root),
            WorkingDirectory = root,
            UseShellExecute = false,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden
        };
        start.ArgumentList.Add("-I");
        start.ArgumentList.Add("-S");
        start.ArgumentList.Add("-m");
        start.ArgumentList.Add("ets2ap.ui_server");
        start.ArgumentList.Add("--trusted-ui-image");
        start.ArgumentList.Add(Path.GetFullPath(uiExecutable));
        if (!string.IsNullOrWhiteSpace(trustedControlServerImage))
        {
            string serverImage = Path.GetFullPath(trustedControlServerImage);
            if (!File.Exists(serverImage))
                throw new FileNotFoundException("The trusted control-server executable does not exist.", serverImage);
            start.ArgumentList.Add("--trusted-control-server-image");
            start.ArgumentList.Add(serverImage);
        }
        return start;
    }
}

internal sealed class PythonServiceRestartPolicy
{
    private int _restartUsed;

    internal bool TryConsumeRestart() => Interlocked.CompareExchange(ref _restartUsed, 1, 0) == 0;
}
