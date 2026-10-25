"""Cross-process Control.v1 client/server interoperability check.

Edited by: OpenAI-Codex_Responsible: Source_Code
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ets2ap.control_client import ControlClient
from ets2ap.control_protocol import ControlState


def main() -> None:
    if sys.platform != "win32" or len(sys.argv) != 3:
        raise SystemExit(2)
    client = ControlClient(
        sys.argv[2],
        authorize_engage=lambda: True,
        pipe_name=sys.argv[1],
    )
    try:
        client.connect()
        deadline = time.monotonic() + 0.5
        status = client.read_status()
        while status.last_accepted_sequence < 1 and time.monotonic() < deadline:
            status = client.read_status()
        if status.last_accepted_sequence != 1 or status.state is not ControlState.DISENGAGED:
            raise AssertionError("native server did not accept the neutral handshake")

        client.send_command(0.5, 0.0, 0.0, True)
        deadline = time.monotonic() + 0.5
        status = client.read_status()
        while status.last_accepted_sequence < 2 and time.monotonic() < deadline:
            status = client.read_status()
        if (status.last_accepted_sequence != 2 or status.state is ControlState.ENGAGED or
                status.steering != 0.0 or status.throttle != 0.0 or status.brake != 0.0):
            raise AssertionError("native server applied output without safety gates")
    finally:
        client.close()


if __name__ == "__main__":
    main()
