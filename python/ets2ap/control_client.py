"""Fail-closed Windows client for the Control.v1 pipe.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import ctypes
import os
import struct
import sys
import threading
import time
from ctypes import wintypes
from typing import Callable

from .control_protocol import (
    COMMAND_SIZE,
    MAX_PACKET_SIZE,
    MAX_STATUS_AGE_NS,
    STATUS_MAGIC,
    STATUS_PREFIX_SIZE,
    STATUS_V11_SIZE,
    ControlCapabilities,
    ControlStatus,
    REQUIRED_ENGAGE_FLAGS,
    SafetyFlags,
    encode_command,
    parse_status,
)

PIPE_NAME = r"\\.\pipe\ETS2AP_Control_v1"
ERROR_PIPE_BUSY = 231
ERROR_IO_PENDING = 997
ERROR_FILE_NOT_FOUND = 2
ERROR_SEM_TIMEOUT = 121
OPEN_EXISTING = 3
FILE_SHARE_READ = 0x1
FILE_SHARE_WRITE = 0x2
FILE_SHARE_DELETE = 0x4
GENERIC_READ = 0x80000000
GENERIC_WRITE = 0x40000000
FILE_FLAG_OVERLAPPED = 0x40000000
SECURITY_SQOS_PRESENT = 0x00100000
SECURITY_IDENTIFICATION = 0x00010000
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
STATUS_FRAME_TIMEOUT_S = 0.1
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
TOKEN_QUERY = 0x0008
TOKEN_USER_CLASS = 1


class _OverlappedOffset(ctypes.Structure):
    _fields_ = [("offset", wintypes.DWORD), ("offset_high", wintypes.DWORD)]


class _OverlappedUnion(ctypes.Union):
    _fields_ = [("offset", _OverlappedOffset), ("pointer", ctypes.c_void_p)]


class _Overlapped(ctypes.Structure):
    _fields_ = [("internal", ctypes.c_size_t), ("internal_high", ctypes.c_size_t),
                ("position", _OverlappedUnion), ("event", ctypes.c_void_p)]


class ControlClientError(RuntimeError):
    """The control pipe is unavailable, untrusted, or protocol-invalid."""


class _PipeTransport:
    def connect(self, timeout_ms: int) -> None:
        raise NotImplementedError

    def verify_server(self) -> bool:
        raise NotImplementedError

    def write(self, packet: bytes) -> None:
        raise NotImplementedError

    def read(self, size: int, timeout_ms: int = 100) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class _WindowsPipeTransport(_PipeTransport):
    def __init__(self, trusted_server_image: str, pipe_name: str = PIPE_NAME) -> None:
        if sys.platform != "win32":
            raise OSError("Control.v1 named pipes require Windows")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.advapi = ctypes.WinDLL("advapi32", use_last_error=True)
        self._configure()
        self.trusted_server_image = self._canonical_image(trusted_server_image)
        self.user_sid = self._process_sid(self.kernel.GetCurrentProcess())
        self.pipe_name = pipe_name
        self.handle: int | None = None

    def _configure(self) -> None:
        k, a = self.kernel, self.advapi
        for name, args, result in (
            ("WaitNamedPipeW", [wintypes.LPCWSTR, wintypes.DWORD], wintypes.BOOL),
            ("CreateFileW", [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                              ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                              ctypes.c_void_p], ctypes.c_void_p),
            ("GetNamedPipeServerProcessId", [ctypes.c_void_p,
                                              ctypes.POINTER(wintypes.ULONG)], wintypes.BOOL),
            ("OpenProcess", [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], ctypes.c_void_p),
            ("QueryFullProcessImageNameW", [ctypes.c_void_p, wintypes.DWORD,
                                             wintypes.LPWSTR,
                                             ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            ("GetFinalPathNameByHandleW", [ctypes.c_void_p, wintypes.LPWSTR,
                                             wintypes.DWORD, wintypes.DWORD], wintypes.DWORD),
            ("CloseHandle", [ctypes.c_void_p], wintypes.BOOL),
            ("GetCurrentProcess", [], ctypes.c_void_p),
            ("CreateEventW", [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL,
                                wintypes.LPCWSTR], ctypes.c_void_p),
            ("WaitForSingleObject", [ctypes.c_void_p, wintypes.DWORD], wintypes.DWORD),
            ("GetOverlappedResult", [ctypes.c_void_p, ctypes.POINTER(_Overlapped),
                                      ctypes.POINTER(wintypes.DWORD), wintypes.BOOL],
             wintypes.BOOL),
            ("CancelIoEx", [ctypes.c_void_p, ctypes.POINTER(_Overlapped)], wintypes.BOOL),
            ("ReadFile", [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                           ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
            ("WriteFile", [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD,
                            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p], wintypes.BOOL),
        ):
            function = getattr(k, name)
            function.argtypes, function.restype = args, result
        a.OpenProcessToken.argtypes = [ctypes.c_void_p, wintypes.DWORD,
                                       ctypes.POINTER(ctypes.c_void_p)]
        a.OpenProcessToken.restype = wintypes.BOOL
        a.GetTokenInformation.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                          wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        a.GetTokenInformation.restype = wintypes.BOOL
        a.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p,
                                             ctypes.POINTER(ctypes.c_void_p)]
        a.ConvertSidToStringSidW.restype = wintypes.BOOL
        k.LocalFree.argtypes = [ctypes.c_void_p]
        k.LocalFree.restype = ctypes.c_void_p

    def _canonical_image(self, path: str) -> str:
        if not path or not os.path.isfile(path):
            raise ValueError("trusted server executable does not exist")
        handle = self.kernel.CreateFileW(
            os.path.abspath(path), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            None, OPEN_EXISTING, 0, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            buffer = ctypes.create_unicode_buffer(32768)
            length = self.kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 0)
            if not length or length >= len(buffer):
                raise ctypes.WinError(ctypes.get_last_error())
            return buffer.value.upper()
        finally:
            self.kernel.CloseHandle(handle)

    def _process_sid(self, process: int) -> str:
        token = ctypes.c_void_p()
        if not self.advapi.OpenProcessToken(process, TOKEN_QUERY, ctypes.byref(token)):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            needed = wintypes.DWORD()
            self.advapi.GetTokenInformation(token, TOKEN_USER_CLASS, None, 0,
                                            ctypes.byref(needed))
            if not needed.value:
                raise ctypes.WinError(ctypes.get_last_error())
            data = ctypes.create_string_buffer(needed.value)
            if not self.advapi.GetTokenInformation(token, TOKEN_USER_CLASS, data, needed,
                                                   ctypes.byref(needed)):
                raise ctypes.WinError(ctypes.get_last_error())
            sid_pointer = ctypes.cast(data, ctypes.POINTER(ctypes.c_void_p))[0]
            text = ctypes.c_void_p()
            if not self.advapi.ConvertSidToStringSidW(sid_pointer, ctypes.byref(text)):
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                return ctypes.wstring_at(text)
            finally:
                self.kernel.LocalFree(text)
        finally:
            self.kernel.CloseHandle(token)

    def connect(self, timeout_ms: int) -> None:
        if timeout_ms < 1:
            raise ValueError("control pipe connection timeout must be positive")
        deadline = time.monotonic() + timeout_ms / 1000
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("control pipe did not become available")
            wait_ms = max(1, int(remaining * 1000 + 0.999))
            if not self.kernel.WaitNamedPipeW(self.pipe_name, wait_ms):
                error = ctypes.get_last_error()
                if error not in (ERROR_FILE_NOT_FOUND, ERROR_PIPE_BUSY, ERROR_SEM_TIMEOUT):
                    raise ctypes.WinError(error)
                if error == ERROR_FILE_NOT_FOUND:
                    time.sleep(min(0.01, max(0.0, deadline - time.monotonic())))
                continue
            handle = self.kernel.CreateFileW(
                self.pipe_name, GENERIC_READ | GENERIC_WRITE, 0, None, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, None)
            if handle != ctypes.c_void_p(-1).value:
                self.handle = handle
                return
            error = ctypes.get_last_error()
            if error not in (ERROR_FILE_NOT_FOUND, ERROR_PIPE_BUSY):
                raise ctypes.WinError(error)

    def verify_server(self) -> bool:
        if self.handle is None:
            return False
        pid = wintypes.ULONG()
        if not self.kernel.GetNamedPipeServerProcessId(self.handle, ctypes.byref(pid)) or not pid.value:
            return False
        process = self.kernel.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
        if not process:
            return False
        try:
            image = ctypes.create_unicode_buffer(32768)
            size = wintypes.DWORD(len(image))
            if not self.kernel.QueryFullProcessImageNameW(process, 0, image, ctypes.byref(size)):
                return False
            return (self._canonical_image(image.value) == self.trusted_server_image
                    and self._process_sid(process) == self.user_sid)
        except (OSError, ValueError):
            return False
        finally:
            self.kernel.CloseHandle(process)

    def write(self, packet: bytes) -> None:
        if self.handle is None:
            raise OSError("control pipe is not connected")
        offset = 0
        while offset < len(packet):
            chunk = packet[offset:]
            buffer = ctypes.create_string_buffer(chunk)
            written = self._overlapped_io(self.kernel.WriteFile, buffer, len(chunk), 100)
            if written == 0:
                raise EOFError("control pipe closed during write")
            offset += written

    def read(self, size: int, timeout_ms: int = 100) -> bytes:
        if self.handle is None:
            raise OSError("control pipe is not connected")
        buffer = ctypes.create_string_buffer(size)
        received = self._overlapped_io(self.kernel.ReadFile, buffer, size, timeout_ms)
        return buffer.raw[:received]

    def _overlapped_io(self, operation, buffer, size: int, timeout_ms: int) -> int:
        if self.handle is None:
            raise OSError("control pipe is not connected")
        event = self.kernel.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        overlapped = _Overlapped()
        overlapped.event = event
        transferred = wintypes.DWORD()
        try:
            if operation(self.handle, buffer, size, ctypes.byref(transferred),
                         ctypes.byref(overlapped)):
                return transferred.value
            error = ctypes.get_last_error()
            if error != ERROR_IO_PENDING:
                raise ctypes.WinError(error)
            result = self.kernel.WaitForSingleObject(event, max(1, timeout_ms))
            if result == WAIT_TIMEOUT:
                self.kernel.CancelIoEx(self.handle, ctypes.byref(overlapped))
                self.kernel.GetOverlappedResult(self.handle, ctypes.byref(overlapped),
                                                 ctypes.byref(transferred), True)
                raise TimeoutError("control pipe I/O exceeded its deadline")
            if result != WAIT_OBJECT_0:
                raise ctypes.WinError(ctypes.get_last_error())
            if not self.kernel.GetOverlappedResult(self.handle, ctypes.byref(overlapped),
                                                   ctypes.byref(transferred), False):
                raise ctypes.WinError(ctypes.get_last_error())
            return transferred.value
        finally:
            self.kernel.CloseHandle(event)

    def close(self) -> None:
        if self.handle is not None:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class ControlClient:
    """Synchronous pipe client; callers must schedule sends independently of status reads."""

    def __init__(self, trusted_server_image: str, *,
                 authorize_engage: Callable[[], bool] | None = None,
                 pipe: _PipeTransport | None = None,
                 pipe_name: str = PIPE_NAME,
                 now_ns: Callable[[], int] = time.perf_counter_ns) -> None:
        self._pipe = (pipe if pipe is not None else
                      _WindowsPipeTransport(trusted_server_image, pipe_name))
        self._authorize_engage = authorize_engage or (lambda: False)
        self._now_ns = now_ns
        self._connected = False
        self._sequence = 0
        self._last_command_timestamp_ns = 0
        self._last_status_timestamp_ns = 0
        self._status_capabilities = ControlCapabilities(0)
        self._send_lock = threading.Lock()
        self._sequence_lock = threading.Lock()
        self._read_lock = threading.Lock()
        self._close_lock = threading.Lock()

    def connect(self, timeout_ms: int = 1000) -> None:
        if self._connected:
            return
        self._sequence = 0
        self._last_command_timestamp_ns = 0
        self._last_status_timestamp_ns = 0
        self._status_capabilities = ControlCapabilities(0)
        try:
            self._pipe.connect(timeout_ms)
            if not self._pipe.verify_server():
                raise ControlClientError("control server identity check failed")
            with self._send_lock:
                self._send_locked(0.0, 0.0, 0.0, False)
                self._connected = True
        except Exception as error:
            self._close_transport()
            if isinstance(error, ControlClientError):
                raise
            raise ControlClientError("control pipe connection failed") from error

    def send_command(self, steering: float, throttle: float, brake: float,
                     engage: bool = False,
                     safety_flags: SafetyFlags | int = 0) -> int:
        with self._send_lock:
            if not self._connected:
                raise ControlClientError("control pipe is not connected")
            if engage:
                if not self._authorize_engage():
                    raise ControlClientError("engagement has not been explicitly authorized")
                if not self._status_capabilities & ControlCapabilities.SAFETY_FLAGS_V11:
                    raise ControlClientError("server does not advertise v1.1 safety-flag support")
                if self._now_ns() - self._last_status_timestamp_ns > MAX_STATUS_AGE_NS:
                    raise ControlClientError("server safety status is not fresh")
                if (int(safety_flags) & REQUIRED_ENGAGE_FLAGS) != REQUIRED_ENGAGE_FLAGS:
                    raise ControlClientError("engagement is missing required safety claims")
                if int(safety_flags) & (1 << 5):
                    raise ControlClientError("manual input is active")
            return self._send_locked(steering, throttle, brake, engage, safety_flags)

    def _send_locked(self, steering: float, throttle: float, brake: float,
                     engage: bool, safety_flags: SafetyFlags | int = 0) -> int:
        sequence = self._sequence + 1
        timestamp_ns = self._now_ns()
        if timestamp_ns <= self._last_command_timestamp_ns:
            self._close_transport()
            raise ControlClientError("monotonic clock did not advance")
        try:
            packet = encode_command(sequence, timestamp_ns, steering, throttle, brake,
                                    engage, safety_flags=safety_flags)
            if not COMMAND_SIZE <= len(packet) <= MAX_PACKET_SIZE:
                raise ControlClientError("encoder returned an invalid command frame")
            with self._sequence_lock:
                self._sequence = sequence
                self._last_command_timestamp_ns = timestamp_ns
            self._pipe.write(packet)
        except Exception as error:
            self._close_transport()
            if isinstance(error, ControlClientError):
                raise
            raise ControlClientError("control pipe write failed") from error
        return sequence

    def read_status(self) -> ControlStatus:
        with self._read_lock:
            return self._read_status_locked()

    def _read_status_locked(self) -> ControlStatus:
        if not self._connected:
            raise ControlClientError("control pipe is not connected")
        try:
            first_byte = self._pipe.read(1, 100)
            if not first_byte:
                raise EOFError("control pipe closed during frame")
            frame_deadline = time.monotonic() + STATUS_FRAME_TIMEOUT_S
            header = first_byte + self._read_exact(15, deadline=frame_deadline)
            magic, major, minor, packet_size, reserved = struct.unpack("<IHHII", header)
            if magic != STATUS_MAGIC or major != 1 or reserved != 0:
                raise ControlClientError("invalid status header")
            if not STATUS_PREFIX_SIZE <= packet_size <= MAX_PACKET_SIZE:
                raise ControlClientError("status packet size is outside the supported range")
            if minor == 0 and packet_size != STATUS_PREFIX_SIZE:
                raise ControlClientError("v1.0 status packet must be exactly 64 bytes")
            if minor > 0 and packet_size < STATUS_V11_SIZE:
                raise ControlClientError("v1.1 status packet is missing its capability field")
            packet = header + self._read_exact(packet_size - len(header),
                                               deadline=frame_deadline)
            status = parse_status(
                packet,
                now_ns=self._now_ns(),
                previous_timestamp_ns=self._last_status_timestamp_ns,
            )
            with self._sequence_lock:
                sent_sequence = self._sequence
            if status.last_accepted_sequence > sent_sequence:
                raise ControlClientError("status acknowledges an unsent command")
            self._last_status_timestamp_ns = status.timestamp_ns
            self._status_capabilities = status.capabilities
            return status
        except Exception as error:
            self._close_transport()
            if isinstance(error, ControlClientError):
                raise
            raise ControlClientError("control status read failed") from error

    def _read_exact(self, size: int, *, deadline: float) -> bytes:
        result = bytearray()
        while len(result) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("control status frame exceeded its deadline")
            timeout_ms = max(1, int(remaining * 1000 + 0.999))
            chunk = self._pipe.read(size - len(result), timeout_ms)
            if not chunk:
                raise EOFError("control pipe closed during frame")
            result.extend(chunk)
        return bytes(result)

    def close(self) -> None:
        self._close_transport()

    def _close_transport(self) -> None:
        with self._close_lock:
            self._connected = False
            try:
                self._pipe.close()
            except Exception:
                pass
