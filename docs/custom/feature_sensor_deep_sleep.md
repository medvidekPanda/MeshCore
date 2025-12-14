# Sensor with Deep Sleep and Group Channel Messaging

### Description

Added support for sensor firmware with deep sleep mode and group channel messaging. This implementation is based on companion_radio deep sleep functionality but adapted for sensor use case with encrypted group channel communication instead of direct messaging.

### Features

- **Deep Sleep Mode**: Ultra-low power consumption (< 1mA vs ~80mA for light sleep)
- **Sensor Reading**: Same sensor reading logic as companion_radio (SHT40 + BMP280)
- **Group Channel Messaging**: Messages sent to encrypted group channel via `PAYLOAD_TYPE_GRP_TXT`
- **Error Handling**: Sends error messages when sensors are not communicating
- **Channel Configuration**: Channel name and secret key configured via build flags
- **Message Format**: Messages include sender ID prefix (hex from first 4 bytes of public key)

### Build Variant

For SEEED Xiao S3 WIO board, the following build variant is available:

- `Xiao_S3_WIO_sensor_deep_sleep` - Sensor with deep sleep and group channel messaging

### Building and Uploading

To build and upload firmware:

```bash
pio run -e Xiao_S3_WIO_sensor_deep_sleep -t upload
```

To monitor Serial output:

```bash
pio device monitor -e Xiao_S3_WIO_sensor_deep_sleep
```

**Note**: In production mode (deep sleep enabled), device will enter deep sleep after sending data, so Serial output will be limited. For debugging, enable `MESH_DEBUG=1` or `SENSOR_DEBUG=1` to disable deep sleep and see continuous output.

### Configuration

#### Build Flags

In `platformio.private.ini` (not in git) you need to define:

```ini
[env:Xiao_S3_WIO_sensor_deep_sleep]
extends = env:Xiao_S3_WIO_sensor_deep_sleep_base
build_flags =
  ${env:Xiao_S3_WIO_sensor_deep_sleep_base.build_flags}
  ; Sensor channel configuration (private - must be set)
  ; Channel name (string)
  -D SENSOR_CHANNEL_NAME='"fogll-sensor-mesh"'
  ; Channel secret key (32 hex characters = 16 bytes)
  -D SENSOR_CHANNEL_SECRET='"ecb253bf5356ad6ed4512d0ab215f6f2"'
```

- `SENSOR_CHANNEL_NAME`: Name of the group channel (string)
- `SENSOR_CHANNEL_SECRET`: Secret key for channel encryption (32 hex characters = 16 bytes)

#### Base Configuration

The base profile `Xiao_S3_WIO_sensor_deep_sleep_base` includes:

- Deep sleep enabled (`ENABLE_DEEP_SLEEP=1`)
- WiFi and Bluetooth disabled (`DISABLE_WIFI_OTA=1`)
- Status LED disabled (`PIN_STATUS_LED=RADIOLIB_NC`)
- Sensors disabled (`DISABLE_SENSORS=1`) - I2C disabled to save power
- Sensor read interval: 30 minutes (1800 seconds) for production
- Debug mode: 60 seconds interval when `MESH_DEBUG=1` or `SENSOR_DEBUG=1` is enabled

### Operation Modes

#### Production Mode (Deep Sleep)

- Device wakes up every 30 minutes (1800 seconds)
- Reads sensors (SHT40 temperature/humidity, BMP280 pressure, battery voltage)
- Sends data to group channel
- Enters deep sleep (only timer wakeup, no radio wakeup)
- Power consumption: < 1mA in deep sleep

#### Debug Mode

When `MESH_DEBUG=1` or `SENSOR_DEBUG=1` is enabled:

- Deep sleep is automatically disabled
- Device stays awake for easier development
- Sensor reading interval: 60 seconds
- All logging enabled (`MESH_PACKET_LOGGING=1`, `MESH_DEBUG=1`)

### Sensor Data Format

#### Normal Operation (All Sensors OK)

Data is sent to group channel in format: `sender_id: timestamp,temperature,humidity,pressure,voltage`

Where `sender_id` is hex string from first 4 bytes of sensor's public key (8 characters).

Example:
```
A1B2C3D4: 1234567890,23.5,45.2,1013.25,3.850
```

#### Error Conditions

**Sensors Not Communicating:**
```
A1B2C3D4: Sensor communication error: sensors not responding (time: 1234567890)
```

**Partial Error (Some Sensors Missing):**
```
A1B2C3D4: Sensor partial error: SHT40/BMP280 not responding (time: 1234567890, voltage: 3.850V)
```

### CLI Commands

#### Companion ID Configuration (Legacy)

**Get Current Companion ID:**
```
get companion.id
```

**Set Companion ID:**
```
set companion.id <64_hex_characters>
```

Note: Companion ID is no longer used for message sending (messages are sent to group channel instead), but the CLI commands are kept for backward compatibility.

### Power Management

#### Deep Sleep Sequence

1. Read sensors and send data to group channel
2. Wait for message transmission (30 seconds)
3. Listen for time sync packets (ADVERT packets)
4. Power down I2C bus (SDA/SCL pins)
5. Power down ADC (battery measurement pin)
6. Power down LoRa radio (standby → sleep)
7. Enter deep sleep for `SENSOR_READ_INTERVAL_SECS` seconds

#### Power Consumption

- **Deep Sleep**: < 1mA (ESP32-S3 module only, dev board may have higher consumption due to power LED, voltage regulator, USB-Serial chip)
- **Active (sending)**: ~80-100mA during transmission
- **Wakeup**: Device resets on wakeup, `setup()` is called again

### Time Synchronization

#### First Startup (Not from Deep Sleep)

On first startup, device waits for ADVERT packet to synchronize time:
- Enters light sleep with radio in RX mode
- Waits up to 60 minutes for ADVERT packet with time sync
- **Non-ADVERT packets are silently ignored** (no processing, no logging)
- Only ADVERT packets with valid timestamp trigger time synchronization
- If no ADVERT received within 60 minutes, sends data with warning about unsynchronized time

#### After Deep Sleep Wakeup

After deep sleep wakeup:
- Checks if time needs update (older than 1 day)
- If time is OK, sends data immediately without waiting
- If time needs update, waits for ADVERT packet (same as first startup)

#### Time Sync Behavior

- **ADVERT packet received**: Time synchronized, RTC clock updated, continues to send data
- **Timeout (60 minutes)**: Sends warning message first, then sensor data with unsynchronized time
- **Non-ADVERT packets**: Silently ignored during wait period (minimal processing, no logging)

### Error Handling

#### Sensor Communication Errors

The firmware checks if sensors are communicating before sending data:

- **All sensors missing**: Sends error message instead of null values
- **Partial sensor failure**: Sends partial error message with available data (e.g., voltage)
- **All sensors OK**: Sends normal CSV format

This prevents sending misleading data (0.0 values) when sensors are disconnected or not responding.

### Channel Configuration

#### Channel Setup

- **Channel Name**: Configured via build flag `SENSOR_CHANNEL_NAME` (string)
- **Channel Secret**: Configured via build flag `SENSOR_CHANNEL_SECRET` (32 hex chars = 16 bytes)
- **Encryption**: Messages are encrypted using AES-128 with HMAC-SHA256 authentication
- **Routing**: Messages use flood routing (`sendFlood()`) to reach all channel members

#### Message Format

Messages sent to group channel follow format: `sender_id: message`

- `sender_id`: Hex string from first 4 bytes of sensor's public key (8 characters)
- `message`: Sensor data in CSV format or error message

This allows multiple sensors to send to the same channel while maintaining sender identification.

### Files

- `variants/xiao_s3_wio/platformio.ini` - Build profiles for sensor deep sleep
- `variants/xiao_s3_wio/platformio.private.ini.example` - Example configuration
- `examples/simple_sensor/main.cpp` - Sensor implementation with deep sleep and group channel messaging

### Implementation Details

#### Sensor Reading

Same logic as companion_radio:
- Uses `sensors.querySensors(0xFF, telemetry)` for all sensors
- Battery voltage via `board.getBattMilliVolts()`
- Data parsed using `LPPReader` to extract values

#### Message Sending

- Creates `mesh::GroupChannel` from channel secret key (hex string)
- Formats message as `sender_id: message` where sender_id is hex from first 4 bytes of public key
- Constructs `PAYLOAD_TYPE_GRP_TXT` packet
- Encrypts using channel secret key (AES-128 + HMAC-SHA256)
- Uses `sendFlood()` for routing (works across multiple hops)
- Includes timestamp for replay protection

#### Deep Sleep vs Debug Mode

- **Deep Sleep**: `ENABLE_DEEP_SLEEP=1` → device enters deep sleep after sending data
- **Debug Mode**: `MESH_DEBUG=1` or `SENSOR_DEBUG=1` → automatically disables deep sleep, device stays awake

### Status

**Status**: Implemented and tested

**Commits**:
- `c41adef9` - Add Xiao_S3_WIO_sensor_deep_sleep profile with group channel messaging
- `5cb21cbd` - Add sensor communication error handling

**Recent Changes**:
- Changed from direct Companion messaging to group channel messaging
- Messages now sent to encrypted group channel via `PAYLOAD_TYPE_GRP_TXT`
- Channel configuration via build flags (`SENSOR_CHANNEL_NAME`, `SENSOR_CHANNEL_SECRET`)
- Message format includes sender ID prefix for identification
- Optimized light sleep time sync wait - non-ADVERT packets silently ignored
- Improved time synchronization logic - only ADVERT packets trigger time sync
- Added timeout handling (60 minutes) with warning message if time sync fails

---

[← Back to Custom Features Index](custom_features.md)
