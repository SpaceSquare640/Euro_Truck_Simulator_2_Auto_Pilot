<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Game Frame IPC v1

Status: v1.0 prototype. The native plugin publishes a best-effort D3D11 back-buffer image to WPF. WPF reads this mapping and remains the sole writer of `perception.v1`; Python reads only `perception.v1`.

## Mapping and snapshot

The mapping is `Local\ETS2AP_GameFrame_v1`, with a single-writer semaphore named `Local\ETS2AP_GameFrame_v1.writer`. Its fixed size is 43,264 bytes: a 64-byte header and one 160 × 90 RGB24 frame (43,200 bytes). The format is little-endian and the image is row-major, top row first. A stable snapshot has a nonzero even `sequence`. The native writer changes it to odd before updating the packet and publishes the next even value after the image is complete. WPF reads the sequence before and after copying and rejects odd or inconsistent snapshots.

| Offset | Size | Field | Type | Unit | Range / meaning |
|---:|---:|---|---|---|---|
| 0 | 4 | magic | byte[4] | — | ASCII `E2GF` |
| 4 | 2 | version_major | uint16 | — | 1 |
| 6 | 2 | version_minor | uint16 | — | 0 |
| 8 | 4 | header_size | uint32 | bytes | 64 |
| 12 | 4 | flags | uint32 | bitmask | Bit 0 is `FRAME_VALID`; all other bits are zero |
| 16 | 8 | sequence | uint64 | count | Nonzero even for a stable frame; odd while writing |
| 24 | 8 | timestamp_ns | uint64 | ns | Monotonic QPC time when the source back buffer was copied; nonzero and not future |
| 32 | 2 | width | uint16 | pixels | 160 |
| 34 | 2 | height | uint16 | pixels | 90 |
| 36 | 4 | pixel_format | uint32 | enum | 1 = tightly packed RGB24 |
| 40 | 4 | payload_size | uint32 | bytes | 43,200 |
| 44 | 20 | reserved | byte[20] | — | All zero |
| 64 | 43,200 | pixels | byte[43,200] | RGB bytes | Each channel is 0–255 |

Consumers accept only v1.0 packets with `FRAME_VALID`, known dimensions and pixel format, zero reserved bytes, a stable sequence, and a timestamp no older than 150 ms. A consumer may reuse the same stable snapshot until it becomes stale, but must reject any sequence or timestamp decrease. A missing or invalid mapping, a stale frame, an unknown flag, a future timestamp, or malformed header means no frame is available. Consumers must not assume a fixed publication rate.

## Producer behavior and limits

The native plugin attempts to hook `IDXGISwapChain::Present` in the ETS2 process without a third-party hook library. It obtains the Present vtable entry from a hidden WARP probe swap chain, then captures a visible top-level window owned by the game process. Two D3D11 staging slots use event queries and non-blocking maps; the render path drops a frame when neither slot is ready. Captures are scheduled at no more than 20 Hz. Supported back-buffer formats are 8-bit RGBA/BGRA, with multisampled buffers resolved before readback. Resize or device changes recreate the staging resources.

The hook is best-effort and is not yet validated in the game. It selects the first visible, top-level, at-least-640 × 360 swap chain whose window is the foreground ETS2 window; it does not capture another swap chain until that window is destroyed. It may not see a game swap chain whose DXGI implementation uses a different vtable, and another in-process hook may conflict with the patched Present entry. To keep a retained hook callback from pointing into an unloaded module, the plugin pins its DLL in the game process until that process exits. The pin is taken before hook installation, so even a failed hook setup leaves the DLL loaded for the rest of that game process. The hidden 8 × 8 WARP probe window and swap chain also remain as process-lifetime hook anchors; this avoids destroying an HWND from a different shutdown thread. If its window has been destroyed by its owner thread, a later capture restart fails closed and uses GDI fallback. Stopping capture disables publication and releases the per-game readback resources. It restores the Present slot only while that slot still points directly to this hook. If another hook has wrapped it, this callback can remain in the Present chain as a pass-through until process exit. D3D12, non-RGBA/BGRA formats, hidden/minimized windows, and graphics-device failures produce no native frame. The UI then falls back to its existing GDI desktop capture. The mapping transports pixels only; it does not write game state or inject input.

## Ownership and bridge

The native plugin acquires the named semaphore as the single writer and holds that lease until capture stops; unlike a Windows mutex, the semaphore can be released by the shutdown thread even if initialization occurred on another thread. The mapping DACL grants access only to the current Windows user; if an existing mapping or semaphore has a different owner or DACL, the publisher refuses to use it. WPF opens the mapping read-only and never creates it. WPF validates the header, freshness, and seqlock snapshot, prefers a fresh native frame, and otherwise attempts the GDI fallback. It publishes the chosen image together with its own focus and input observations to `perception.v1`. Python does not read this internal mapping directly.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
