// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Windows.Input;

namespace ETS2AP.Ui;

internal enum UiPage { Overview, Setup, Settings, Activity }

internal sealed class NavigationState
{
    public UiPage Current { get; private set; } = UiPage.Overview;

    public void Navigate(UiPage page)
    {
        if (!Enum.IsDefined(page)) throw new ArgumentOutOfRangeException(nameof(page));
        Current = page;
    }

    public bool IsSelected(UiPage page) => Current == page;

    public static UiPage? FromShortcut(Key key, ModifierKeys modifiers)
    {
        if (modifiers != ModifierKeys.Control) return null;
        return key switch
        {
            Key.D1 or Key.NumPad1 => UiPage.Overview,
            Key.D2 or Key.NumPad2 => UiPage.Setup,
            Key.D3 or Key.NumPad3 => UiPage.Settings,
            Key.D4 or Key.NumPad4 => UiPage.Activity,
            _ => null
        };
    }
}
