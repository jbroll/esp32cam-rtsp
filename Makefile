# ESP32CAM-RTSP Makefile
# Requires: .env file with WIFI_SSID and WIFI_PASSWORD

BOARD ?= esp32cam_ai_thinker
PORT ?= $(shell ls /dev/ttyUSB* 2>/dev/null | head -1)

# Load environment variables from .env
include .env
export

.PHONY: build upload monitor clean minify

build:
	pio run -e $(BOARD)

upload: build
	pio run -e $(BOARD) -t upload --upload-port $(PORT)

monitor:
	pio device monitor --port $(PORT) --baud 115200

clean:
	pio run -t clean

minify:
	. .venv/bin/activate && python3 ./minify.py ./html/index.html ./html/index.min.html

# Build and upload in one step
flash: upload

# Erase all flash (resets config)
erase:
	pio run -e $(BOARD) -t erase --upload-port $(PORT)
