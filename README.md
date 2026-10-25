# Euro Truck Simulator 2 Auto Pilot

Autopilot and driver-assistance app for Euro Truck Simulator 2 (Windows 10/11).

**Documentation, Change Log, Wiki and the interactive simulator are on the project website:**

👉 https://spacesquare640.github.io/Euro_Truck_Simulator_2_Auto_Pilot/

## Windows release packages

Each GitHub Release provides two assets:

- `ETS2AP_DLL_V<version>.zip` — the game connector downloaded by the app from GitHub Releases.
- `ETS2AP_V<version>_x64_en-US.msi` — the Windows Installer package.

The MSI includes the CPython 3.13 x64 embeddable runtime and the Python service modules; users do not need to install Python separately. The runtime's license is installed alongside it. The WPF app starts the status service from its bundled runtime, while automatic ETS2 control remains unavailable.

Install the app by opening the MSI with Windows Installer. After installing and launching the app, choose **Install connector** to download and verify the matching connector package before installing its DLL into the selected ETS2 plugins folder.

The connector ZIP includes the SCS Telemetry and Input SDK MIT license notice beside the DLL payload.

<!-- Edited by: OpenAI-Codex_Responsible: Source_Code -->

---

Application source code and software are licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE). The project's website content (text and images) is licensed under CC BY-NC-SA 4.0; the website source code is licensed under the PolyForm Noncommercial License 1.0.0. Copyright (c) 2026 Kingsley. See [NOTICE](NOTICE) for third-party notices.

<!-- Edited by: Anthropic-Claude_Code -->
