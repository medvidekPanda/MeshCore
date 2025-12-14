# Custom Features - MeshCore Custom Implementations

This document describes custom changes added to MeshCore firmware that are not part of the standard distribution.

## Contents

1. [MQTT Bridge for Room Server](feature_mqtt_bridge.md)
2. [WiFi and Bluetooth CLI Control](feature_wifi_bt_control.md)
3. [Light Sleep for Ultra-Low Power](feature_light_sleep.md)
4. [Companion Radio with Deep Sleep for Ultra-Low Power](feature_companion_radio_deep_sleep.md)
5. [Sensor with Deep Sleep and Group Channel Messaging](feature_sensor_deep_sleep.md)
6. [USB Serial Configuration (When Bluetooth is Disabled)](feature_usb_serial.md)

---

## Related Files

### Sensor with Deep Sleep and Group Channel Messaging

- `variants/xiao_s3_wio/platformio.ini` - Build profiles for sensor deep sleep
- `variants/xiao_s3_wio/platformio.private.ini.example` - Example configuration with channel name and secret
- `examples/simple_sensor/main.cpp` - Sensor implementation with deep sleep, group channel messaging, and CLI commands

### MQTT Bridge

- `src/helpers/bridges/MQTTBridge.h` - MQTT bridge header
- `src/helpers/bridges/MQTTBridge.cpp` - MQTT bridge implementation
- `examples/simple_room_server/MyMesh.h` - Room Server MyMesh with bridge support
- `examples/simple_room_server/MyMesh.cpp` - Room Server MyMesh implementation
- `read.py` - Python script for reading MQTT messages
- `send.py` - Python script for sending MQTT messages
- `requirements.txt` - Python dependencies

### WiFi/BT CLI

- `src/helpers/CommonCLI.h` - CLI interface with WiFi/BT commands
- `src/helpers/CommonCLI.cpp` - CLI implementation
- `examples/simple_repeater/MyMesh.h` - Repeater MyMesh with WiFi/BT methods
- `examples/simple_repeater/MyMesh.cpp` - Repeater MyMesh implementation
- `examples/simple_repeater/main.cpp` - Repeater main with conditional initialization
- `variants/xiao_s3_wio/platformio.ini` - Build configuration

### Light Sleep

- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Light sleep implementation for ESP32-S3
- `src/helpers/radiolib/RadioLibWrappers.h/cpp` - RadioLib wrapper with interrupt reinitialization
- `examples/simple_repeater/main.cpp` - Light sleep integration with USB detection and optimized TX completion

### Battery Voltage Measurement

- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Battery voltage measurement on A0 pin (GPIO 1) with 1/2 voltage divider
- `variants/xiao_s3_wio/platformio.ini` - PIN_VBAT_READ=1 configuration for lowpower variants
- Based on [Seeed Studio wiki](https://wiki.seeedstudio.com/check_battery_voltage/) - requires external 200kΩ resistor divider circuit
- **Automatic safety monitoring** - battery voltage is measured every 1 hour (3600 seconds) in `low_sleep` variant
- Measurement happens automatically on wakeup from light sleep timeout (safety watchdog)
- Uses 16-sample averaging to remove spike-like errors during communication (as recommended by Seeed Studio)
- Battery voltage multiplier: 2x (due to 1/2 voltage divider on A0 pin)

---

## Change History

### Commit: Add MQTT bridge support for Room Server (732fbc0d)

**Author**: Jan Ptáček  
**Date**: 2025-11-22

**Changes**:

- Added MQTT bridge implementation for Room Server (`MQTTBridge.h/cpp`)
- Integration into Room Server firmware (`simple_room_server`)
- Python scripts for reading/sending (`read.py`, `send.py`)
- Channel filtering support via `MQTT_FILTER_CHANNELS`
- Moved credentials to `platformio.private.ini` (not in git)
- Decoding and decryption of mesh packets in Python script
- Fixed `SerialWifiInterface` frame parsing (state machine)
- WiFi initialization in repeater for MQTT bridge (optional)

**Files**:

- `src/helpers/bridges/MQTTBridge.h/cpp` - MQTT bridge implementation
- `examples/simple_room_server/MyMesh.h/cpp` - Room Server integration
- `examples/simple_room_server/main.cpp` - WiFi initialization
- `read.py`, `send.py` - Python scripts
- `variants/xiao_s3_wio/platformio.ini` - Build configuration
- `variants/xiao_s3_wio/platformio.private.ini.example` - Template for credentials

### Uncommitted changes: WiFi and BT CLI control for repeater

**Status**: Implemented, but not yet committed

**Changes**:

- Added WiFi and BT CLI control via `set wifi on/off` and `set bt on/off`
- Added `get wifi` and `get bt` commands to display status
- Conditional initialization of WiFi/BT at startup (only if enabled)
- Lowpower build variant `Xiao_S3_WIO_repeater_lowpower`
- State storage in NodePrefs (`wifi_enabled`, `bt_enabled`)
- WiFi and BT are compiled but not initialized by default

**Files**:

- `src/helpers/CommonCLI.h/cpp` - CLI commands
- `examples/simple_repeater/MyMesh.h/cpp` - Repeater integration
- `examples/simple_repeater/main.cpp` - Conditional initialization
- `variants/xiao_s3_wio/platformio.ini` - Lowpower variant

### Uncommitted changes: Light Sleep for Ultra-Low Power

**Status**: Implemented and tested

**Changes**:

- Added light sleep implementation for ESP32-S3 (`XiaoS3WIOBoard.h`)
- GPIO wakeup support for LoRa packet detection (DIO1 pin)
- Interrupt handler management (remove before sleep, reinit after wakeup)
- RadioLib interrupt reinitialization method (`reinitInterrupts()`)
- Light sleep variant `Xiao_S3_WIO_repeater_low_sleep`
- Automatic sleep after 5 seconds of inactivity
- Fast wakeup on incoming LoRa packets
- Minimal Serial output to reduce power consumption
- **USB connection detection** - device stays awake if USB is connected
- **Extended awake time** - 2 minutes after hard reset if USB is connected
- **Optimized TX completion detection** - uses `isInRecvMode()` instead of `isSendComplete()` to avoid conflicts with Dispatcher
- **Battery voltage safety monitoring** - automatic measurement every 1 hour (3600 seconds) via light sleep timeout
- Battery voltage measurement on GPIO 1 (A0 pin) with 1/2 voltage divider (200kΩ resistors)

**Power savings**:

- Light sleep: ~5-15mA (vs ~50-100mA active)
- Radio stays in RX mode during sleep
- Fast response time compared to deep sleep

**USB detection**:

- USB detection was initially implemented but had a bug (always returned true)
- USB detection is currently disabled to ensure device enters light sleep correctly
- Device will enter light sleep regardless of USB connection (for production use)
- Future: proper USB detection can be added if needed for development

**Files**:

- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Light sleep implementation
- `src/helpers/radiolib/RadioLibWrappers.h/cpp` - Interrupt reinitialization
- `examples/simple_repeater/main.cpp` - Light sleep integration with USB detection
- `variants/xiao_s3_wio/platformio.ini` - Light sleep variant

### Commit: Add Xiao_S3_WIO_sensor_deep_sleep profile with group channel messaging (c41adef9)

**Author**: Jan Ptáček  
**Date**: 2025-12-11

**Changes**:

- New profile `Xiao_S3_WIO_sensor_deep_sleep_base` and `Xiao_S3_WIO_sensor_deep_sleep`
- Added `sendMessageToChannel` function for sending messages to encrypted group channel
- Automatic message sending every 5 minutes on sensor read
- Added logging for tracking message sending process
- Deep sleep support (same as companion_radio)
- Sensor reading using `sensors.querySensors()` and `board.getBattMilliVolts()`
- Parsing telemetry data using `LPPReader`
- Messages sent via `PAYLOAD_TYPE_GRP_TXT` to encrypted group channel

**Files**:

- `examples/simple_sensor/main.cpp` - Sensor implementation
- `variants/xiao_s3_wio/platformio.ini` - Build profiles
- `variants/xiao_s3_wio/platformio.private.ini.example` - Example configuration

### Commit: Add sensor communication error handling (5cb21cbd)

**Author**: Jan Ptáček  
**Date**: 2025-12-11

**Changes**:

- Check if sensors are communicating before sending data
- Send error message instead of null values when sensors not responding
- Send partial error message when some sensors missing
- Normal CSV format when all sensors OK

**Files**:

- `examples/simple_sensor/main.cpp` - Error handling implementation

### Uncommitted changes: Sensor Deep Sleep - CLI Configuration and Persistent Storage

**Status**: Implemented, but not yet committed

**Changes**:

- CLI commands for companion ID configuration (`get companion.id`, `set companion.id`) - legacy, kept for backward compatibility
- Persistent storage of companion ID to filesystem (`/companion_id`)
- Validation and error handling for companion ID (64 hex characters)
- Power loss protection for saved configuration (flush, verification, validation on load)
- Fallback to default from build flag if file is corrupted or invalid
- Companion ID loaded from filesystem on startup (if exists and valid)
- **Note**: Companion ID is no longer used for message sending (messages are sent to group channel instead)

**Files**:

- `examples/simple_sensor/main.cpp` - CLI commands and persistent storage implementation

---

## Build and Upload

### Building and Uploading Firmware

To build and upload firmware for Xiao S3 WIO variants, use PlatformIO:

```bash
# Build and upload
pio run -e Xiao_S3_WIO_repeater_low_sleep -t upload

# Monitor Serial output
pio device monitor -e Xiao_S3_WIO_repeater_low_sleep
```

**Example for sensor deep sleep variant:**

```bash
# Build and upload
pio run -e Xiao_S3_WIO_sensor_deep_sleep -t upload

# Monitor Serial output
pio device monitor -e Xiao_S3_WIO_sensor_deep_sleep
```

Available variants:

- `Xiao_S3_WIO_repeater` - Standard repeater (battery voltage measurement enabled)
- `Xiao_S3_WIO_repeater_lowpower` - Low power repeater with WiFi/BT disabled by default (battery voltage measurement enabled)
- `Xiao_S3_WIO_repeater_super_lowpower` - Ultra low power (80MHz CPU, sensors disabled) (battery voltage measurement enabled)
- `Xiao_S3_WIO_repeater_low_sleep` - Low power with light sleep (automatic hourly battery monitoring)
- `Xiao_S3_WIO_companion_radio_usb` - Companion radio with USB interface (battery voltage in telemetry)
- `Xiao_S3_WIO_companion_radio_ble` - Companion radio with BLE interface (battery voltage in telemetry)
- `Xiao_S3_WIO_companion_radio_serial` - Companion radio with serial interface (battery voltage in telemetry)
- `Xiao_S3_WIO_room_server_mqtt` - Room Server with MQTT bridge
- `Xiao_S3_WIO_sensor_deep_sleep` - Sensor with deep sleep and group channel messaging

### Battery Voltage Measurement

For all Xiao S3 WIO variants (repeater and companion), battery voltage measurement on A0 pin (GPIO 1) is enabled by default. This requires an external voltage divider circuit (1/2 ratio with 200kΩ resistors) as described in the [Seeed Studio wiki](https://wiki.seeedstudio.com/check_battery_voltage/).

**For `repeater_low_sleep` variant:**

- Battery voltage is automatically measured every 1 hour (3600 seconds) via light sleep timeout wakeup
- This provides safety monitoring of battery health even when no packets are received
- Measurement happens only on timeout wakeup (not on packet wakeup) to minimize power consumption
- Battery voltage is logged to Serial when measured: `[BATT] Safety check (hourly): Battery: X.XXXV (XXXmV)`
- The 1-hour timeout also serves as a safety watchdog - prevents device from sleeping forever if radio fails

**For companion variants (`companion_radio_usb`, `companion_radio_ble`, `companion_radio_serial`):**

- Battery voltage is measured on-demand when telemetry is requested (not automatically)
- Battery voltage is included in telemetry responses (`REQ_TYPE_GET_TELEMETRY_DATA`)
- Follows existing telemetry intervals - sent when requested by contacts/apps, not at fixed intervals
- Battery voltage is also available in stats responses and UI display
- No automatic hourly measurement (unlike repeater low_sleep variant)

**Measurement method:**

- Uses A0 pin (GPIO 1) with 1/2 voltage divider (two 200kΩ resistors)
- 16-sample averaging to remove spike-like errors during communication
- Voltage multiplier: 2x (due to 1/2 divider)
- Based on `analogReadMilliVolts()` with 12-bit ADC resolution

**Battery percentage calculation:**

- Measurements are taken during device operation (under load), so voltage is lower than no-load voltage
- Battery percentage is calculated using: 3000mV (0%) to 4100mV (100%)
- LiPo batteries: 4.2V no-load = 100%, but under load (during TX/RX) voltage drops to ~4.1V even when fully charged
- The 4100mV maximum accounts for voltage sag under load, providing more accurate battery percentage readings

---

## Notes

- **MQTT Bridge**: Requires WiFi connection and functional MQTT broker
- **WiFi/BT CLI**: Changes require reboot to apply
- **Lowpower variant**: WiFi and BT are compiled but not initialized by default
- **Light Sleep**: USB connection detection keeps device awake for PC access
- **Battery Measurement**: Requires external voltage divider circuit on A0 pin (GPIO 1) - two 200kΩ resistors
- **Battery Safety Monitoring**: In `low_sleep` variant, battery voltage is measured every 1 hour automatically
- **Compatibility**: All changes are backward compatible with existing configurations
- **Sensor Deep Sleep**: Channel name and secret configured via build flags (`SENSOR_CHANNEL_NAME`, `SENSOR_CHANNEL_SECRET`), messages sent to encrypted group channel

---

_Document created: 2025-11-22_  
_Last updated: 2025-12-11_  
_Branch: time-sync_
