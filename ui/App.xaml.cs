// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Windows;

namespace ETS2AP.Ui;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (ConnectorInstaller.IsHelperInvocation(e.Args))
        {
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            Shutdown(ConnectorInstaller.RunElevatedHelper(e.Args));
            return;
        }

        MainWindow = new MainWindow();
        MainWindow.Show();
    }
}
