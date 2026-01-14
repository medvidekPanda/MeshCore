# Custom Features - MeshCore Custom Implementations

This document describes custom changes added to MeshCore firmware that are not part of the standard distribution.

## Contents

1. [Sensor with Deep Sleep and Group Channel Messaging](feature_sensor_deep_sleep.md)

---

## Related Files

### Sensor with Deep Sleep and Group Channel Messaging

- `variants/xiao_s3_wio/platformio.ini` - Build profiles for sensor deep sleep
- `variants/xiao_s3_wio/platformio.private.ini.example` - Example configuration with channel name and secret
- `examples/simple_sensor/main.cpp` - Sensor implementation with deep sleep, group channel messaging, and CLI commands

---

## Change History

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

### Commit: Sensor Deep Sleep - CLI Configuration and Persistent Storage

**Status**: Implemented and committed

**Changes**:

- CLI commands for companion ID configuration (`get companion.id`, `set companion.id`) - **legacy, kept for backward compatibility only**
- Persistent storage of companion ID to filesystem (`/companion_id`)
- Validation and error handling for companion ID (64 hex characters)
- Power loss protection for saved configuration (flush, verification, validation on load)
- Fallback to default from build flag if file is corrupted or invalid
- Companion ID loaded from filesystem on startup (if exists and valid)
- **Important**: Companion ID is **no longer used for message sending** - messages are sent to group channel instead (see Sensor Deep Sleep feature documentation)

**Files**:

- `examples/simple_sensor/main.cpp` - CLI commands and persistent storage implementation

### Commit: Add altitude reading support from BMP280 in simple_sensor (1f93b35d)

**Author**: Jan Ptáček  
**Date**: 2025-12-XX

**Changes**:

- Added support for reading altitude from BMP280 sensor
- Altitude value included in sensor data CSV format
- Altitude parsing from LPP telemetry data

**Files**:

- `examples/simple_sensor/main.cpp` - Altitude reading implementation

---

## Build and Upload

### Building and Uploading Firmware

To build and upload firmware for sensor deep sleep variant, use PlatformIO:

```bash
# Build and upload
pio run -e Xiao_S3_WIO_sensor_deep_sleep -t upload

# Monitor Serial output
pio device monitor -e Xiao_S3_WIO_sensor_deep_sleep
```

Available variants:

- `Xiao_S3_WIO_sensor_deep_sleep` - Sensor with deep sleep and group channel messaging

---

## Notes

- **Sensor Deep Sleep**: Channel name and secret configured via build flags (`SENSOR_CHANNEL_NAME`, `SENSOR_CHANNEL_SECRET`), messages sent to encrypted group channel
- **Battery Voltage**: Included in sensor telemetry data (read from `board.getBattMilliVolts()`)
- **Compatibility**: All changes are backward compatible with existing configurations

---

_Document created: 2025-11-22_  
_Last updated: 2025-12-XX_  
_Branch: dev-fogll_
