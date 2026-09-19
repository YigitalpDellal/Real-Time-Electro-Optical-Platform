# Real-Time Electro-Optical Monitoring Platform

![Project banner](docs/media/project_banner.jpg)

[![CI](https://github.com/YigitalpDellal/Real-Time-Electro-Optical-Platform/actions/workflows/ci.yml/badge.svg)](https://github.com/YigitalpDellal/Real-Time-Electro-Optical-Platform/actions/workflows/ci.yml)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-3B%2B-C51A4A?logo=raspberrypi&logoColor=white)
![TM4C123](https://img.shields.io/badge/MCU-TM4C123-CC0000)
![Python](https://img.shields.io/badge/Python-3.x-3776AB?logo=python&logoColor=white)
![C](https://img.shields.io/badge/C-Embedded-00599C?logo=c&logoColor=white)

A network-enabled pan-tilt electro-optical platform built around a **Raspberry Pi 3 Model B+** and a **TM4C123GXL LaunchPad**. The system combines live video streaming, browser-based control, automatic scanning, motion-based target events, OLED telemetry, acoustic alerts, UART communication, and MCU-side PWM servo control.

The project was developed as a practical embedded-systems integration exercise: Linux handles networking, video, user interaction, and high-level state; the TM4C123 handles low-level actuator timing and validates motion commands.

## Demo

**[▶ Watch the final system demo](docs/media/final_demo.mp4)**

Additional short clips:

- [Hardware walkthrough](docs/media/hardware_walkthrough.mp4)
- [Pan/tilt motion demo](docs/media/pan_tilt_motion.mp4)
- [Pan/tilt close-up](docs/media/pan_tilt_closeup.mp4)

| Hardware overview | OLED telemetry HUD |
|---|---|
| ![Hardware overview](docs/media/hardware_overview.jpg) | ![OLED HUD](docs/media/oled_hud.jpg) |

## What the Project Demonstrates

- Hardware/software partitioning between Linux and an MCU
- UART command/acknowledgement protocol design
- 50 Hz hobby-servo PWM generation on the TM4C123
- Mechanical safety limits and recentering logic
- Browser-based remote control and telemetry
- Automatic horizontal scan state management
- Motion/background-based target acquisition and loss events
- Event-driven scan interruption
- I2C SSD1306 OLED framebuffer rendering
- GPIO buzzer signaling
- Debugging of power, timing, UART, camera, and motion-detection issues

## Features

| Area | Stable implementation |
|---|---|
| Video | Logitech C270 MJPEG stream through uStreamer |
| Manual control | Browser PAN/TILT controls with 5° steps |
| Centering | `CENTER` returns both axes to 90° |
| Automatic scan | PAN sweeps between 45° and 135° and reverses at endpoints |
| Target events | Background-change detector with acquisition/loss debounce |
| Scan integration | Scan stops when the detector reports a target |
| Alerts | Two short beeps on acquire, one longer low tone on loss |
| Telemetry | OLED shows AZ, EL, CAM, LINK, TGT and an EO reticle |
| Pi ↔ MCU link | UART at 115200 baud, 8-N-1 |
| Servo control | TM4C123 PWM on PB6/PB7 |

## System Architecture

```mermaid
flowchart TD
    CAM[Logitech C270] --> STREAM[uStreamer :8080]
    STREAM --> DET[target_detector.py]
    STREAM --> WEB[eo_web_control.py :8082]

    DET --> STATE[/tmp/eo_target_state]
    STATE --> WEB
    STATE --> HUD[eo_hud_controller.c]

    WEB --> HUD
    HUD -->|UART 115200 8-N-1| MCU[TM4C123 firmware]

    MCU -->|PB6 / M0PWM0| PAN[PAN servo]
    MCU -->|PB7 / M0PWM1| TILT[TILT servo]

    HUD --> OLED[SSD1306 OLED]
    DET --> BUZZER[Buzzer / GPIO18]
```

The design deliberately keeps **PWM generation on the TM4C123** rather than Linux. This isolates time-critical actuator control from Linux scheduling while leaving video, networking, and high-level behavior on the Raspberry Pi.

More detail: [`docs/architecture.md`](docs/architecture.md)

## Hardware

- Raspberry Pi 3 Model B+
- EK-TM4C123GXL / TM4C123GH6PM LaunchPad
- Logitech C270 USB webcam
- 2 × MG90S micro servos
- Two-axis pan-tilt bracket
- SSD1306-compatible 128×64 I2C OLED
- Passive buzzer with transistor/resistor driver stage
- External regulated 5 V servo supply
- Breadboards, jumper wires, bulk electrolytic capacitor

Full connections: [`docs/wiring.md`](docs/wiring.md)

## Operating Limits

The commanded range is intentionally narrower than the nominal servo range to prevent mechanical binding.

| Axis | Minimum | Center | Maximum |
|---|---:|---:|---:|
| PAN | 45° | 90° | 135° |
| TILT | 55° | 90° | 125° |

## UART Protocol

The Raspberry Pi sends text commands and waits for a defined acknowledgement from the TM4C123.

| Raspberry Pi command | TM4C123 response |
|---|---|
| `PING` | `ACK` |
| `CENTER` | `CENTER_OK` |
| `PAN <angle>` | `PAN_OK` |
| `TILT <angle>` | `TILT_OK` |

The firmware validates requested angles before changing PWM outputs. Invalid input is rejected instead of being forwarded directly to the servos.

## Target Detection

The stable detector is intentionally lightweight and does **not** claim object recognition. It learns an empty scene and identifies sufficiently large changes relative to that background.

Stable settings:

| Parameter | Value |
|---|---:|
| Processing resolution | 320×240 |
| Difference threshold | 25 |
| Minimum contour area | 4000 px |
| Acquisition confirmation | 4 frames |
| Loss confirmation | 15 frames |
| Initial background warm-up | 20 frames |
| Loop delay | 0.12 s |

The detector publishes one of the following states to `/tmp/eo_target_state`:

```text
NO_TARGET
TARGET_ACQUIRED
TARGET_PRESENT
TARGET_LOST
```

That file acts as a small inter-process interface between target detection, scan control, and the OLED HUD.

> **Known limitation:** because the stable detector is based on background change, large moving shadows or abrupt lighting changes can look like motion. The demo uses a stable background-learning period before target entry. See [`docs/troubleshooting.md`](docs/troubleshooting.md).

## Automatic Scan

The platform scans horizontally and reverses at each PAN endpoint.

```text
45° → 55° → ... → 135°
                  ↓
45° ← 55° ← ... ← 125°
```

Stable scan settings:

```text
SCAN_STEP  = 10 degrees
SCAN_DELAY = 1.20 seconds
```

The slower command rate was selected after testing showed that faster command generation could queue motion requests and cause UART response timeouts. When a target is reported, scanning stops and the platform remains stationary until the operator starts it again.

## OLED HUD

The 128×64 display shows live platform state:

- `AZ` — current PAN / azimuth angle
- `EL` — current TILT / elevation angle
- `CAM` — camera presence
- `LINK` — UART link state
- `TGT` — target state
- PAN scale and live pointer
- electro-optical aiming reticle

![OLED telemetry](docs/media/oled_hud.jpg)

## Browser Control

The browser interface runs on port `8082` and provides manual movement, centering, scan start/stop, live camera video, and system telemetry.

![Web control panel](docs/media/web_control.png)

## Build and Run

### Raspberry Pi prerequisites

The tested system expects:

- Raspberry Pi OS
- UART enabled as `/dev/serial0`
- I2C enabled as `/dev/i2c-1`
- Logitech C270 exposed as `/dev/video0`
- Python 3
- OpenCV, NumPy, gpiozero
- uStreamer
- GCC
- `v4l2-ctl` and `i2cdetect` for setup/debugging

On Raspberry Pi OS, distribution packages for OpenCV/NumPy/gpiozero are preferable to compiling OpenCV on the Pi.

### 1. Build the Raspberry Pi C controller

From the repository root:

```bash
gcc -std=c11 -O2 -Wall -Wextra \
  -o eo_hud_controller_target \
  controller/eo_hud_controller.c
```

`eo_web_control.py` launches `./eo_hud_controller_target`, so run the web layer from the repository root.

### 2. Start the camera stream

```bash
v4l2-ctl -d /dev/video0 --set-ctrl=exposure_dynamic_framerate=0

ustreamer \
  --device=/dev/video0 \
  --resolution=640x480 \
  --desired-fps=30 \
  --format=MJPEG \
  --host=0.0.0.0 \
  --port=8080
```

### 3. Start target detection

In a second terminal:

```bash
python3 raspberry_pi/target_detector.py
```

Keep the scene still during the initial background-learning period.

### 4. Start web control + HUD integration

In a third terminal, from the repository root:

```bash
python3 raspberry_pi/eo_web_control.py
```

Open:

```text
http://<RASPBERRY_PI_IP>:8082/
```

## TM4C123 Firmware

The TM4C firmware is intended to be built and flashed with Code Composer Studio and TivaWare/DriverLib available to the project.

Key configuration:

```text
System clock : 80 MHz
UART1        : PB0 / U1RX, PB1 / U1TX
UART format  : 115200 baud, 8-N-1
PAN PWM      : PB6 / M0PWM0
TILT PWM     : PB7 / M0PWM1
Servo PWM    : 50 Hz
```

Source: [`tm4c/tm4c_pan_tilt_firmware.c`](tm4c/tm4c_pan_tilt_firmware.c)

## Verification Evidence

Target acquisition and loss:

![Target detection log](docs/media/target_detection_log.png)

Automatic scan stopping after target detection:

![Scan stop log](docs/media/scan_target_stop_log.png)

## Repository Layout

```text
.
├── README.md
├── RELEASE_NOTES.md
├── controller/
│   └── eo_hud_controller.c
├── raspberry_pi/
│   ├── eo_web_control.py
│   └── target_detector.py
├── tm4c/
│   └── tm4c_pan_tilt_firmware.c
├── docs/
│   ├── architecture.md
│   ├── wiring.md
│   ├── troubleshooting.md
│   └── media/
├── experiments/
│   └── README.md
└── .github/
    └── workflows/
        └── ci.yml
```

## Experimental Tracking Work

Classical OpenCV trackers including MOSSE, KCF, and CSRT were evaluated during development. They were useful for exploring visual-lock behavior, but the stable release does not claim persistent object tracking because drift, reacquisition latency, changing appearance, and Raspberry Pi 3 compute limits made those versions less predictable.

The final architecture therefore uses deterministic manual/scan control with target events, while tracking remains documented as engineering exploration in [`experiments/README.md`](experiments/README.md).

## Engineering Notes

The project went through real hardware/debugging iterations including:

- servo power instability and jitter,
- common-ground issues,
- mechanical endpoint strain,
- UART startup retries and stale receive data,
- queued scan commands and UART timeouts,
- camera stream/device issues,
- background-learning false positives,
- target-loss debounce trade-offs,
- OLED initialization and visibility,
- experimental tracker drift.

The fixes and reasoning are documented in [`docs/troubleshooting.md`](docs/troubleshooting.md).

## Stable Release

The stable release includes manual pan/tilt control, recentering, automatic horizontal scanning, target acquisition/loss events, scan interruption, buzzer alerts, OLED telemetry, live video, UART command handling, and TM4C123 PWM servo control.

See [`RELEASE_NOTES.md`](RELEASE_NOTES.md) for the stable feature boundary and known limitation.
