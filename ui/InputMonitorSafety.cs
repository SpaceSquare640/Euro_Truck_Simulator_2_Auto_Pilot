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

internal sealed class RawKeyboardState
{
    private readonly HashSet<(nint Device, int Key)> _pressedKeys = new();

    internal bool IsActive => _pressedKeys.Count != 0;

    internal void SetKey(nint device, int key, bool isDown)
    {
        if (isDown) _pressedKeys.Add((device, key));
        else _pressedKeys.Remove((device, key));
    }

    internal void RemoveDevice(nint device) =>
        _pressedKeys.RemoveWhere(pressed => pressed.Device == device);
}

internal sealed class RawMouseButtonState
{
    private readonly Dictionary<nint, uint> _heldButtons = new();

    internal bool IsActive => _heldButtons.Count != 0;

    internal void Apply(nint device, uint flags)
    {
        uint heldButtons = _heldButtons.TryGetValue(device, out uint current) ? current : 0;
        heldButtons = InputMonitorSafety.ApplyMouseButtonFlags(heldButtons, flags);
        if (heldButtons == 0) _heldButtons.Remove(device);
        else _heldButtons[device] = heldButtons;
    }

    internal void RemoveDevice(nint device) => _heldButtons.Remove(device);
}
