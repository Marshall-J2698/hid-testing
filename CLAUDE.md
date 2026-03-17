# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an ESP-IDF USB HID (Human Interface Device) host driver example for ESP32-S2, ESP32-S3, and ESP32-P4 microcontrollers. It demonstrates USB HID host communication (keyboard, mouse, generic HID devices) using the `espressif__usb_host_hid` managed component.

## Build Commands

Uses ESP-IDF v5.5.2 with CMake/Ninja. The ESP-IDF is installed at `/Users/johnsonm3/.espressif/v5.5.2/esp-idf`.

```bash
# Build, flash, and monitor (replace PORT with actual serial port)
idf.py -p PORT flash monitor

# Build only
idf.py build

# Configure project
idf.py menuconfig

# Clean build
idf.py fullclean
```

Supported targets: `esp32p4`, `esp32s2`, `esp32s3`.

## Architecture

### Two-Tier Callback System

The driver uses a two-level callback model to decouple USB interrupt context from application logic:

1. **Driver-level callback** (`hid_host_driver_event_cb_t`) — fires on device connect/disconnect
2. **Interface-level callback** (`hid_host_interface_event_cb_t`) — fires on input reports (keyboard keystrokes, mouse movement)

Callbacks execute in USB interrupt handler context. The example app uses a FreeRTOS queue (`app_event_queue`) to pass events to the main task for safe processing.

### HID Interface State Machine

Interfaces in `hid_host.c` progress through: `NOT_INITIALIZED → IDLE → READY → ACTIVE → WAIT_USER_DELETION` (or `SUSPENDED`). State is guarded by spinlocks; invalid state transitions are rejected.

### Two-Stage Device Initialization

1. **Detection**: Driver callback fires when USB device connects
2. **Activation**: Application must call `hid_host_device_open()` then `hid_host_device_start()` to begin receiving reports

### Report Handling

- **Boot protocol** (keyboard/mouse): Fixed format, parsed into typed structs (`hid_mouse_input_report_boot_t`)
- **Generic protocol**: Raw bytes, printed as hex
- Keyboard tracks modifier keys (shift, alt) and generates key-press/key-release events

## Key Files

| File | Purpose |
|------|---------|
| `main/hid_host_example.c` | Example app — event loop, GPIO quit button, keyboard/mouse printing |
| `managed_components/espressif__usb_host_hid/hid_host.c` | Core HID class driver (1,793 lines) |
| `managed_components/espressif__usb_host_hid/include/usb/hid_host.h` | Public API |
| `managed_components/espressif__usb_host_hid/include/usb/hid.h` | HID constants and descriptor structs |
| `managed_components/espressif__usb_host_hid/include/usb/hid_usage_keyboard.h` | HID key codes |
| `sdkconfig.defaults` | Enables `CONFIG_USB_HOST_HUBS_SUPPORTED` |
| `main/idf_component.yml` | Declares dependency on `usb_host_hid ^1.0.1` |

## Testing

The managed component includes its own test infrastructure:

- `managed_components/espressif__usb_host_hid/test_app/` — requires two ESP32 boards (one host, one acting as HID device via TinyUSB)
- `managed_components/espressif__usb_host_hid/host_test/` — Linux host-side tests for CI

Tests cover: device install/uninstall, class requests, error handling, and mock HID device behavior.
