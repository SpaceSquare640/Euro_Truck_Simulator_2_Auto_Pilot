"""Read the UI-owned ETS2 perception shared mapping using a seqlock snapshot.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import ctypes
import sys
import time

from .perception_protocol import MAX_PACKET_SIZE, PerceptionFrame, decode_frame

MAPPING_NAME = "Local\\ETS2AP_Perception_v1"
FILE_MAP_READ = 0x0004
SEQUENCE_OFFSET = 16


class PerceptionReader:
    """Read-only consumer; it never creates a missing shared mapping."""

    def __init__(self) -> None:
        if sys.platform != "win32":
            raise OSError("Windows is required")
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._kernel32.OpenFileMappingW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        self._kernel32.OpenFileMappingW.restype = ctypes.c_void_p
        self._kernel32.MapViewOfFile.argtypes = [
            ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t,
        ]
        self._kernel32.MapViewOfFile.restype = ctypes.c_void_p
        self._kernel32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
        self._kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        self._previous_sequence = 0

    def read(self) -> PerceptionFrame | None:
        handle = self._kernel32.OpenFileMappingW(FILE_MAP_READ, False, MAPPING_NAME)
        if not handle:
            return None
        try:
            view = self._kernel32.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, MAX_PACKET_SIZE)
            if not view:
                return None
            try:
                deadline = time.perf_counter_ns() + 10_000_000
                while time.perf_counter_ns() < deadline:
                    first = ctypes.c_uint64.from_address(view + SEQUENCE_OFFSET).value
                    if first == 0 or first & 1:
                        continue
                    packet = ctypes.string_at(view, MAX_PACKET_SIZE)
                    second = ctypes.c_uint64.from_address(view + SEQUENCE_OFFSET).value
                    if first != second or second & 1:
                        continue
                    try:
                        frame = decode_frame(
                            packet,
                            now_ns=time.perf_counter_ns(),
                            previous_sequence=self._previous_sequence,
                        )
                    except ValueError:
                        return None
                    self._previous_sequence = frame.sequence
                    return frame
                return None
            finally:
                self._kernel32.UnmapViewOfFile(view)
        finally:
            self._kernel32.CloseHandle(handle)
