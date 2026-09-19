# Troubleshooting and Engineering Notes

The project was developed iteratively. The issues below are the ones that materially changed the stable design.

## Servo jitter, weak motion, or movement changing when the camera is active

**Observed:** servo motion became small, unstable, or noisy under heavier Raspberry Pi activity.

**Cause:** the servo load and switching current were not well isolated from the computing side.

**Fix used:**

- external regulated 5 V supply for the servos,
- common ground between Pi, TM4C123, and servo supply,
- 220 uF / 470 uF bulk capacitance near the servo power rail,
- keep the PWM signal on the TM4C123 while the Pi handles high-level commands.

## Servo noise near the TILT endpoint

**Observed:** the tilt servo buzzed or strained near an extreme position.

**Cause:** mechanical binding/end-stop load.

**Fix used:** restrict the operating range to 55-125° for TILT. PAN is restricted to 45-135°.

## Uncontrolled or unexpectedly large mechanical rotation

**Observed:** the bracket could move to a mechanically unsafe orientation during setup.

**Cause:** servo horn alignment and mechanical reference were not yet matched to the software center.

**Fix used:** center the servo at 90° first, then mechanically refit the horn/bracket before applying the final angle limits.

## UART startup timeout

**Observed:** the first `PING` could time out even though later communication worked.

**Cause:** serial startup/settling timing and stale receive data.

**Fix used:**

- flush stale serial data before sending a new command,
- permit up to three startup PING attempts,
- consider the link established only after `ACK`.

## Automatic scan continued too long after STOP or generated UART timeouts

**Observed:** scan commands accumulated faster than the controller could complete the request/response cycle.

**Fix used:**

```text
SCAN_STEP  = 10 degrees
SCAN_DELAY = 1.20 seconds
```

This reduced command traffic and made STOP behavior predictable.

## Camera not found or stream unavailable

**Checks:**

```bash
ls -l /dev/video0
```

Start the tested stream configuration:

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

The detector expects `http://127.0.0.1:8080/snapshot`.

## False target detection from shadows or lighting changes

**Observed:** a moving shadow could be detected as a target.

**Reason:** the stable detector is background-difference based. It detects sufficiently large scene change; it does not classify people or distinguish illumination changes from physical objects.

**Practical mitigation used:**

- keep lighting stable,
- avoid pointing the camera directly into a bright window,
- start with an empty/still scene,
- allow the initial 20-frame background warm-up to finish,
- avoid moving through the scene during background learning.

Stable detector settings:

```text
threshold difference = 25
minimum contour area = 4000
acquire confirmation = 4 frames
lost confirmation    = 15 frames
```

These settings were kept because they provided a useful balance for the demonstrated environment. They are environment-dependent and may need retuning elsewhere.

## Target-loss delay

**Observed:** loss is not declared immediately after the target leaves.

**Reason:** `LOST_FRAMES = 15` intentionally adds debounce to avoid dropping the target because of a few weak frames.

**Trade-off:** lower values react faster but are more sensitive to momentary detector failures.

## OLED remains black

**Checks:**

```bash
i2cdetect -y 1
```

Expected display address:

```text
0x3c
```

Also verify SDA/SCL wiring, power, ground, and `/dev/i2c-1` availability.

The final controller initializes the SSD1306 and maintains its own 1024-byte framebuffer.

## OLED status indicator was difficult to read

**Observed:** the first small status marker was hard to distinguish on the 128x64 display.

**Fix used:** increase the status marker to a 5x5 symbol, using filled vs. hollow states for clearer visual separation.

## Buzzer works in a standalone test but not during the full system test

**Checks:**

- confirm BCM GPIO18 is used,
- verify the transistor/resistor driver stage,
- confirm a common ground,
- make sure only the intended detector process owns the buzzer during the test.

Standalone GPIO tests were useful for separating hardware faults from detector logic.

## Experimental visual tracking drift

MOSSE, KCF, and CSRT were tested during development. Problems included:

- drift onto background regions,
- difficulty staying locked during target appearance changes,
- slow or inconsistent loss detection,
- reacquisition complexity,
- additional compute load on Raspberry Pi 3.

These results led to the stable architecture: deterministic manual/scan control with motion-based target events. The tracking experiments are treated as engineering exploration, not as a claimed stable feature.