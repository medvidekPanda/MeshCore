# Light Sleep for Ultra-Low Power

### Description

Light sleep mode provides ultra-low power consumption with fast wakeup capability. Unlike deep sleep, light sleep allows the radio to stay in RX mode, enabling the device to wake up quickly on incoming LoRa packets.

### Light Sleep Variant

For ultra-low power consumption with fast wakeup, use `Xiao_S3_WIO_repeater_low_sleep`. This variant:

1. Inherits from `lowpower` (WiFi/BT disabled).
2. **Implements light sleep mode** - device enters light sleep after 5 seconds of inactivity.
3. **GPIO wakeup** - wakes up automatically when LoRa packet is received (DIO1 interrupt).
4. **Fast wakeup** - light sleep allows radio to stay in RX mode, enabling faster response than deep sleep.
5. **Battery voltage monitoring** - automatically measures battery voltage every hour for safety monitoring.

**Key features:**

- Device enters light sleep after 5 seconds of inactivity
- Automatically wakes up on incoming LoRa packet
- Radio stays in RX mode during sleep (unlike deep sleep)
- Interrupt handler is properly managed to prevent conflicts
- Minimal power consumption while maintaining fast response
- **Safety watchdog** - wakes up every 1 hour (3600 seconds) to measure battery voltage, even if no packets are received
- **Immediate packet processing** - packet that wakes device is processed immediately to prevent loss
- **Radio state management** - proper RX mode initialization after wakeup for trace route support

**Power consumption:**

- Light sleep: ~5-15mA (depends on radio module)
- Active mode: ~50-100mA
- Significant savings compared to always-active mode

**Technical implementation:**

- GPIO interrupt handler is removed before sleep to prevent conflicts
- GPIO wakeup is configured for DIO1 pin (LoRa packet detection)
- After wakeup, RadioLib interrupt handler is re-initialized
- Serial output is minimized to reduce power consumption
- USB connection detection using `Serial.availableForWrite()` (ESP32-S3 USB-Serial-JTAG)
- If USB is connected, device never enters light sleep (stays awake for PC access)
- After hard reset, if USB is connected, device stays awake for 2 minutes (120 seconds)
- Optimized TX completion detection using `isInRecvMode()` instead of `isSendComplete()` to avoid conflicts with Dispatcher

```ini
[env:Xiao_S3_WIO_repeater_low_sleep]
extends = env:Xiao_S3_WIO_repeater_lowpower
build_flags =
  ${env:Xiao_S3_WIO_repeater_lowpower.build_flags}
  -D ENABLE_LIGHT_SLEEP
  ; Optional: set timeout for light sleep (in seconds)
  ; -D LIGHT_SLEEP_TIMEOUT=60
```

**Note**: If `LIGHT_SLEEP_TIMEOUT` is not defined, device will use default 1-hour safety watchdog timeout for battery voltage monitoring.

**Battery Voltage Monitoring:**

- Device automatically wakes up every 1 hour (3600 seconds) to measure battery voltage
- Battery voltage is measured and logged to Serial when waking up from timeout
- This provides safety monitoring of battery health even when no packets are received
- Battery measurement happens only on timeout wakeup, not on packet wakeup (to save power)
- Measurement uses the same method as manual battery checks (A0 pin with 1/2 voltage divider)

### Companion Radio with Light Sleep and Environmental Sensors

For companion radio devices that need to operate in low-power mode while periodically reading environmental sensors, use `Xiao_S3_WIO_companion_radio_low_sleep`. This variant:

1. **Implements light sleep mode** - device enters light sleep and wakes up on LoRa packet (DIO1 interrupt) or timeout for sensor reading
2. **WiFi and Bluetooth disabled** - no BLE_PIN_CODE or WIFI_SSID defined to save power
3. **Periodic sensor reading** - automatically reads environmental sensors at configurable intervals
4. **Sensor data logging** - logs all sensor readings to Serial for debugging

**Key features:**

- Device enters light sleep after processing
- Automatically wakes up on incoming LoRa packet or sensor reading timeout
- Radio stays in RX mode during sleep (unlike deep sleep)
- **Sensor reading interval**: 30 minutes (1800 seconds) for production, 1 minute (60 seconds) for debug
- **Supported sensors**: SHT40 (temperature, humidity) and BMP280 (pressure, altitude)
- **BMP280 auto-detection**: automatically tries both I2C addresses (0x76 and 0x77)
- **Sensor data logging**: all readings are logged to Serial in human-readable format
- **Battery voltage**: included in sensor telemetry

**Configuration:**

```ini
[env:Xiao_S3_WIO_companion_radio_low_sleep]
extends = Xiao_S3_WIO
build_flags =
  ${Xiao_S3_WIO.build_flags}
  -D ENABLE_LIGHT_SLEEP=1
  -D DISABLE_WIFI_OTA=1
  -D PIN_STATUS_LED=RADIOLIB_NC
  ; Debug mode: 1 minute sensor reading interval
  -D DEBUG_SENSOR_READ=1
  ; Optional: Enable Bluetooth in debug mode (for USB-powered development)
  ; In debug mode, device is usually USB-powered, so power consumption is less critical
  ; For production/external use, remove BLE_PIN_CODE to disable BT and save power
  ; -D BLE_PIN_CODE=123456
  ; Production mode: 30 minute sensor reading interval (remove DEBUG_SENSOR_READ)
```

**Sensor Reading:**

- Sensors are read when device wakes up from timeout (not on packet wakeup)
- Data is encoded in CayenneLPP format
- All sensor values are logged to Serial:
  - Temperature (°C)
  - Humidity (%)
  - Pressure (hPa)
  - Altitude (m)
  - Battery voltage (V)
  - Current (A) and Power (W) if INA3221 is present

**BMP280 I2C Address Detection:**

The BMP280 sensor can use either I2C address 0x76 or 0x77 depending on the solder bridge configuration. The firmware automatically tries both addresses during initialization:

1. First attempts address 0x76 (default)
2. If not found, tries address 0x77 (alternate)
3. Logs which address was successful

**Power consumption:**

- Light sleep: ~5-15mA (depends on radio module)
- Active mode (sensor reading): ~50-100mA
- Sensor reading duration: ~100-200ms per reading
- Significant savings compared to always-active mode

**Channel Configuration:**

To automatically send sensor readings to a private mesh channel, configure the channel name and PSK:

```ini
build_flags =
  ${Xiao_S3_WIO.build_flags}
  -D ENABLE_LIGHT_SLEEP=1
  -D DISABLE_WIFI_OTA=1
  -D PIN_STATUS_LED=RADIOLIB_NC
  -D DEBUG_SENSOR_READ=1
  ; Send sensor data to private channel
  -D SENSOR_CHANNEL_NAME='"SensorData"'
  -D SENSOR_CHANNEL_PSK='"your-128-bit-psk-base64"'
```

**Channel Configuration Details:**

- `SENSOR_CHANNEL_NAME`: Name of the channel (max 32 characters)
- `SENSOR_CHANNEL_PSK`: Pre-shared key (PSK) for the channel in base64 format (128-bit = 16 bytes = 24 base64 characters)
- If channel doesn't exist, it will be automatically created with the provided PSK
- Sensor data is sent as `PAYLOAD_TYPE_GRP_DATA` packets containing CayenneLPP telemetry

**Data Format:**

- Each packet contains: 4-byte timestamp + CayenneLPP telemetry data
- Data is encrypted with the channel's PSK
- All devices with the same channel name and PSK can receive the data

**Receiving Data:**

- Use MeshCore client app (Android/iOS/Web) to subscribe to the channel
- Or use another companion radio/repeater configured with the same channel
- Data will appear as group data messages on the channel

### Files

- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Light sleep implementation for ESP32-S3
- `src/helpers/radiolib/RadioLibWrappers.h/cpp` - RadioLib wrapper with interrupt reinitialization
- `examples/simple_repeater/main.cpp` - Light sleep integration with USB detection and optimized TX completion
- `examples/companion_radio/main.cpp` - Companion radio with light sleep and sensor reading

---

[← Back to Custom Features Index](custom_features.md)
