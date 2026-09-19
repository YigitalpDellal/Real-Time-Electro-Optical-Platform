# Stable Release Notes

## Stable features

- Manual PAN/TILT control
- CENTER command
- Automatic horizontal scan
- Target acquisition/loss event detection
- Scan stop on target detection
- Buzzer alerts
- OLED telemetry HUD
- Raspberry Pi to TM4C123 UART protocol
- TM4C123 PWM servo control
- Software angle limits
- Live MJPEG camera stream
- Browser control panel

## Known limitation

The target detector is based on background change. Strong moving shadows and abrupt lighting changes can be interpreted as motion. Stable lighting and an empty-scene warm-up are recommended.

## Not part of the stable release

Persistent object tracking with MOSSE/KCF/CSRT was evaluated experimentally but is not claimed as a stable feature.