PYTHON ?= python3
CC ?= gcc

TARGET := eo_hud_controller_target
CONTROLLER_SRC := controller/eo_hud_controller.c
PYTHON_SOURCES := \
	raspberry_pi/target_detector.py \
	raspberry_pi/eo_web_control.py

.PHONY: all build check pycheck clean

all: build

build:
	$(CC) -std=c11 -O2 -Wall -Wextra -o $(TARGET) $(CONTROLLER_SRC)

pycheck:
	$(PYTHON) -m py_compile $(PYTHON_SOURCES)

check: pycheck build

clean:
	rm -f $(TARGET)
	find raspberry_pi -type d -name __pycache__ -prune -exec rm -rf {} +
