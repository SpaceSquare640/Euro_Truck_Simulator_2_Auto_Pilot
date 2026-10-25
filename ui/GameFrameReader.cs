// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Buffers.Binary;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Threading;

namespace ETS2AP.Ui;

internal sealed class GameFrameReader : IDisposable
{
    internal const string MappingName = "Local\\ETS2AP_GameFrame_v1";
    private const int Width = 160;
    private const int Height = 90;
    private const int HeaderSize = 64;
    private const int PixelBytes = Width * Height * 3;
    private const int PacketSize = HeaderSize + PixelBytes;
    private const ulong MaximumAgeNs = 150_000_000;
    private const ulong RetryIntervalNs = 1_000_000_000;
    private const uint FrameValid = 1;
    private readonly byte[] _snapshot = new byte[PacketSize];
    private MemoryMappedFile? _mapping;
    private MemoryMappedViewAccessor? _view;
    private ulong _retryAfterNs;
    private ulong _lastSequence;
    private ulong _lastTimestampNs;

    internal bool TryRead(byte[] rgb, ulong nowNs)
    {
        if (rgb.Length != PixelBytes || nowNs == 0) return false;
        if (_view is null && !TryOpen(nowNs)) return false;

        try
        {
            MemoryMappedViewAccessor view = _view!;
            ulong firstSequence = view.ReadUInt64(16);
            if (firstSequence == 0 || (firstSequence & 1) != 0) return false;
            Thread.MemoryBarrier();
            if (view.ReadArray(0, _snapshot, 0, _snapshot.Length) != _snapshot.Length)
                return false;
            Thread.MemoryBarrier();
            ulong secondSequence = view.ReadUInt64(16);
            ReadOnlySpan<byte> packet = _snapshot;
            if (firstSequence != secondSequence ||
                BinaryPrimitives.ReadUInt64LittleEndian(packet.Slice(16, 8)) != firstSequence ||
                !packet.Slice(0, 4).SequenceEqual("E2GF"u8) ||
                BinaryPrimitives.ReadUInt16LittleEndian(packet.Slice(4, 2)) != 1 ||
                BinaryPrimitives.ReadUInt16LittleEndian(packet.Slice(6, 2)) != 0 ||
                BinaryPrimitives.ReadUInt32LittleEndian(packet.Slice(8, 4)) != HeaderSize ||
                BinaryPrimitives.ReadUInt32LittleEndian(packet.Slice(12, 4)) != FrameValid ||
                BinaryPrimitives.ReadUInt16LittleEndian(packet.Slice(32, 2)) != Width ||
                BinaryPrimitives.ReadUInt16LittleEndian(packet.Slice(34, 2)) != Height ||
                BinaryPrimitives.ReadUInt32LittleEndian(packet.Slice(36, 4)) != 1 ||
                BinaryPrimitives.ReadUInt32LittleEndian(packet.Slice(40, 4)) != PixelBytes ||
                packet.Slice(44, 20).IndexOfAnyExcept((byte)0) >= 0)
                return false;

            ulong timestampNs = BinaryPrimitives.ReadUInt64LittleEndian(packet.Slice(24, 8));
            if (firstSequence < _lastSequence || timestampNs == 0 ||
                timestampNs < _lastTimestampNs || timestampNs > nowNs ||
                nowNs - timestampNs > MaximumAgeNs)
                return false;
            if (firstSequence == _lastSequence && timestampNs != _lastTimestampNs)
                return false;
            packet.Slice(HeaderSize, PixelBytes).CopyTo(rgb);
            _lastSequence = firstSequence;
            _lastTimestampNs = timestampNs;
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or
            ObjectDisposedException or ArgumentException or OverflowException)
        {
            CloseMapping();
            _retryAfterNs = nowNs + RetryIntervalNs;
            return false;
        }
    }

    private bool TryOpen(ulong nowNs)
    {
        if (nowNs < _retryAfterNs) return false;
        try
        {
            _mapping = MemoryMappedFile.OpenExisting(MappingName, MemoryMappedFileRights.Read);
            _view = _mapping.CreateViewAccessor(0, PacketSize, MemoryMappedFileAccess.Read);
            _retryAfterNs = 0;
            return true;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or
            ObjectDisposedException or ArgumentException)
        {
            CloseMapping();
            _retryAfterNs = nowNs + RetryIntervalNs;
            return false;
        }
    }

    private void CloseMapping()
    {
        _view?.Dispose();
        _view = null;
        _mapping?.Dispose();
        _mapping = null;
        _lastSequence = 0;
        _lastTimestampNs = 0;
    }

    public void Dispose() => CloseMapping();
}
