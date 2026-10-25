// Edited by: OpenAI-Codex_Responsible: Source_Code
namespace ETS2AP.Ui;

internal static class InputMonitorSafety
{
    private static readonly (uint Down, uint Up)[] MouseButtonPairs =
    [
        (0x0001, 0x0002),
        (0x0004, 0x0008),
        (0x0010, 0x0020),
        (0x0040, 0x0080),
        (0x0100, 0x0200)
    ];

    internal static uint ApplyMouseButtonFlags(uint heldButtons, uint flags)
    {
        foreach ((uint down, uint up) in MouseButtonPairs)
        {
            if ((flags & down) != 0) heldButtons |= down;
            if ((flags & up) != 0) heldButtons &= ~down;
        }
        return heldButtons;
    }

    internal static bool IsHealthy(bool rawInputRegistered, bool xinputAvailable,
        bool physicalDeviceCoverageValidated, bool unclassifiedHidObserved) =>
        rawInputRegistered && xinputAvailable && physicalDeviceCoverageValidated &&
        !unclassifiedHidObserved;
}
