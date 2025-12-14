# Companion Radio with Deep Sleep for Ultra-Low Power

### Description

For companion radio devices that need **ultra-low power consumption** (< 1mA vs ~80mA for light sleep), use `Xiao_S3_WIO_companion_radio_deep_sleep`. This variant implements deep sleep mode where the device wakes up only on timer for sensor reading.

### Key Features

1. **Implements deep sleep mode** - device enters deep sleep and wakes up only on timer (for sensor reading)
2. **WiFi and Bluetooth disabled** - `DISABLE_WIFI_OTA=1` and no `BLE_PIN_CODE` to save power
3. **Status LED disabled** - `PIN_STATUS_LED=RADIOLIB_NC` to minimize power consumption
4. **Periodic sensor reading** - automatically reads environmental sensors at configurable intervals (30 minutes default)
5. **Device reset on wakeup** - deep sleep causes full reset, wakeup reason is handled in `setup()`

### Key Differences from Light Sleep

- **Much lower power consumption**: < 1mA in deep sleep vs ~80mA in light sleep
- **Radio is powered off** during deep sleep (cannot receive packets)
- **Device resets on wakeup** - all state is lost, wakeup reason is detected in `setup()`
- **Only timer wakeup** - device wakes up periodically for sensor reading, not on LoRa packets
- **Ideal for battery-powered sensors** that need to operate for months on battery

### Configuration

```ini
[env:Xiao_S3_WIO_companion_radio_deep_sleep]
extends = env:Xiao_S3_WIO_companion_radio_deep_sleep_base
build_flags =
  ${env:Xiao_S3_WIO_companion_radio_deep_sleep_base.build_flags}
  -D ENABLE_DEEP_SLEEP=1
  -D SENSOR_READ_INTERVAL_SECS=1800  ; 30 minutes
  ; Optional: Enable sensor debug logging
  ; -D SENSOR_DEBUG=1
```

**Private configuration** (in `platformio.private.ini`):

```ini
[env:Xiao_S3_WIO_companion_radio_deep_sleep]
extends = env:Xiao_S3_WIO_companion_radio_deep_sleep_base
build_flags =
  ${env:Xiao_S3_WIO_companion_radio_deep_sleep_base.build_flags}
  ; Sensor channel configuration (private)
  -D SENSOR_CHANNEL_NAME='"fogll-mesh-senzor"'
  -D SENSOR_CHANNEL_PSK='"your-128-bit-psk-base64"'
```

### Wakeup Behavior

- Device wakes up from deep sleep after `SENSOR_READ_INTERVAL_SECS` (default: 1800 seconds = 30 minutes)
- On wakeup, device performs full reset
- `setup()` detects wakeup reason using `esp_reset_reason() == ESP_RST_DEEPSLEEP`
- Sensors are read immediately after wakeup
- Sensor data is sent to mesh channel (if configured)
- After sending, device waits 5 seconds (or 30 seconds on first startup) then enters deep sleep again

### Power Consumption

- **Deep sleep**: 
  - ESP32-S3 module alone: ~8-20 µA (only RTC and wakeup timer active)
  - XIAO ESP32S3 development board: ~2-8 mA (due to power LED, voltage regulator, USB-Serial chip)
  - Note: Development board components (power LED, voltage regulator) cannot be disabled via software
- **Active mode** (sensor reading + transmission): ~50-100mA
- **Active duration**: ~5-30 seconds per cycle (depending on startup vs wakeup)
- **Average power**: ~0.1-0.5mA (depending on sensor reading interval)

### Usage Example

1. **Upload firmware** with companion radio deep sleep variant:

   ```bash
   pio run -e Xiao_S3_WIO_companion_radio_deep_sleep -t upload
   ```

2. **Monitor Serial output**:

   ```bash
   pio device monitor -e Xiao_S3_WIO_companion_radio_deep_sleep
   ```

2. **Monitor sensor readings** via Serial (device will reset on each wakeup):

   ```
   [SENSOR] Wakeup from deep sleep
   [SENSOR] Reading sensors (interval: 1800 secs)
     [CH1] Voltage: 1.150 V
     [CH1] Temperature: 27.20°C
     [CH1] Humidity: 46.50%
     [CH1] Pressure: 1013.25 hPa
     [CH1] Altitude: 150.00 m
   [SENSOR] Sensor reading complete
   [SENSOR] Entering deep sleep for 1800 seconds...
   ```

3. **Debug mode** (disables deep sleep, enables more frequent sensor reads):

   To enable debug mode, uncomment `MESH_DEBUG=1` in the base configuration:

   ```ini
   build_flags =
     ${env:Xiao_S3_WIO_companion_radio_deep_sleep_base.build_flags}
     -D ENABLE_DEEP_SLEEP=1
     -D SENSOR_READ_INTERVAL_SECS=60  ; 1 minute for debug
     -D MESH_DEBUG=1  ; Disables deep sleep, enables BT
   ```

   **Note**: When `MESH_DEBUG=1` is set, deep sleep is automatically disabled and device operates in normal mode with Bluetooth enabled.

4. **Send sensor data to private channel** (configure in `platformio.private.ini`):

   See [Companion Radio with Light Sleep](feature_light_sleep.md) section for channel configuration details.

### Important Notes

- **Deep sleep resets the device** - all RAM is lost, only RTC memory is preserved
- **Radio cannot receive packets** during deep sleep - device only wakes up on timer
- **First startup** (not from deep sleep): device waits 30 seconds before entering deep sleep
- **Wakeup from deep sleep**: device waits 5 seconds before entering deep sleep again
- **USB connection**: If USB is connected, device may not enter deep sleep (depends on board implementation)
- **Battery voltage**: Measured and included in sensor telemetry on each wakeup

### Files

- `variants/xiao_s3_wio/platformio.ini` - Build profiles for companion radio deep sleep
- `examples/companion_radio/main.cpp` - Companion radio implementation with deep sleep

---

[← Back to Custom Features Index](custom_features.md)
