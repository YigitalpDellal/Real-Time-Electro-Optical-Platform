<p align="center">
  <img src="docs/media/hardware_overview.jpg" alt="Real-Time Electro-Optical Monitoring Platform" width="760">
</p>

<h1 align="center">Real-Time Electro-Optical Monitoring Platform</h1>

<p align="center">
  Raspberry Pi + TM4C123 embedded vision and pan-tilt control platform
</p>

<p align="center">
  <a href="https://github.com/YigitalpDellal/Real-Time-Electro-Optical-Platform/actions/workflows/ci.yml">
    <img src="https://github.com/YigitalpDellal/Real-Time-Electro-Optical-Platform/actions/workflows/ci.yml/badge.svg" alt="CI">
  </a>
  <img src="https://img.shields.io/badge/Raspberry%20Pi-3B%2B-C51A4A?logo=raspberrypi&logoColor=white" alt="Raspberry Pi 3B+">
  <img src="https://img.shields.io/badge/MCU-TM4C123-CC0000" alt="TM4C123">
  <img src="https://img.shields.io/badge/Status-Stable%20Prototype-2ea44f" alt="Stable Prototype">
</p>

<p align="center">
  <a href="docs/media/final_demo.mp4"><strong>Final Demo</strong></a> ·
  <a href="docs/architecture.md">Architecture</a> ·
  <a href="docs/wiring.md">Wiring</a> ·
  <a href="docs/troubleshooting.md">Troubleshooting</a> ·
  <a href="RELEASE_NOTES.md">Release Notes</a>
</p>

---

## Overview

This project is a network-enabled electro-optical monitoring platform built around a **Raspberry Pi 3 Model B+** and a **TM4C123GXL LaunchPad**.

The Raspberry Pi handles video streaming, browser control, target-event detection, system state, and telemetry. The TM4C123 handles deterministic low-level actuator control and generates the PWM signals for the two-axis pan-tilt mechanism.

The result is a complete embedded-systems integration project combining Linux, microcontroller firmware, UART communication, real hardware, computer vision, and a browser-based control layer.

### Technical snapshot

| Layer | Implementation |
|---|---|
| Linux host | Raspberry Pi 3 Model B+ |
| Real-time controller | TM4C123GXL / TM4C123GH6PM |
| Camera | Logitech C270 USB webcam |
| Actuation | 2 × MG90S servos |
| Host ↔ MCU link | UART, 115200 baud, 8-N-1 |
| Servo control | TM4C123 PWM, 50 Hz |
| Display | SSD1306-compatible 128×64 I2C OLED |
| User interface | Browser-based control panel |
| Video | MJPEG stream with uStreamer |
| Detection | Lightweight background-change detector |
| Alerts | Passive buzzer on Raspberry Pi GPIO18 |

## Demo

**[Watch the final end-to-end demonstration](docs/media/final_demo.mp4)**

Additional clips:

- [Hardware walkthrough](docs/media/hardware_walkthrough.mp4)
- [Pan/tilt motion test](docs/media/pan_tilt_motion.mp4)
- [Pan/tilt close-up](docs/media/pan_tilt_closeup.mp4)

<table>
<tr>
<td width="50%"><img src="docs/media/oled_hud.jpg" alt="OLED HUD"></td>
<td width="50%"><img src="docs/media/web_control.png" alt="Web control interface"></td>
</tr>
<tr>
<td align="center"><strong>OLED telemetry HUD</strong></td>
<td align="center"><strong>Browser control interface</strong></td>
</tr>
</table>

## System Architecture

~~~mermaid
flowchart LR
    CAM[Logitech C270] --> STREAM[uStreamer :8080]
    STREAM --> DET[target_detector.py]
    STREAM --> WEB[eo_web_control.py :8082]

    DET --> STATE[/tmp/eo_target_state]
    STATE --> WEB
    STATE --> CTRL[eo_hud_controller.c]

    WEB --> CTRL
    CTRL -->|UART 115200| MCU[TM4C123]

    MCU -->|PB6 / M0PWM0| PAN[PAN servo]
    MCU -->|PB7 / M0PWM1| TILT[TILT servo]

    CTRL --> OLED[SSD1306 OLED]
    DET --> BUZZER[GPIO18 buzzer]
~~~

The architecture intentionally keeps **servo PWM generation on the TM4C123** instead of Linux. High-level networking, video, telemetry, and target-state logic stay on the Raspberry Pi, while time-sensitive actuator control remains on the microcontroller.

For the full design breakdown, see [docs/architecture.md](docs/architecture.md).

## Core Capabilities

- Live MJPEG camera streaming
- Browser-based pan/tilt control
- Automatic platform centering
- Horizontal automatic scan with endpoint reversal
- Mechanical angle limiting
- Target-acquired and target-lost event detection
- Automatic scan interruption when a target is confirmed
- Distinct buzzer alerts for target acquisition and loss
- Live OLED telemetry for azimuth, elevation, camera, link, and target state
- Raspberry Pi ↔ TM4C123 UART command/acknowledgement protocol
- MCU-side PWM generation for both servos
- UART timeout handling and startup link verification

## Control Protocol

| Command | Response | Function |
|---|---|---|
| PING | ACK | Link verification |
| CENTER | CENTER_OK | Return both axes to 90° |
| PAN <angle> | PAN_OK | Set horizontal angle |
| TILT <angle> | TILT_OK | Set vertical angle |

### Mechanical limits

| Axis | Minimum | Center | Maximum |
|---|---:|---:|---:|
| PAN | 45° | 90° | 135° |
| TILT | 55° | 90° | 125° |

The restricted operating range protects the pan-tilt mechanism from mechanical binding.

## Target Detection

The stable detector is deliberately lightweight. It performs **background-change detection**, not object classification or identity recognition.

Current stable configuration:

| Parameter | Value |
|---|---:|
| Processing resolution | 320×240 |
| Minimum contour area | 4000 px |
| Acquisition confirmation | 4 frames |
| Loss confirmation | 15 frames |
| Initial background learning | 20 frames |
| Loop delay | 0.12 s |

The detector publishes system state through:

~~~text
/tmp/eo_target_state
~~~

Possible states:

~~~text
NO_TARGET
TARGET_ACQUIRED
TARGET_PRESENT
TARGET_LOST
~~~

This keeps target detection loosely coupled from the web-control and OLED processes.

## Repository Structure

~~~text
.
├── README.md
├── RELEASE_NOTES.md
├── Makefile
├── controller/
│   └── eo_hud_controller.c
├── raspberry_pi/
│   ├── eo_web_control.py
│   ├── target_detector.py
│   └── requirements.txt
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
~~~

## Build and Run

### 1. Validate the repository

~~~bash
make check
~~~

This checks both Raspberry Pi Python modules and builds the Linux-side C controller.

### 2. Build the Raspberry Pi controller

~~~bash
make build
~~~

### 3. Start the camera stream

~~~bash
v4l2-ctl -d /dev/video0 --set-ctrl=exposure_dynamic_framerate=0

ustreamer \
  --device=/dev/video0 \
  --resolution=640x480 \
  --desired-fps=30 \
  --format=MJPEG \
  --host=0.0.0.0 \
  --port=8080
~~~

### 4. Start target detection

~~~bash
python3 raspberry_pi/target_detector.py
~~~

Keep the scene stable during the initial background-learning period.

### 5. Start web control and telemetry

~~~bash
python3 raspberry_pi/eo_web_control.py
~~~

Then open:

~~~text
http://<RASPBERRY_PI_IP>:8082/
~~~

### Raspberry Pi Python dependencies

~~~bash
pip install -r raspberry_pi/requirements.txt
~~~

On Raspberry Pi OS, system packages for OpenCV and NumPy may be preferable to compiling/installing large Python wheels locally.

## TM4C123 Firmware

The TM4C firmware is built and flashed using **Code Composer Studio** with TivaWare/DriverLib available to the project.

Key configuration:

~~~text
System clock : 80 MHz
UART1        : PB0 / U1RX, PB1 / U1TX
UART         : 115200 baud, 8-N-1
PAN PWM      : PB6 / M0PWM0
TILT PWM     : PB7 / M0PWM1
Servo PWM    : 50 Hz
~~~

Source: [tm4c/tm4c_pan_tilt_firmware.c](tm4c/tm4c_pan_tilt_firmware.c)

## Verification

The repository includes real test evidence from the working system.

<table>
<tr>
<td width="50%"><img src="docs/media/target_detection_log.png" alt="Target detection log"></td>
<td width="50%"><img src="docs/media/scan_target_stop_log.png" alt="Scan stop log"></td>
</tr>
<tr>
<td align="center"><strong>Target acquisition / loss</strong></td>
<td align="center"><strong>Scan interruption / UART traffic</strong></td>
</tr>
</table>

GitHub Actions also performs:

- Python syntax validation
- Linux-side C controller build with -Wall -Wextra

## Engineering Scope

This repository represents the **stable prototype**. Classical OpenCV trackers such as MOSSE, KCF, and CSRT were evaluated during development, but they are not presented as stable features because of drift, reacquisition latency, and Raspberry Pi 3 performance constraints.

The stable design therefore prioritizes deterministic actuator control, operator control, automatic scanning, and reliable target events.

Development notes and adjusted approaches are documented in [docs/troubleshooting.md](docs/troubleshooting.md) and [experiments/README.md](experiments/README.md).

## Known Limitation

The target detector is based on scene change. Large moving shadows or abrupt lighting changes can therefore cause false target events. The final demo uses a stable background-learning period before target entry.

## Documentation

- [Architecture](docs/architecture.md) — software and hardware architecture
- [Wiring](docs/wiring.md) — wiring and signal connections
- [Troubleshooting](docs/troubleshooting.md) — debugging history and fixes
- [Release Notes](RELEASE_NOTES.md) — stable feature boundary and release notes

---

<p align="center">
  <strong>Embedded Linux · TM4C123 · UART · PWM · Computer Vision · Hardware Integration</strong>
</p>
