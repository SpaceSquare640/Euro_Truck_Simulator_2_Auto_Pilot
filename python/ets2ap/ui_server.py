"""Development UI status server for the Windows UI v1 named pipe.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import argparse
import ctypes
import msvcrt
import os
import struct
import sys
import time
from ctypes import wintypes

from .telemetry import TelemetryReader, TelemetrySummary
from .ui_protocol import MAX_FRAME, frame, message, parse

PIPE_NAME = r"\\.\pipe\ETS2AP_UI_v1"
ERROR_IO_PENDING = 997
ERROR_PIPE_CONNECTED = 535
ERROR_BROKEN_PIPE = 109
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258
PIPE_ACCESS_DUPLEX = 0x00000003
FILE_FLAG_OVERLAPPED = 0x40000000
FILE_FLAG_FIRST_PIPE_INSTANCE = 0x00080000
PIPE_TYPE_BYTE = 0
PIPE_READMODE_BYTE = 0
PIPE_WAIT = 0
PIPE_REJECT_REMOTE_CLIENTS = 0x00000008
TOKEN_QUERY = 0x0008
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class OVERLAPPED(ctypes.Structure):
    _fields_ = [("Internal", ctypes.c_void_p), ("InternalHigh", ctypes.c_void_p),
                ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD),
                ("hEvent", ctypes.c_void_p)]


class SECURITY_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("nLength", wintypes.DWORD), ("lpSecurityDescriptor", ctypes.c_void_p),
                ("bInheritHandle", wintypes.BOOL)]


class TOKEN_USER(ctypes.Structure):
    _fields_ = [("Sid", ctypes.c_void_p), ("Attributes", wintypes.DWORD)]


class WinPipe:
    def __init__(self, trusted_ui_image: str):
        if sys.platform != "win32":
            raise OSError("Windows is required")
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.advapi = ctypes.WinDLL("advapi32", use_last_error=True)
        self._configure()
        self.trusted_ui_image = self._canonical_image(trusted_ui_image)
        self.user_sid = self._process_sid(self.kernel.GetCurrentProcess())
        descriptor = ctypes.c_void_p()
        sddl = f"D:P(A;;GA;;;{self.user_sid})"
        if not self.advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, 1, ctypes.byref(descriptor), None):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            security = SECURITY_ATTRIBUTES(ctypes.sizeof(SECURITY_ATTRIBUTES), descriptor, False)
            self.handle = self.kernel.CreateNamedPipeW(
                PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, MAX_FRAME + 4, MAX_FRAME + 4, 0, ctypes.byref(security))
            if self.handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            self.kernel.LocalFree(descriptor)

    def _configure(self) -> None:
        k, a = self.kernel, self.advapi
        for name, args, result in (
            ("CreateNamedPipeW", [ctypes.c_wchar_p, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p], ctypes.c_void_p),
            ("CreateEventW", [ctypes.c_void_p, wintypes.BOOL, wintypes.BOOL, ctypes.c_wchar_p], ctypes.c_void_p),
            ("ConnectNamedPipe", [ctypes.c_void_p, ctypes.POINTER(OVERLAPPED)], wintypes.BOOL),
            ("ReadFile", [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
                          ctypes.POINTER(OVERLAPPED)], wintypes.BOOL),
            ("WriteFile", [ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
                           ctypes.POINTER(OVERLAPPED)], wintypes.BOOL),
            ("GetOverlappedResult", [ctypes.c_void_p, ctypes.POINTER(OVERLAPPED), ctypes.POINTER(wintypes.DWORD),
                                     wintypes.BOOL], wintypes.BOOL),
            ("CancelIoEx", [ctypes.c_void_p, ctypes.POINTER(OVERLAPPED)], wintypes.BOOL),
            ("GetNamedPipeClientProcessId", [ctypes.c_void_p, ctypes.POINTER(wintypes.ULONG)], wintypes.BOOL),
            ("OpenProcess", [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], ctypes.c_void_p),
            ("GetCurrentProcess", [], ctypes.c_void_p),
            ("QueryFullProcessImageNameW", [ctypes.c_void_p, wintypes.DWORD, ctypes.c_wchar_p,
                                             ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            ("GetFinalPathNameByHandleW", [ctypes.c_void_p, ctypes.c_wchar_p, wintypes.DWORD,
                                             wintypes.DWORD], wintypes.DWORD),
            ("CloseHandle", [ctypes.c_void_p], wintypes.BOOL),
            ("DisconnectNamedPipe", [ctypes.c_void_p], wintypes.BOOL),
            ("WaitForSingleObject", [ctypes.c_void_p, wintypes.DWORD], wintypes.DWORD),
        ):
            function = getattr(k, name)
            function.argtypes, function.restype = args, result
        a.GetTokenInformation.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
                                         wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        a.GetTokenInformation.restype = wintypes.BOOL
        a.OpenProcessToken.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(ctypes.c_void_p)]
        a.OpenProcessToken.restype = wintypes.BOOL
        a.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        a.ConvertSidToStringSidW.restype = wintypes.BOOL
        a.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [ctypes.c_wchar_p, wintypes.DWORD,
                                                                            ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
        a.ConvertStringSecurityDescriptorToSecurityDescriptorW.restype = wintypes.BOOL
        k.LocalFree.argtypes = [ctypes.c_void_p]

    def _canonical_image(self, path: str) -> str:
        if not os.path.isfile(path):
            raise ValueError("trusted executable does not exist")
        with open(path, "rb") as source:
            buffer = ctypes.create_unicode_buffer(32768)
            handle = msvcrt.get_osfhandle(source.fileno())
            length = self.kernel.GetFinalPathNameByHandleW(handle, buffer, len(buffer), 0)
            if not length or length >= len(buffer):
                raise ctypes.WinError(ctypes.get_last_error())
            return buffer.value.upper()

    def _process_sid(self, process: int) -> str:
        token = ctypes.c_void_p()
        if not self.advapi.OpenProcessToken(process, TOKEN_QUERY, ctypes.byref(token)):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            needed = wintypes.DWORD()
            self.advapi.GetTokenInformation(token, 1, None, 0, ctypes.byref(needed))
            buffer = ctypes.create_string_buffer(needed.value)
            if not self.advapi.GetTokenInformation(token, 1, buffer, needed, ctypes.byref(needed)):
                raise ctypes.WinError(ctypes.get_last_error())
            sid = TOKEN_USER.from_buffer(buffer).Sid
            text = ctypes.c_void_p()
            if not self.advapi.ConvertSidToStringSidW(sid, ctypes.byref(text)):
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                return ctypes.wstring_at(text)
            finally:
                self.kernel.LocalFree(text)
        finally:
            self.kernel.CloseHandle(token)

    def connect(self) -> None:
        event = self.kernel.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            operation = OVERLAPPED(hEvent=event)
            if not self.kernel.ConnectNamedPipe(self.handle, ctypes.byref(operation)):
                error = ctypes.get_last_error()
                if error == ERROR_PIPE_CONNECTED:
                    return
                if error != ERROR_IO_PENDING:
                    raise ctypes.WinError(error)
                self._finish(operation, 0xFFFFFFFF)
        finally:
            self.kernel.CloseHandle(event)

    def verify_client(self) -> bool:
        pid = wintypes.ULONG()
        if not self.kernel.GetNamedPipeClientProcessId(self.handle, ctypes.byref(pid)) or not pid.value:
            return False
        process = self.kernel.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid.value)
        if not process:
            return False
        try:
            buffer = ctypes.create_unicode_buffer(32768)
            size = wintypes.DWORD(len(buffer))
            if not self.kernel.QueryFullProcessImageNameW(process, 0, buffer, ctypes.byref(size)):
                return False
            path = self._canonical_image(buffer.value)
            return path == self.trusted_ui_image and self._process_sid(process) == self.user_sid
        except (OSError, ValueError):
            return False
        finally:
            self.kernel.CloseHandle(process)

    def _finish(self, operation: OVERLAPPED, timeout_ms: int) -> int:
        result = self.kernel.WaitForSingleObject(operation.hEvent, timeout_ms)
        if result != WAIT_OBJECT_0:
            self.kernel.CancelIoEx(self.handle, ctypes.byref(operation))
            transferred = wintypes.DWORD()
            completed = self.kernel.GetOverlappedResult(
                self.handle, ctypes.byref(operation), ctypes.byref(transferred), True)
            if completed:
                return transferred.value
            if result == WAIT_TIMEOUT:
                raise TimeoutError("pipe I/O timeout")
            raise ctypes.WinError(ctypes.get_last_error())
        transferred = wintypes.DWORD()
        if not self.kernel.GetOverlappedResult(self.handle, ctypes.byref(operation), ctypes.byref(transferred), False):
            raise ctypes.WinError(ctypes.get_last_error())
        return transferred.value

    def _io(self, data: bytes | int, *, write: bool, timeout_ms: int) -> bytes | int:
        event = self.kernel.CreateEventW(None, True, False, None)
        if not event:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            operation = OVERLAPPED(hEvent=event)
            buffer = ctypes.create_string_buffer(data if isinstance(data, bytes) else data)
            length = len(data) if isinstance(data, bytes) else data
            action = self.kernel.WriteFile if write else self.kernel.ReadFile
            immediate = wintypes.DWORD()
            if action(self.handle, buffer, length, ctypes.byref(immediate), ctypes.byref(operation)):
                count = immediate.value
            elif ctypes.get_last_error() == ERROR_IO_PENDING:
                count = self._finish(operation, timeout_ms)
            else:
                raise ctypes.WinError(ctypes.get_last_error())
            return count if write else buffer.raw[:count]
        finally:
            self.kernel.CloseHandle(event)

    def read_frame(self, idle_ms: int = 500) -> dict | None:
        try:
            first = self._io(4, write=False, timeout_ms=idle_ms)
        except TimeoutError:
            return None
        if not first:
            raise EOFError
        started = time.monotonic()
        prefix = bytearray(first)
        while len(prefix) < 4:
            remaining = int(1000 - (time.monotonic() - started) * 1000)
            if remaining <= 0:
                raise TimeoutError("frame timeout")
            chunk = self._io(4 - len(prefix), write=False, timeout_ms=remaining)
            if not chunk:
                raise EOFError
            prefix.extend(chunk)
        length = struct.unpack("<I", prefix)[0]
        if not 1 <= length <= MAX_FRAME:
            raise ValueError("invalid frame length")
        payload = bytearray()
        while len(payload) < length:
            remaining = max(1, int(1000 - (time.monotonic() - started) * 1000))
            if time.monotonic() - started >= 1:
                raise TimeoutError("frame timeout")
            chunk = self._io(length - len(payload), write=False, timeout_ms=remaining)
            if not chunk:
                raise EOFError
            payload.extend(chunk)
        return parse(bytes(payload))

    def send(self, value: dict) -> None:
        encoded = frame(value)
        offset = 0
        while offset < len(encoded):
            count = self._io(encoded[offset:], write=True, timeout_ms=1000)
            if not count:
                raise EOFError
            offset += count

    def close(self) -> None:
        self.kernel.CloseHandle(self.handle)


def status_payload(summary: TelemetrySummary | None, revision: int) -> dict:
    if summary is None or summary.speed_mps is None or summary.paused is None:
        error = "TELEMETRY_UNAVAILABLE"
    elif summary.telemetry_age_s is None or summary.telemetry_age_s > 0.25:
        error = "TELEMETRY_STALE"
    else:
        error = "CONTROL_UNAVAILABLE"
    return {"state": "fault", "error_code": error, "state_revision": revision,
            "telemetry": {"speed_mps": summary.speed_mps if summary else None,
                          "speed_limit_mps": summary.speed_limit_mps if summary else None,
                          "navigation_distance_m": summary.navigation_distance_m if summary else None,
                          "telemetry_age_s": summary.telemetry_age_s if summary else None}}


def serve(trusted_ui_image: str) -> None:
    pipe = WinPipe(trusted_ui_image)
    reader = TelemetryReader()
    try:
        while True:
            pipe.connect()
            try:
                if not pipe.verify_client():
                    continue
                hello = pipe.read_frame(1000)
                if (hello is None or hello["type"] != "hello" or
                        hello["version"].split(".")[0] != "1" or
                        not hello["id"].startswith("u-")):
                    continue
                versions = hello["payload"].get("supported_versions")
                if (not isinstance(versions, list) or not versions or
                        any(not isinstance(version, str) for version in versions) or
                        "1.0" not in versions):
                    pipe.send(message("error", hello["id"], {"code": "UNSUPPORTED_VERSION"}))
                    continue
                pipe.send(message("hello_ack", hello["id"], {"selected_version": "1.0"}))
                counter = 0
                pipe.send(message("config", "p-1", {"config": {"target_speed_mps": 0}}))
                last_state = None
                revision = 0
                seen_requests = {hello["id"]}
                while True:
                    summary = reader.read()
                    candidate = status_payload(summary, revision)
                    state_key = (candidate["state"], candidate["error_code"])
                    if last_state is not None and state_key != last_state:
                        revision += 1
                    candidate["state_revision"] = revision
                    last_state = state_key
                    counter += 1
                    pipe.send(message("status", f"p-{counter + 1}", candidate))
                    request = pipe.read_frame(250)
                    if request is None:
                        continue
                    if not request["id"].startswith("u-") or request["id"] in seen_requests:
                        raise ValueError("invalid request envelope")
                    seen_requests.add(request["id"])
                    if request["version"].split(".")[0] != "1":
                        pipe.send(message("error", request["id"], {"code": "UNSUPPORTED_VERSION"}))
                        raise ValueError("unsupported version")
                    kind = request["type"]
                    if kind == "get_status" and request["payload"] == {}:
                        pipe.send(message("status", request["id"], candidate))
                    elif kind == "disengage" and request["payload"] == {}:
                        pipe.send(message("status", request["id"], candidate))
                    elif kind == "engage":
                        requested_revision = request["payload"].get("state_revision")
                        code = ("CONTROL_UNAVAILABLE" if type(requested_revision) is int and requested_revision >= 0
                                else "MALFORMED_MESSAGE")
                        pipe.send(message("error", request["id"], {"code": code}))
                    else:
                        pipe.send(message("error", request["id"], {"code": "MALFORMED_MESSAGE"}))
            except (EOFError, OSError, ValueError, TimeoutError):
                pass
            finally:
                pipe.kernel.DisconnectNamedPipe(pipe.handle)
    finally:
        pipe.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="ETS2AP UI status service")
    parser.add_argument("--trusted-ui-image", required=True, help="canonical path of the trusted UI executable")
    args = parser.parse_args()
    serve(args.trusted_ui_image)


if __name__ == "__main__":
    main()
