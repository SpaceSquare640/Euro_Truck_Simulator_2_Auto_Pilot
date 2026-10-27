// Edited by: OpenAI-Codex_Responsible: Source_Code
using System.Buffers.Binary;

namespace ETS2AP.Ui;

internal static class PhysicalInputPipeProtocol
{
    internal const int FrameSize = 24;
    private const ushort Version = 1;
    private const ushort RawInputAvailableFlag = 0x0001;
    private const ushort ManualInputActiveFlag = 0x0002;

    internal static byte[] CreateFrame(ulong sequence, bool rawInputAvailable,
        bool manualInputActive)
    {
        if (sequence == 0) throw new ArgumentOutOfRangeException(nameof(sequence));
        if (!rawInputAvailable) manualInputActive = true;

        byte[] frame = new byte[FrameSize];
        "E2PI"u8.CopyTo(frame);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(4), Version);
        ushort flags = (ushort)((rawInputAvailable ? RawInputAvailableFlag : 0) |
            (manualInputActive ? ManualInputActiveFlag : 0));
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(6), flags);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(8), FrameSize);
        BinaryPrimitives.WriteUInt64LittleEndian(frame.AsSpan(12), sequence);
        return frame;
    }
}
