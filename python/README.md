<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->
# Python UI status service

This development service reads the existing `Local\ETS2AP_Telemetry_v1` mapping and publishes informational `ui.v1` status snapshots on `\\.\pipe\ETS2AP_UI_v1`. It uses only the Python standard library. There is no native control client yet, so it always reports `fault` and rejects `engage` with `CONTROL_UNAVAILABLE`. The UI Engage button remains disabled.

From the Source Code directory, set `PYTHONPATH=python` and run `python -m ets2ap.ui_server --trusted-ui-image <absolute path to ETS2AP.Ui.exe>`. Launch the WPF app with `--trusted-python-image <absolute path to the running python.exe>`. Both paths must point to the actual process images; the peers compare full canonical image paths and current-user SIDs before exchanging messages. The final packaged Python process model and trusted path provisioning remain design decisions. Without an explicit trusted image path, the UI stays offline.

The server creates a single-client, byte-stream Windows named pipe with a current-user DACL, remote-client rejection, first-instance ownership, and overlapped I/O. It accepts `hello`, `get_status`, and `disengage` for status-only use. `set_config` and actual engagement are not implemented. Missing, invalid, or stale telemetry never makes the service ready.

Offline checks: set `PYTHONPATH=python` and run `python -m unittest discover -s python/tests -v`. These checks do not prove game telemetry, native control, installer behavior, or cross-process security outside the current Windows execution environment.
