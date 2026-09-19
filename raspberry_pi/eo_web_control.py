#!/usr/bin/env python3

"""
Web-based control layer for the Real-Time Electro-Optical Platform.

Responsibilities:
- Starts and supervises the C-based Raspberry Pi/TM4C controller.
- Sends PAN, TILT and CENTER commands to the TM4C123 through the controller.
- Provides a browser interface for manual pan/tilt operation.
- Implements automatic horizontal scan mode.
- Reads the shared target-state file produced by the detector.
- Stops automatic scanning when a target is confirmed.
- Reports platform telemetry to the browser.

The low-level servo PWM generation remains on the TM4C123.
"""

import json
import os
import subprocess
import threading
import time

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


WEB_PORT = 8082

PAN_MIN = 45
PAN_MAX = 135

TILT_MIN = 55
TILT_MAX = 125

CENTER = 90

MANUAL_STEP = 5

SCAN_STEP = 10
SCAN_DELAY = 1.20

TARGET_STATE_FILE = "/tmp/eo_target_state"


current_pan = CENTER
current_tilt = CENTER

scan_enabled = False
controller_online = False

lock = threading.Lock()

controller = None


# Read the detector state shared through /tmp/eo_target_state.
# This provides loose coupling between target detection and platform control.
def read_target_state():

    try:
        with open(
            TARGET_STATE_FILE,
            "r"
        ) as f:

            return f.read().strip()

    except OSError:

        return "OFFLINE"


# Continuously read the C controller output in a background thread.
# UART status and acknowledgement messages remain visible in the terminal.
def controller_reader():

    global controller_online

    while True:

        if controller is None:
            return

        line = controller.stdout.readline()

        if line == "":
            break

        print(
            "[CONTROLLER]",
            line.rstrip()
        )

        if "UART link established" in line:
            controller_online = True

    controller_online = False


# Launch the compiled C controller as a subprocess.
# The C process owns UART communication and OLED updates.
def start_controller():

    global controller
    global controller_online

    controller_online = False

    controller = subprocess.Popen(
        [
            "stdbuf",
            "-oL",
            "-eL",
            "./eo_hud_controller_target"
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1
    )

    thread = threading.Thread(
        target=controller_reader,
        daemon=True
    )

    thread.start()


# Forward one high-level command to the C controller through stdin.
# Examples: "pan 90", "tilt 100", "center".
def send_controller_command(command):

    if (
        controller is None
        or
        controller.poll() is not None
    ):
        return False

    try:

        controller.stdin.write(
            command + "\n"
        )

        controller.stdin.flush()

        print(
            "[WEB -> CONTROLLER]",
            command
        )

        return True

    except Exception as error:

        print(
            "[COMMAND ERROR]",
            error
        )

        return False


# Apply software safety limits before requesting a PAN movement.
def set_pan(value):

    global current_pan

    value = max(
        PAN_MIN,
        min(PAN_MAX, int(value))
    )

    if send_controller_command(
        f"pan {value}"
    ):

        with lock:
            current_pan = value

        return True

    return False


# Apply software safety limits before requesting a TILT movement.
def set_tilt(value):

    global current_tilt

    value = max(
        TILT_MIN,
        min(TILT_MAX, int(value))
    )

    if send_controller_command(
        f"tilt {value}"
    ):

        with lock:
            current_tilt = value

        return True

    return False


# Return both platform axes to the defined 90-degree center position.
def center_platform():

    global current_pan
    global current_tilt

    if send_controller_command(
        "center"
    ):

        with lock:
            current_pan = CENTER
            current_tilt = CENTER

        return True

    return False


# Automatic horizontal scan state machine.
#
# The platform moves between PAN_MIN and PAN_MAX. The direction reverses
# at each endpoint. Scanning stops when the detector reports a confirmed
# target, leaving the platform stationary for operator control.
def scan_worker():

    global scan_enabled

    direction = 1

    while True:

        time.sleep(
            SCAN_DELAY
        )

        with lock:
            enabled = scan_enabled
            pan = current_pan

        if not enabled:
            continue

        target_state = read_target_state()

        if target_state in (
            "TARGET_ACQUIRED",
            "TARGET_PRESENT"
        ):

            with lock:
                scan_enabled = False

            print(
                "[SCAN] Target detected. Scan stopped."
            )

            continue

        next_pan = (
            pan
            + direction * SCAN_STEP
        )

        if next_pan >= PAN_MAX:

            next_pan = PAN_MAX
            direction = -1

        elif next_pan <= PAN_MIN:

            next_pan = PAN_MIN
            direction = 1

        set_pan(
            next_pan
        )


# HTTP request handler for the browser control panel and telemetry API.
class WebHandler(
    BaseHTTPRequestHandler
):

    def log_message(
        self,
        format,
        *args
    ):
        return


    def send_json(
        self,
        data,
        code=200
    ):

        body = json.dumps(
            data
        ).encode()

        self.send_response(
            code
        )

        self.send_header(
            "Content-Type",
            "application/json"
        )

        self.send_header(
            "Cache-Control",
            "no-store"
        )

        self.send_header(
            "Content-Length",
            str(len(body))
        )

        self.end_headers()

        self.wfile.write(
            body
        )


    def do_GET(self):

        if self.path.startswith(
            "/status"
        ):

            with lock:

                data = {
                    "pan": current_pan,
                    "tilt": current_tilt,
                    "scan": scan_enabled,
                    "uart": controller_online,
                    "target": read_target_state()
                }

            self.send_json(
                data
            )

            return


        if self.path == "/":

            page = r"""
<!DOCTYPE html>

<html>

<head>

<meta charset="utf-8">

<title>EO Platform Control</title>

<style>

body {
    margin: 0;
    background: #0b0e12;
    color: #e7edf5;
    font-family: Arial, sans-serif;
    text-align: center;
}

h2 {
    margin-bottom: 4px;
}

#subtitle {
    color: #98a4b3;
    margin-bottom: 14px;
}

#video {
    width: 640px;
    max-width: 94vw;
    border: 2px solid #394452;
}

.panel {
    margin: 18px auto;
    width: 420px;
    max-width: 92vw;
    padding: 16px;
    border: 1px solid #394452;
    border-radius: 8px;
    background: #151a21;
}

.grid {
    display: grid;
    grid-template-columns:
        1fr 1fr 1fr;
    gap: 8px;
}

button {
    padding: 13px 8px;
    font-size: 16px;
    cursor: pointer;
    background: #242c36;
    color: white;
    border: 1px solid #536172;
    border-radius: 6px;
}

button:hover {
    background: #313c49;
}

.center {
    font-weight: bold;
}

.scan {
    width: 48%;
    margin-top: 12px;
}

#telemetry {
    margin-top: 14px;
    font-family: monospace;
    font-size: 16px;
    line-height: 1.7;
}

.good {
    color: #43e06f;
}

.bad {
    color: #ff5555;
}

.warn {
    color: #ffd34e;
}

</style>

</head>

<body>

<h2>
REAL-TIME ELECTRO-OPTICAL PLATFORM
</h2>

<div id="subtitle">
Raspberry Pi 3 B+ + TM4C123 Pan-Tilt Controller
</div>

<img
    id="video"
    alt="Live EO Camera"
/>

<div class="panel">

<div class="grid">

<div></div>

<button onclick="tiltPlus()">
▲ TILT +
</button>

<div></div>


<button onclick="panMinus()">
◀ PAN -
</button>

<button
    class="center"
    onclick="sendCommand('center')"
>
CENTER
</button>

<button onclick="panPlus()">
PAN + ▶
</button>


<div></div>

<button onclick="tiltMinus()">
▼ TILT -
</button>

<div></div>

</div>


<button
    class="scan"
    onclick="sendCommand('scan_start')"
>
START SCAN
</button>

<button
    class="scan"
    onclick="sendCommand('scan_stop')"
>
STOP SCAN
</button>


<div id="telemetry">

PAN:
<span id="pan">
--
</span>°

&nbsp;&nbsp;

TILT:
<span id="tilt">
--
</span>°

<br>

UART:
<span id="uart">
--
</span>

&nbsp;&nbsp;

TARGET:
<span id="target">
--
</span>

<br>

MODE:
<span id="mode">
--
</span>

</div>

</div>


<script>

const host =
    window.location.hostname;

document.getElementById(
    "video"
).src =
    "http://" +
    host +
    ":8080/stream";


async function sendCommand(
    command
) {

    await fetch(
        "/command",
        {
            method:
                "POST",

            headers: {
                "Content-Type":
                    "application/json"
            },

            body:
                JSON.stringify(
                    {
                        command:
                            command
                    }
                )
        }
    );

    updateStatus();
}


async function updateStatus() {

    try {

        const response =
            await fetch(
                "/status?ts="
                + Date.now(),
                {
                    cache:
                        "no-store"
                }
            );

        const data =
            await response.json();


        document.getElementById(
            "pan"
        ).textContent =
            data.pan;


        document.getElementById(
            "tilt"
        ).textContent =
            data.tilt;


        const uart =
            document.getElementById(
                "uart"
            );

        uart.textContent =
            data.uart
            ? "ONLINE"
            : "OFFLINE";

        uart.className =
            data.uart
            ? "good"
            : "bad";


        const target =
            document.getElementById(
                "target"
            );

        target.textContent =
            data.target;

        target.className =
            (
                data.target ===
                "TARGET_PRESENT"
                ||
                data.target ===
                "TARGET_ACQUIRED"
            )
            ? "good"
            : "warn";


        document.getElementById(
            "mode"
        ).textContent =
            data.scan
            ? "SCAN"
            : "MANUAL";
    }

    catch (error) {

        document.getElementById(
            "uart"
        ).textContent =
            "OFFLINE";
    }
}


function panMinus() {
    sendCommand(
        "pan_minus"
    );
}


function panPlus() {
    sendCommand(
        "pan_plus"
    );
}


function tiltMinus() {
    sendCommand(
        "tilt_minus"
    );
}


function tiltPlus() {
    sendCommand(
        "tilt_plus"
    );
}


setInterval(
    updateStatus,
    500
);


updateStatus();

</script>

</body>

</html>
"""

            body = page.encode()

            self.send_response(
                200
            )

            self.send_header(
                "Content-Type",
                "text/html; charset=utf-8"
            )

            self.send_header(
                "Content-Length",
                str(len(body))
            )

            self.end_headers()

            self.wfile.write(
                body
            )

            return


        self.send_error(
            404
        )


    def do_POST(self):

        global scan_enabled

        if self.path != "/command":

            self.send_error(
                404
            )

            return


        length = int(
            self.headers.get(
                "Content-Length",
                "0"
            )
        )

        raw = self.rfile.read(
            length
        )

        try:

            data = json.loads(
                raw.decode()
            )

            command = data[
                "command"
            ]

        except Exception:

            self.send_json(
                {
                    "ok": False
                },
                400
            )

            return


        with lock:

            pan = current_pan
            tilt = current_tilt


        if command == "pan_minus":

            with lock:
                scan_enabled = False

            ok = set_pan(
                pan - MANUAL_STEP
            )


        elif command == "pan_plus":

            with lock:
                scan_enabled = False

            ok = set_pan(
                pan + MANUAL_STEP
            )


        elif command == "tilt_minus":

            with lock:
                scan_enabled = False

            ok = set_tilt(
                tilt - MANUAL_STEP
            )


        elif command == "tilt_plus":

            with lock:
                scan_enabled = False

            ok = set_tilt(
                tilt + MANUAL_STEP
            )


        elif command == "center":

            with lock:
                scan_enabled = False

            ok = center_platform()


        elif command == "scan_start":

            with lock:
                scan_enabled = True

            ok = True


        elif command == "scan_stop":

            with lock:
                scan_enabled = False

            ok = True


        else:

            ok = False


        self.send_json(
            {
                "ok": ok
            },
            200 if ok else 400
        )


def main():

    print()
    print(
        "=========================================="
    )

    print(
        " EO PLATFORM WEB CONTROL"
    )

    print(
        "=========================================="
    )

    print()

    print(
        "[INFO] Starting TM4C controller..."
    )


    start_controller()


    scan_thread = threading.Thread(
        target=scan_worker,
        daemon=True
    )

    scan_thread.start()


    server = ThreadingHTTPServer(
        (
            "0.0.0.0",
            WEB_PORT
        ),
        WebHandler
    )


    print(
        f"[INFO] Web control: http://PI_IP:{WEB_PORT}/"
    )

    print(
        "[INFO] Ctrl+C to stop."
    )

    print()


    try:

        server.serve_forever()

    except KeyboardInterrupt:

        print()
        print(
            "[INFO] Stopping web control..."
        )

    finally:

        server.shutdown()

        with lock:
            global scan_enabled
            scan_enabled = False

        if (
            controller is not None
            and
            controller.poll() is None
        ):

            try:

                send_controller_command(
                    "quit"
                )

                controller.wait(
                    timeout=3
                )

            except Exception:

                controller.terminate()


if __name__ == "__main__":
    main()