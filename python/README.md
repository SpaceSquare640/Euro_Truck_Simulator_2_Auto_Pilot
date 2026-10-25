<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Python UI status and control services

This development service reads the existing `Local\ETS2AP_Telemetry_v1` mapping and publishes informational `ui.v1` status snapshots on `\\.\pipe\ETS2AP_UI_v1`. It uses only the Python standard library. The UI status service has no control loop connected to the native Control.v1 client, so it continues to report `fault` and rejects `engage` with `CONTROL_UNAVAILABLE`. The UI Engage button remains disabled.

For a packaged install, the WPF app starts `runtime\python.exe -I -S -m ets2ap.ui_server` with its own executable path as the trusted UI image. The MSI places the Python modules under `python\ets2ap` and adds `..\python` to the embedded runtime's isolated module search path. The peers compare canonical process image paths and current-user SIDs before exchanging messages. The app allows one automatic restart after an unexpected service exit and terminates the child process when the app closes. If the runtime or service module is missing, the UI remains offline.

For a development pipe smoke check, run the service with `--trusted-ui-image <absolute path to ETS2AP.Ui.Checks.exe>`, then run `ETS2AP.Ui.Checks.exe --ipc-smoke <absolute path to the same python.exe>`. The application does not accept an external Python path for its packaged service.

The server creates a single-client, byte-stream Windows named pipe with a current-user DACL, remote-client rejection, first-instance ownership, and overlapped I/O. It accepts `hello`, `get_status`, and `disengage` for status-only use. `set_config` and actual engagement are not implemented. Missing, invalid, or stale telemetry never makes the service ready.

The `ets2ap.control_client` module implements the Control.v1 binary codec and a Windows named-pipe client. Its overlapped handle uses security identification QoS; before sending, it checks the server's canonical executable path and current-user SID, then sends a neutral sequence-1 handshake. Status packets are validated and must complete within 100 ms. The client denies engagement by default unless a caller supplies an explicit authorizer. The product UI does not supply one, and native engagement safety gates are not connected to telemetry freshness, a verified speed limit, UI authorization, or a physical manual-input detector; automatic driving remains unavailable.

The native Control.v1 server trusts the bundled Python runtime path registered under `HKLM\SOFTWARE\Kingsley\ETS2 Auto Pilot\InstallLocation\runtime\python.exe`. The WPF app launches the status-only Python service, but it does not start the Python Control.v1 client; automatic driving remains unavailable.

Offline checks: set `PYTHONPATH=python` and run `python -m unittest discover -s python/tests -v`. Native cross-process Control.v1 interoperability is exercised by `native/plugin/tests/control_host_tests.cpp` with `ETS2AP_TEST_PYTHON` set to the bundled runtime path. These checks do not prove game telemetry, installer behavior, or in-game control.

GitHub Actions detects this module by its `ets2ap/` and `tests/` directories, compiles the package, and runs the standard-library `unittest` suite. The current Python module has no third-party package dependencies.
