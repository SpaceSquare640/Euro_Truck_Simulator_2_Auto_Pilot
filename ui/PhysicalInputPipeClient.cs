// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Principal;

namespace ETS2AP.Ui;

internal sealed class PhysicalInputPipeClient : IDisposable
{
    private const string PipeName = "ETS2AP_PhysicalInput_v1";
    private readonly string? _trustedServerImage;
    private readonly CancellationTokenSource _cancellation = new();
    private readonly SemaphoreSlim _publishSignal = new(0, 1);
    private readonly object _stateLock = new();
    private Task? _worker;
    private bool _rawInputAvailable;
    private bool _manualInputActive = true;
    private bool _disposed;

    internal PhysicalInputPipeClient(string? trustedServerImage)
    {
        _trustedServerImage = CanonicalPathOrNull(trustedServerImage);
    }

    internal void Start()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _worker ??= Task.Run(() => RunAsync(_cancellation.Token));
    }

    internal void Publish(bool rawInputAvailable, bool manualInputActive)
    {
        lock (_stateLock)
        {
            _rawInputAvailable = rawInputAvailable;
            _manualInputActive = !rawInputAvailable || manualInputActive;
        }
        try { _publishSignal.Release(); }
        catch (SemaphoreFullException) { }
    }

    private async Task RunAsync(CancellationToken cancellation)
    {
        while (!cancellation.IsCancellationRequested)
        {
            try
            {
                if (_trustedServerImage is null)
                    throw new UnauthorizedAccessException("No trusted control server image is configured.");

                using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.Out,
                    PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1000, cancellation);
                if (!VerifyServer(pipe))
                    throw new UnauthorizedAccessException("Untrusted physical-input pipe server.");

                ulong sequence = 0;
                while (!cancellation.IsCancellationRequested)
                {
                    await _publishSignal.WaitAsync(cancellation);
                    bool available;
                    bool manual;
                    lock (_stateLock)
                    {
                        available = _rawInputAvailable;
                        manual = _manualInputActive;
                    }

                    byte[] frame = PhysicalInputPipeProtocol.CreateFrame(
                        checked(++sequence), available, manual);
                    await pipe.WriteAsync(frame, cancellation);
                    await pipe.FlushAsync(cancellation);
                }
            }
            catch (Exception error) when (error is IOException or TimeoutException or
                UnauthorizedAccessException or InvalidOperationException or
                OperationCanceledException or OverflowException)
            {
                if (cancellation.IsCancellationRequested) break;
                try { await Task.Delay(500, cancellation); }
                catch (OperationCanceledException) { break; }
            }
        }
    }

    private bool VerifyServer(NamedPipeClientStream pipe)
    {
        if (!GetNamedPipeServerProcessId(pipe.SafePipeHandle.DangerousGetHandle(), out uint pid) || pid == 0)
            return false;
        try
        {
            using Process process = Process.GetProcessById((int)pid);
            string? image = process.MainModule?.FileName;
            if (image is null || CanonicalPathOrNull(image) != _trustedServerImage) return false;
            if (!OpenProcessToken(process.Handle, 0x0008, out nint token)) return false;
            try
            {
                using var identity = new WindowsIdentity(token);
                SecurityIdentifier? currentUser = WindowsIdentity.GetCurrent().User;
                return currentUser is not null && identity.User?.Equals(currentUser) == true;
            }
            finally { CloseHandle(token); }
        }
        catch (Exception error) when (error is ArgumentException or InvalidOperationException or
            System.ComponentModel.Win32Exception or IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    internal static string? CanonicalPathOrNull(string? path)
    {
        if (string.IsNullOrWhiteSpace(path)) return null;
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete);
            var buffer = new char[32768];
            uint length = GetFinalPathNameByHandleW(stream.SafeFileHandle.DangerousGetHandle(),
                buffer, (uint)buffer.Length, 0x00000002);
            if (length == 0 || length >= buffer.Length) return null;
            return new string(buffer, 0, (int)length).ToUpperInvariant();
        }
        catch (Exception error) when (error is ArgumentException or IOException or
            UnauthorizedAccessException or NotSupportedException)
        {
            return null;
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _cancellation.Cancel();
        try { _worker?.GetAwaiter().GetResult(); }
        catch (OperationCanceledException) { }
        _publishSignal.Dispose();
        _cancellation.Dispose();
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeServerProcessId(nint pipe, out uint serverProcessId);

    [DllImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool OpenProcessToken(nint process, uint access, out nint token);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(nint handle);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandleW(nint file, [Out] char[] path,
        uint length, uint flags);
}
