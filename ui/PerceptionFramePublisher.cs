// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Buffers.Binary;
using System.IO.MemoryMappedFiles;

namespace ETS2AP.Ui;

[Flags]
internal enum PerceptionFrameFlags : uint
{
    None = 0,
    GameFocused = 1 << 0,
    InputMonitorHealthy = 1 << 1,
    ManualInputActive = 1 << 2,
    FrameValid = 1 << 3
}

internal sealed class PerceptionFramePublisher : IDisposable
{
    internal const string MappingName = "Local\\ETS2AP_Perception_v1";
    internal const int Width = 160;
    internal const int Height = 90;
    internal const int HeaderSize = 64;
    internal const int PixelBytes = Width * Height * 3;
    internal const int PacketSize = HeaderSize + PixelBytes;
    private const string MutexName = "Local\\ETS2AP_Perception_v1.lock";
    private const uint PixelFormatRgb24 = 1;
    private readonly MemoryMappedFile _mapping;
    private readonly MemoryMappedViewAccessor _view;
    private readonly Mutex _writerMutex;
    private bool _disposed;

    internal PerceptionFramePublisher()
    {
        _mapping = MemoryMappedFile.CreateOrOpen(MappingName, PacketSize,
            MemoryMappedFileAccess.ReadWrite);
        _view = _mapping.CreateViewAccessor(0, PacketSize, MemoryMappedFileAccess.ReadWrite);
        _writerMutex = new Mutex(false, MutexName);
    }

    internal void Publish(ReadOnlySpan<byte> rgb, ulong timestampNs, PerceptionFrameFlags flags)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (rgb.Length != PixelBytes || timestampNs == 0 ||
            ((uint)flags & ~0xFU) != 0)
            throw new ArgumentException("Invalid perception frame.");

        bool ownsMutex = false;
        try
        {
            try { ownsMutex = _writerMutex.WaitOne(10); }
            catch (AbandonedMutexException) { ownsMutex = true; }
            if (!ownsMutex) return;

            ulong current = _view.ReadUInt64(16);
            ulong evenSequence = checked((current & ~1UL) + 2UL);
            ulong oddSequence = evenSequence - 1UL;
            byte[] packet = CreatePacket(rgb, timestampNs, flags, evenSequence);
            _view.Write(16, oddSequence);
            _view.WriteArray(0, packet, 0, 16);
            _view.WriteArray(24, packet, 24, PacketSize - 24);
            Thread.MemoryBarrier();
            _view.Write(16, evenSequence);
        }
        finally
        {
            if (ownsMutex) _writerMutex.ReleaseMutex();
        }
    }

    internal static byte[] CreatePacket(ReadOnlySpan<byte> rgb, ulong timestampNs,
        PerceptionFrameFlags flags, ulong sequence)
    {
        if (rgb.Length != PixelBytes || timestampNs == 0 || sequence == 0 ||
            (sequence & 1UL) != 0 || ((uint)flags & ~0xFU) != 0)
            throw new ArgumentException("Invalid perception frame.");

        byte[] packet = new byte[PacketSize];
        "E2PF"u8.CopyTo(packet);
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(6), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(8), HeaderSize);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(12), (uint)flags);
        BinaryPrimitives.WriteUInt64LittleEndian(packet.AsSpan(16), sequence);
        BinaryPrimitives.WriteUInt64LittleEndian(packet.AsSpan(24), timestampNs);
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(32), Width);
        BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(34), Height);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(36), PixelFormatRgb24);
        BinaryPrimitives.WriteUInt32LittleEndian(packet.AsSpan(40), PixelBytes);
        rgb.CopyTo(packet.AsSpan(HeaderSize));
        return packet;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _writerMutex.Dispose();
        _view.Dispose();
        _mapping.Dispose();
    }
}
