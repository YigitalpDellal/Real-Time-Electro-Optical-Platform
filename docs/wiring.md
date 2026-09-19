# Wiring

This document describes the wiring used by the final source code.

## Raspberry Pi 3 B+ to TM4C123 UART

The Raspberry Pi uses `/dev/serial0`. The TM4C123 firmware uses UART1 on PB0/PB1.

| Raspberry Pi | Physical pin | Direction | TM4C123 |
|---|---:|---|---|
| GPIO14 / TXD0 | 8 | Pi -> TM4C | PB0 / U1RX |
| GPIO15 / RXD0 | 10 | TM4C -> Pi | PB1 / U1TX |
| GND | any GND pin | common reference | GND |

UART settings:

```text
115200 baud
8 data bits
no parity
1 stop bit
```

UART lines are crossed TX-to-RX and RX-to-TX.

## TM4C123 to Servos

| Function | TM4C123 pin | Peripheral |
|---|---|---|
| PAN servo signal | PB6 | M0PWM0 |
| TILT servo signal | PB7 | M0PWM1 |

Servo PWM:

```text
Frequency: 50 Hz
Period:    20 ms
```

The firmware uses a wide calibration of approximately 500-2500 us for the full 0-180° mapping, while the commanded range is restricted in software:

```text
PAN  : 45-135°
TILT : 55-125°
```

### Servo power

The two MG90S servos should be powered from the external regulated 5 V supply used by the prototype rather than loading the Raspberry Pi rail.

Required grounding:

```text
Raspberry Pi GND
TM4C123 GND
servo supply GND
```

must be connected together.

A 220 uF or 470 uF electrolytic capacitor across the servo 5 V rail near the breadboard can help suppress short supply dips and servo-induced noise. Observe capacitor polarity.

## SSD1306 OLED

The Raspberry Pi controller opens `/dev/i2c-1` and uses address `0x3C`.

| OLED | Raspberry Pi BCM | Physical pin |
|---|---|---:|
| SDA | GPIO2 / SDA1 | 3 |
| SCL | GPIO3 / SCL1 | 5 |
| GND | GND | any GND pin |
| VCC | 3.3 V supply used by the prototype/module | 1 or 17 |

Before running the HUD, the display should normally appear at `0x3c` with:

```bash
i2cdetect -y 1
```

## Buzzer

The detector uses:

```text
BCM GPIO18
physical pin 12
```

The prototype drives the buzzer through the external transistor/resistor driver stage rather than treating the GPIO as a power source. Keep the driver ground common with the Raspberry Pi ground.

Alert patterns:

```text
Target acquired: two short ~1400 Hz beeps
Target lost:     one longer ~700 Hz beep
```

## Camera

The Logitech C270 connects by USB and is expected by the stable configuration as:

```text
/dev/video0
```

The streaming configuration used during testing was 640x480 MJPEG at a requested 30 FPS.

## Power Notes

- Power the Raspberry Pi from a stable supply suitable for the Pi 3 B+.
- Power the servos from the separate regulated 5 V supply used by the build.
- Do not omit the common ground between Pi, TM4C123, and servo supply.
- Avoid commanding mechanical endpoints outside the software limits even if the servo itself advertises a wider nominal rotation range.