// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

namespace ets2ap::plugin {

// Starts a best-effort D3D11 Present capture hook in the ETS2 process.
// Failure leaves the existing WPF desktop-capture path available.
bool StartGameFrameCapture() noexcept;
void StopGameFrameCapture() noexcept;

} // namespace ets2ap::plugin
