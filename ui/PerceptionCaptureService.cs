// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace ETS2AP.Ui;

internal sealed class PerceptionCaptureService : IDisposable
{
    private const int WmInput = 0x00FF;
    private const int WmInputDeviceChange = 0x00FE;
    private const int WmKeyDown = 0x0100;
    private const int WmKeyUp = 0x0101;
    private const int WmSysKeyDown = 0x0104;
    private const int WmSysKeyUp = 0x0105;
    private const uint RidInput = 0x10000003;
    private const uint RidevInputSink = 0x00000100;
    private const uint RidevDeviceNotify = 0x00002000;
    private const nint GidcRemoval = 2;
    private const uint HidReportType = 2;
    private const bool PhysicalDeviceCoverageValidated = false;
    private const int Rgb24Bytes = PerceptionFramePublisher.PixelBytes;
    private readonly Window _window;
    private readonly DispatcherTimer _timer;
    private readonly PerceptionFramePublisher _publisher;
    private readonly PhysicalInputPipeClient _inputPipe;
    private readonly GameFrameReader _gameFrameReader = new();
    private readonly RawKeyboardState _pressedKeys = new();
    private readonly RawMouseButtonState _mouseButtons = new();
    private readonly Dictionary<nint, byte[]> _hidBaselines = new();
    private HwndSource? _source;
    private ulong _manualUntilNs;
    private bool _rawInputHealthy;
    private bool _xinputHealthy;
    private bool _unclassifiedHidObserved;
    private bool _disposed;

    internal PerceptionCaptureService(Window window, string? trustedControlServerImage)
    {
        _window = window;
        _publisher = new PerceptionFramePublisher();
        _inputPipe = new PhysicalInputPipeClient(trustedControlServerImage);
        _timer = new DispatcherTimer(DispatcherPriority.Background, window.Dispatcher)
        {
            Interval = TimeSpan.FromMilliseconds(50)
        };
        _timer.Tick += Tick;
    }

    internal void Start()
    {
        if (_disposed) throw new ObjectDisposedException(nameof(PerceptionCaptureService));
        _inputPipe.Start();
        _source = HwndSource.FromHwnd(new WindowInteropHelper(_window).Handle);
        if (_source is null) return;
        _source.AddHook(WindowHook);
        RawInputDevice[] devices =
        [
            new() { UsagePage = 1, Usage = 2, Flags = RidevInputSink | RidevDeviceNotify, Target = _source.Handle },
            new() { UsagePage = 1, Usage = 4, Flags = RidevInputSink | RidevDeviceNotify, Target = _source.Handle },
            new() { UsagePage = 1, Usage = 5, Flags = RidevInputSink | RidevDeviceNotify, Target = _source.Handle },
            new() { UsagePage = 1, Usage = 6, Flags = RidevInputSink | RidevDeviceNotify, Target = _source.Handle },
            new() { UsagePage = 1, Usage = 8, Flags = RidevInputSink | RidevDeviceNotify, Target = _source.Handle }
        ];
        _rawInputHealthy = RegisterRawInputDevices(devices, (uint)devices.Length,
            (uint)Marshal.SizeOf<RawInputDevice>());
        try
        {
            _ = XInputGetState(0, out _);
            _xinputHealthy = true;
        }
        catch (DllNotFoundException) { _xinputHealthy = false; }
        catch (EntryPointNotFoundException) { _xinputHealthy = false; }
        _timer.Start();
    }

    private void Tick(object? sender, EventArgs e)
    {
        if (_disposed) return;
        ulong now = NowNanoseconds();
        nint gameWindow = FindGameWindow(out bool focused);
        byte[] rgb = new byte[Rgb24Bytes];
        bool frameValid = focused && gameWindow != 0 &&
            (_gameFrameReader.TryRead(rgb, now) || TryCapture(gameWindow, rgb));
        bool xinputActive = focused && PollXInput(now);
        bool manual = focused && (_pressedKeys.IsActive || _mouseButtons.IsActive ||
                      now < _manualUntilNs || xinputActive);

        PerceptionFrameFlags flags = PerceptionFrameFlags.None;
        if (focused) flags |= PerceptionFrameFlags.GameFocused;
        if (InputMonitorSafety.IsHealthy(_rawInputHealthy, _xinputHealthy,
                PhysicalDeviceCoverageValidated, _unclassifiedHidObserved))
            flags |= PerceptionFrameFlags.InputMonitorHealthy;
        if (manual) flags |= PerceptionFrameFlags.ManualInputActive;
        if (frameValid) flags |= PerceptionFrameFlags.FrameValid;
        _inputPipe.Publish(_rawInputHealthy, manual);
        try { _publisher.Publish(rgb, now, flags); }
        catch (Exception error) when (error is IOException or ObjectDisposedException or
            UnauthorizedAccessException or ArgumentException or OverflowException)
        {
            _rawInputHealthy = false;
        }
    }

    private nint WindowHook(nint hwnd, int message, nint wParam, nint lParam, ref bool handled)
    {
        if (message == WmInput) ObserveRawInput(lParam);
        else if (message == WmInputDeviceChange && wParam == GidcRemoval)
        {
            _pressedKeys.RemoveDevice(lParam);
            _mouseButtons.RemoveDevice(lParam);
            _hidBaselines.Remove(lParam);
        }
        return nint.Zero;
    }

    private void ObserveRawInput(nint rawInput)
    {
        bool gameForeground = IsGameForeground();
        uint size = 0;
        uint headerSize = (uint)Marshal.SizeOf<RawInputHeader>();
        _ = GetRawInputData(rawInput, RidInput, nint.Zero, ref size, headerSize);
        if (size < headerSize || size > 64 * 1024) return;
        nint storage = Marshal.AllocHGlobal((int)size);
        try
        {
            if (GetRawInputData(rawInput, RidInput, storage, ref size, headerSize) != size) return;
            uint type = unchecked((uint)Marshal.ReadInt32(storage));
            nint device = Marshal.ReadIntPtr(storage, 8);
            ulong now = NowNanoseconds();
            if (type == 1)
            {
                int message = Marshal.ReadInt32(storage, (int)headerSize + 8);
                int key = Marshal.ReadInt16(storage, (int)headerSize + 6);
                bool keyDown = message is WmKeyDown or WmSysKeyDown;
                bool keyUp = message is WmKeyUp or WmSysKeyUp;
                if (keyDown || keyUp) _pressedKeys.SetKey(device, key, keyDown);
                if (gameForeground && keyDown) _manualUntilNs = now + 250_000_000UL;
            }
            else if (type == 0)
            {
                uint buttonFlags = unchecked((uint)Marshal.ReadInt16(storage, (int)headerSize + 4));
                int deltaX = Marshal.ReadInt32(storage, (int)headerSize + 12);
                int deltaY = Marshal.ReadInt32(storage, (int)headerSize + 16);
                UpdateMouseButtons(device, buttonFlags);
                if (gameForeground && (buttonFlags != 0 || deltaX != 0 || deltaY != 0))
                    _manualUntilNs = now + 250_000_000UL;
            }
            else if (gameForeground && type == HidReportType)
            {
                _unclassifiedHidObserved = true;
                uint reportSize = unchecked((uint)Marshal.ReadInt32(storage, (int)headerSize));
                uint reportCount = unchecked((uint)Marshal.ReadInt32(storage, (int)headerSize + 4));
                ulong reportBytes = (ulong)reportSize * reportCount;
                if (reportSize == 0 || reportBytes > size - headerSize - 8 || reportBytes > 16 * 1024)
                    return;
                byte[] report = new byte[(int)reportBytes];
                Marshal.Copy(storage + (int)headerSize + 8, report, 0, report.Length);
                if (_hidBaselines.TryGetValue(device, out byte[]? baseline))
                {
                    if (!report.AsSpan().SequenceEqual(baseline)) _manualUntilNs = now + 250_000_000UL;
                }
                else if (_hidBaselines.Count < 64)
                {
                    _hidBaselines[device] = report;
                }
            }
        }
        finally { Marshal.FreeHGlobal(storage); }
    }

    private void UpdateMouseButtons(nint device, uint flags)
    {
        _mouseButtons.Apply(device, flags);
    }

    private bool PollXInput(ulong now)
    {
        try
        {
            for (uint index = 0; index < 4; index++)
            {
                if (XInputGetState(index, out XInputState state) != 0) continue;
                XInputGamepad pad = state.Gamepad;
                if (pad.Buttons != 0 || pad.LeftTrigger > 20 || pad.RightTrigger > 20 ||
                    Math.Abs((int)pad.ThumbLX) > 6000 || Math.Abs((int)pad.ThumbLY) > 6000 ||
                    Math.Abs((int)pad.ThumbRX) > 6000 || Math.Abs((int)pad.ThumbRY) > 6000)
                    _manualUntilNs = now + 250_000_000UL;
            }
            _xinputHealthy = true;
        }
        catch (DllNotFoundException) { _xinputHealthy = false; }
        catch (EntryPointNotFoundException) { _xinputHealthy = false; }
        return now < _manualUntilNs;
    }

    private static nint FindGameWindow(out bool focused)
    {
        focused = false;
        try
        {
            foreach (Process process in Process.GetProcessesByName("eurotrucks2"))
            {
                using (process)
                {
                    nint window = process.MainWindowHandle;
                    if (window == 0) continue;
                    if (GetForegroundWindow() == window) focused = true;
                    return window;
                }
            }
        }
        catch (InvalidOperationException) { }
        catch (System.ComponentModel.Win32Exception) { }
        return nint.Zero;
    }

    private static bool IsGameForeground()
    {
        nint foreground = GetForegroundWindow();
        if (foreground == 0) return false;
        _ = GetWindowThreadProcessId(foreground, out uint processId);
        try
        {
            using Process process = Process.GetProcessById((int)processId);
            return string.Equals(process.ProcessName, "eurotrucks2", StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException or
            System.ComponentModel.Win32Exception)
        {
            return false;
        }
    }

    private static bool TryCapture(nint gameWindow, byte[] rgb)
    {
        if (!GetClientRect(gameWindow, out Rect client)) return false;
        Point topLeft = new() { X = client.Left, Y = client.Top };
        Point bottomRight = new() { X = client.Right, Y = client.Bottom };
        if (!ClientToScreen(gameWindow, ref topLeft) || !ClientToScreen(gameWindow, ref bottomRight))
            return false;
        int width = bottomRight.X - topLeft.X;
        int height = bottomRight.Y - topLeft.Y;
        if (width < 160 || height < 90) return false;

        nint screen = GetDC(nint.Zero);
        if (screen == 0) return false;
        nint memory = CreateCompatibleDC(screen);
        nint bitmap = CreateCompatibleBitmap(screen, PerceptionFramePublisher.Width,
            PerceptionFramePublisher.Height);
        nint oldBitmap = 0;
        try
        {
            if (memory == 0 || bitmap == 0) return false;
            oldBitmap = SelectObject(memory, bitmap);
            if (oldBitmap == 0 || oldBitmap == new IntPtr(-1)) return false;
            _ = SetStretchBltMode(memory, 4);
            if (!StretchBlt(memory, 0, 0, PerceptionFramePublisher.Width,
                    PerceptionFramePublisher.Height, screen, topLeft.X, topLeft.Y, width, height,
                    0x00CC0020)) return false;
            _ = SelectObject(memory, oldBitmap);
            oldBitmap = 0;

            byte[] bgra = new byte[PerceptionFramePublisher.Width * PerceptionFramePublisher.Height * 4];
            BitmapInfo info = BitmapInfo.CreateTopDown(PerceptionFramePublisher.Width,
                PerceptionFramePublisher.Height);
            if (GetDIBits(screen, bitmap, 0, PerceptionFramePublisher.Height, bgra, ref info, 0) == 0)
                return false;
            for (int source = 0, target = 0; target < rgb.Length; source += 4, target += 3)
            {
                rgb[target] = bgra[source + 2];
                rgb[target + 1] = bgra[source + 1];
                rgb[target + 2] = bgra[source];
            }
            return true;
        }
        finally
        {
            if (oldBitmap != 0) _ = SelectObject(memory, oldBitmap);
            if (bitmap != 0) _ = DeleteObject(bitmap);
            if (memory != 0) _ = DeleteDC(memory);
            _ = ReleaseDC(nint.Zero, screen);
        }
    }

    private static ulong NowNanoseconds()
    {
        long ticks = Stopwatch.GetTimestamp();
        long frequency = Stopwatch.Frequency;
        ulong seconds = (ulong)(ticks / frequency);
        ulong remainder = (ulong)(ticks % frequency);
        return checked(seconds * 1_000_000_000UL + remainder * 1_000_000_000UL / (ulong)frequency);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _timer.Stop();
        _timer.Tick -= Tick;
        if (_source is not null) _source.RemoveHook(WindowHook);
        _inputPipe.Dispose();
        _gameFrameReader.Dispose();
        _publisher.Dispose();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawInputDevice { internal ushort UsagePage; internal ushort Usage; internal uint Flags; internal nint Target; }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawInputHeader { internal uint Type; internal uint Size; internal nint Device; internal nuint WParam; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { internal int Left; internal int Top; internal int Right; internal int Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Point { internal int X; internal int Y; }

    [StructLayout(LayoutKind.Sequential, Pack = 2)]
    private struct XInputGamepad
    {
        internal ushort Buttons;
        internal byte LeftTrigger;
        internal byte RightTrigger;
        internal short ThumbLX;
        internal short ThumbLY;
        internal short ThumbRX;
        internal short ThumbRY;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct XInputState { internal uint PacketNumber; internal XInputGamepad Gamepad; }

    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfoHeader
    {
        internal uint Size;
        internal int Width;
        internal int Height;
        internal ushort Planes;
        internal ushort BitCount;
        internal uint Compression;
        internal uint SizeImage;
        internal int XPelsPerMeter;
        internal int YPelsPerMeter;
        internal uint ClrUsed;
        internal uint ClrImportant;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct BitmapInfo
    {
        internal BitmapInfoHeader Header;
        internal uint Color;

        internal static BitmapInfo CreateTopDown(int width, int height) => new()
        {
            Header = new BitmapInfoHeader
            {
                Size = (uint)Marshal.SizeOf<BitmapInfoHeader>(), Width = width, Height = -height,
                Planes = 1, BitCount = 32, Compression = 0,
                SizeImage = (uint)(width * height * 4)
            }
        };
    }

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RegisterRawInputDevices([In] RawInputDevice[] devices, uint count, uint size);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetRawInputData(nint rawInput, uint command, nint data, ref uint size, uint headerSize);
    [DllImport("user32.dll")] private static extern nint GetForegroundWindow();
    [DllImport("user32.dll", SetLastError = true)] private static extern uint GetWindowThreadProcessId(nint window, out uint processId);
    [DllImport("user32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool GetClientRect(nint window, out Rect rect);
    [DllImport("user32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool ClientToScreen(nint window, ref Point point);
    [DllImport("user32.dll")] private static extern nint GetDC(nint window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(nint window, nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint CreateCompatibleDC(nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint CreateCompatibleBitmap(nint dc, int width, int height);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern nint SelectObject(nint dc, nint value);
    [DllImport("gdi32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool DeleteObject(nint value);
    [DllImport("gdi32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool DeleteDC(nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern int SetStretchBltMode(nint dc, int mode);
    [DllImport("gdi32.dll", SetLastError = true)] [return: MarshalAs(UnmanagedType.Bool)] private static extern bool StretchBlt(nint dest, int x, int y, int width, int height, nint source, int sourceX, int sourceY, int sourceWidth, int sourceHeight, uint operation);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern int GetDIBits(nint dc, nint bitmap, uint start, uint lines, [Out] byte[] bits, ref BitmapInfo info, uint usage);
    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState", ExactSpelling = true)] private static extern uint XInputGetState(uint index, out XInputState state);
}
