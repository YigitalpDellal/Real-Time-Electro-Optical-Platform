#!/usr/bin/env python3

import time
import urllib.request

import cv2
import numpy as np
from gpiozero import PWMOutputDevice


# ============================================================
# Real-Time Electro-Optical Platform
# Target Detection + Buzzer Alert Module
# ============================================================

SNAPSHOT_URL = "http://127.0.0.1:8080/snapshot"

FRAME_WIDTH = 320
FRAME_HEIGHT = 240

# Minimum contour area required to consider something a target.
MIN_TARGET_AREA = 4000

# Debounce / confirmation values.
ACQUIRE_FRAMES = 4
LOST_FRAMES = 15

# Number of initial frames used to learn the empty scene.
WARMUP_FRAMES = 20

# Approximate detector rate.
LOOP_DELAY = 0.12

# Raspberry Pi BCM numbering.
BUZZER_GPIO = 18

# File that other EO modules can later read.
STATE_FILE = "/tmp/eo_target_state"


def write_state(state):
    try:
        with open(STATE_FILE, "w") as f:
            f.write(state + "\n")
    except OSError:
        pass


def tone(frequency, duration):
    """
    Generate one tone using the passive buzzer.
    """
    buzzer = PWMOutputDevice(
        BUZZER_GPIO,
        frequency=frequency,
        initial_value=0
    )

    buzzer.value = 0.5
    time.sleep(duration)
    buzzer.value = 0

    buzzer.close()


def target_acquired_alert():
    """
    TARGET ACQUIRED:
    Two short high-frequency beeps.
    """
    for _ in range(2):
        tone(1400, 0.12)
        time.sleep(0.10)


def target_lost_alert():
    """
    TARGET LOST:
    One longer low-frequency beep.
    """
    tone(700, 0.55)


def get_frame():
    """
    Request one JPEG frame from uStreamer and decode it
    into an OpenCV image.
    """
    with urllib.request.urlopen(
        SNAPSHOT_URL,
        timeout=2.0
    ) as response:

        image_data = response.read()

    image_array = np.frombuffer(
        image_data,
        dtype=np.uint8
    )

    frame = cv2.imdecode(
        image_array,
        cv2.IMREAD_COLOR
    )

    if frame is None:
        raise RuntimeError("Unable to decode camera frame.")

    return frame


def prepare_frame(frame):
    """
    Reduce processing load and prepare the image
    for motion/background comparison.
    """
    frame = cv2.resize(
        frame,
        (FRAME_WIDTH, FRAME_HEIGHT)
    )

    gray = cv2.cvtColor(
        frame,
        cv2.COLOR_BGR2GRAY
    )

    gray = cv2.GaussianBlur(
        gray,
        (21, 21),
        0
    )

    return frame, gray


def main():

    print()
    print("==========================================")
    print(" Real-Time Electro-Optical Platform")
    print(" Target Detection Module")
    print("==========================================")
    print()

    print("[INFO] Camera source :", SNAPSHOT_URL)
    print("[INFO] Processing    : 320x240")
    print("[INFO] Buzzer GPIO   : GPIO18")
    print("[INFO] Learning background...")

    write_state("NO_TARGET")

    background = None

    warmup_remaining = WARMUP_FRAMES

    target_active = False

    acquire_counter = 0
    lost_counter = 0

    camera_error_counter = 0

    try:

        while True:

            # ------------------------------------------------
            # Get current camera frame
            # ------------------------------------------------

            try:
                frame = get_frame()
                camera_error_counter = 0

            except Exception as error:

                camera_error_counter += 1

                if camera_error_counter == 1 or \
                   camera_error_counter % 10 == 0:

                    print(
                        "[WARNING] Camera frame unavailable:",
                        error
                    )

                time.sleep(0.5)
                continue

            _, gray = prepare_frame(frame)

            # ------------------------------------------------
            # Initial background
            # ------------------------------------------------

            if background is None:

                background = gray.astype("float")

                time.sleep(LOOP_DELAY)
                continue

            # ------------------------------------------------
            # Warm-up / scene learning
            # ------------------------------------------------

            if warmup_remaining > 0:

                cv2.accumulateWeighted(
                    gray,
                    background,
                    0.20
                )

                warmup_remaining -= 1

                if warmup_remaining == 0:

                    print("[READY] Target detector armed.")
                    print("[READY] Waiting for target...")
                    print()

                time.sleep(LOOP_DELAY)
                continue

            # ------------------------------------------------
            # Compare current frame with learned background
            # ------------------------------------------------

            background_image = cv2.convertScaleAbs(
                background
            )

            difference = cv2.absdiff(
                gray,
                background_image
            )

            threshold = cv2.threshold(
                difference,
                25,
                255,
                cv2.THRESH_BINARY
            )[1]

            threshold = cv2.dilate(
                threshold,
                None,
                iterations=2
            )

            contours, _ = cv2.findContours(
                threshold,
                cv2.RETR_EXTERNAL,
                cv2.CHAIN_APPROX_SIMPLE
            )

            largest_area = 0

            for contour in contours:

                area = cv2.contourArea(contour)

                if area > largest_area:
                    largest_area = area

            target_detected = (
                largest_area >= MIN_TARGET_AREA
            )

            # ------------------------------------------------
            # NO TARGET -> TARGET ACQUIRED
            # ------------------------------------------------

            if not target_active:

                if target_detected:

                    acquire_counter += 1

                else:

                    acquire_counter = 0

                    # Update the empty-scene model slowly only
                    # when no target is currently detected.
                    cv2.accumulateWeighted(
                        gray,
                        background,
                        0.04
                    )

                if acquire_counter >= ACQUIRE_FRAMES:

                    target_active = True

                    acquire_counter = 0
                    lost_counter = 0

                    write_state("TARGET_ACQUIRED")

                    print(
                        "[TARGET ACQUIRED]"
                        f" area={largest_area:.0f}"
                    )

                    target_acquired_alert()

                    write_state("TARGET_PRESENT")

            # ------------------------------------------------
            # TARGET PRESENT -> TARGET LOST
            # ------------------------------------------------

            else:

                if target_detected:

                    lost_counter = 0

                else:

                    lost_counter += 1

                if lost_counter >= LOST_FRAMES:

                    target_active = False

                    lost_counter = 0
                    acquire_counter = 0

                    print("[TARGET LOST]")

                    write_state("TARGET_LOST")

                    target_lost_alert()

                    # Start learning the now-empty scene again.
                    background = gray.astype("float")
                    warmup_remaining = 5

                    write_state("NO_TARGET")

            time.sleep(LOOP_DELAY)

    except KeyboardInterrupt:

        print()
        print("[INFO] Detector stopped by user.")

        write_state("OFFLINE")


if __name__ == "__main__":
    main()