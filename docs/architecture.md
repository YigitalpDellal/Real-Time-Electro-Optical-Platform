# System Architecture

## Overview

The platform is split into a high-level Linux side and a low-level real-time actuator side.

- **Raspberry Pi 3 B+**: video, motion detection, web UI, scan state machine, OLED rendering, and system integration
- **TM4C123**: UART command parser, angle validation, and 50 Hz servo PWM generation

This separation keeps networking and image processing away from the time-sensitive PWM generation.

## Runtime Processes

### uStreamer

The Logitech C270 is exposed as `/dev/video0`. uStreamer publishes:

- `http://127.0.0.1:8080/snapshot` for frame-by-frame detector input
- `http://<PI_IP>:8080/stream` for the browser live view

### `target_detector.py`

The detector:

1. requests camera snapshots,
2. resizes them to 320x240,
3. converts them to blurred grayscale,
4. learns an initial background,
5. compares subsequent frames against the learned background,
6. confirms acquisition/loss over multiple frames,
7. writes `/tmp/eo_target_state`,
8. drives the buzzer on BCM GPIO18.

Published states are:

```text
NO_TARGET
TARGET_ACQUIRED
TARGET_PRESENT
TARGET_LOST
OFFLINE
```

After `TARGET_LOST`, the detector relearns the now-empty scene for a short period before arming again.

### `eo_web_control.py`

The web layer serves port `8082` and provides:

- PAN - / PAN +
- TILT - / TILT +
- CENTER
- START SCAN
- STOP SCAN
- current PAN/TILT telemetry
- UART status
- target status
- MANUAL / SCAN mode status

It launches the C controller as a subprocess and sends textual commands through the subprocess standard input.

The scan thread advances PAN in 10 degree steps with a 1.20 second delay. It reverses at 45° and 135°. If the shared target state becomes `TARGET_ACQUIRED` or `TARGET_PRESENT`, the scan flag is cleared and motion stops.

### `eo_hud_controller.c`

The C integration process owns two hardware interfaces:

- `/dev/serial0` for TM4C123 UART
- `/dev/i2c-1` for the SSD1306 OLED

It also checks `/dev/video0` to report camera presence on the HUD and reads `/tmp/eo_target_state` for the target indicator.

UART startup is verified with `PING`/`ACK`. The implementation permits up to three startup attempts because the serial interface can occasionally need a short settling period after process start.

### TM4C123 firmware

The microcontroller receives commands on UART1:

```text
PING
CENTER
PAN <angle>
TILT <angle>
TEST
```

It validates angles and updates PWM Module 0 Generator 0:

- PWM output 0 -> PB6 -> PAN servo
- PWM output 1 -> PB7 -> TILT servo

Both outputs share the 50 Hz servo period but use independent pulse widths.

## Control Paths

### Manual movement

```text
Browser
  -> eo_web_control.py
  -> eo_hud_controller.c
  -> UART
  -> TM4C123
  -> PWM
  -> servo
```

### Target event

```text
Camera
  -> uStreamer snapshot
  -> target_detector.py
  -> /tmp/eo_target_state
      -> eo_web_control.py -> stop scan
      -> eo_hud_controller.c -> OLED TGT status
  -> buzzer alert
```

## Safety Boundaries

There are two layers of angle limiting:

1. the Raspberry Pi control layer clamps requested values,
2. the TM4C123 firmware validates the received angle again.

The stable limits are:

```text
PAN  : 45° to 135°
TILT : 55° to 125°
```

This protects the mechanical bracket even if a higher-level control request is out of range.

## Design Trade-offs

### Why PWM stays on the TM4C123

Linux is suitable for networking, UI, video, and orchestration, but its scheduler is not intended to generate precise hobby-servo timing directly. The TM4C123 therefore owns the actual PWM outputs.

### Why the stable release uses scan + detection instead of visual lock

Classical trackers were evaluated, but persistent visual lock became sensitive to drift, target appearance, loss/reacquisition timing, and compute load on the Raspberry Pi 3. The stable architecture uses motion detection as an event source and keeps platform movement deterministic.