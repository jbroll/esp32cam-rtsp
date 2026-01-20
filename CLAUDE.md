# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32CAM-RTSP is firmware for ESP32-CAM modules that provides:
- RTSP streaming server (port 554, path `/mjpeg/1`)
- HTTP Motion JPEG streaming (`/stream`)
- HTTP snapshot endpoint (`/snapshot`)
- Web-based configuration interface with mDNS discovery

## Build Commands

This is a PlatformIO project using the Arduino framework for ESP32.

```bash
# Build all board targets
pio run

# Build for a specific board (e.g., AI Thinker)
pio run -e esp32cam_ai_thinker

# Upload firmware (device must be in download mode)
pio run -t upload -e esp32cam_ai_thinker

# Monitor serial output (115200 baud)
pio device monitor

# Update Espressif toolchain
pio pkg update -g -p espressif32

# Erase flash completely (resets all config)
pio run -t erase
```

## HTML Minification

When modifying `html/index.html`, regenerate the minified version:

```bash
./generate_html.sh
```

This runs `minify.py` which requires `pip install minify-html`.

## Architecture

### Board Definitions (`boards/`)
JSON files defining hardware-specific configuration for each supported ESP32-CAM variant. Each file specifies:
- Camera GPIO pin mappings (`CAMERA_CONFIG_PIN_*`)
- Flash/user LED GPIOs
- PSRAM configuration (`CAMERA_CONFIG_FB_LOCATION`, `CAMERA_CONFIG_FB_COUNT`)
- CPU and flash settings

Board selection is done via PlatformIO environment (e.g., `-e esp32cam_ai_thinker`).

### Main Application (`src/main.cpp`)
Single-file application containing:
- Camera initialization and configuration via IotWebConf parameters
- HTTP handlers: `/` (status), `/config`, `/snapshot`, `/stream`, `/flash`
- RTSP server startup on WiFi connection
- Moustache templating for HTML with embedded `html/index.min.html`

### RTSP Server (`lib/rtsp_server/`)
Custom wrapper around Micro-RTSP library managing client connections and streaming.

### Configuration System
Uses IotWebConf library for:
- WiFi credential management
- Camera settings persistence
- Web-based configuration UI
- Captive portal for initial setup (AP mode SSID: `ESP32CAM-RTSP-<MAC>`)

### Key Dependencies (from `platformio.ini`)
- `prampec/IotWebConf@^3.2.1` - WiFi config and web server
- `geeksville/Micro-RTSP@^0.1.6` - RTSP protocol implementation
- `rzeldent/micro-moustache@^1.0.1` - HTML templating

### Settings (`include/settings.h`)
Application defaults including WiFi SSID, RTSP port, and camera parameter defaults.

## Adding New Board Support

1. Create `boards/esp32cam_<boardname>.json` with GPIO mappings
2. Add environment in `platformio.ini`:
   ```ini
   [env:esp32cam_<boardname>]
   board = esp32cam_<boardname>
   ```
