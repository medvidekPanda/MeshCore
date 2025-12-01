# Custom Features - MQTT Bridge and WiFi/BT Control

This document describes custom changes added to MeshCore firmware that are not part of the standard distribution.

## Contents

1. [MQTT Bridge for Room Server](#mqtt-bridge-for-room-server)
2. [WiFi and Bluetooth CLI Control](#wifi-and-bluetooth-cli-control)
3. [Light Sleep for Ultra-Low Power](#light-sleep-variant)
4. [Companion Radio with Light Sleep and Environmental Sensors](#companion-radio-with-light-sleep-and-environmental-sensors)
5. [Companion Radio with Deep Sleep for Ultra-Low Power](#companion-radio-with-deep-sleep-for-ultra-low-power)
6. [USB Serial Configuration (When Bluetooth is Disabled)](#usb-serial-configuration-when-bluetooth-is-disabled)

---

## MQTT Bridge for Room Server

### Description

Added support for MQTT bridge on Room Server firmware, which allows forwarding mesh packets over MQTT broker. This functionality enables integration of MeshCore network with external systems via MQTT protocol.

### Configuration

#### Build Variant

For SEEED Xiao S3 WIO board, the following build variant is available:

- `Xiao_S3_WIO_room_server_mqtt` - Room Server with MQTT bridge support

#### Build Flags

In `platformio.private.ini` (not in git) you need to define:

```ini
[env:Xiao_S3_WIO_room_server_mqtt]
build_flags =
  -D WIFI_SSID='"your_wifi_ssid"'
  -D WIFI_PWD='"your_wifi_password"'
  -D MQTT_BROKER='"your_mqtt_broker_ip"'
  -D MQTT_PORT=1883
  -D ADMIN_PASSWORD='"your_admin_password"'
  -D ROOM_PASSWORD='"your_room_password"'
  ; Optional authentication:
  ;  -D MQTT_USER='"username"'
  ;  -D MQTT_PASS='"password"'
  ; Optional: Second MQTT server (can also be configured via CLI):
  ;  -D MQTT_BROKER2='"your_second_mqtt_broker_ip"'
  ;  -D MQTT_PORT2=1883
  ;  -D MQTT_USER2='"username2"'
  ;  -D MQTT_PASS2='"password2"'
  ; Optional channel filtering:
  ;  -D MQTT_FILTER_CHANNELS='"0x01,0x02"'  ; Filter only specific channels
```

#### MQTT Topic Structure

- **Publish topic**: `{MQTT_TOPIC_PREFIX}tx` (default: `meshcore/tx`)
- **Subscribe topic**: `{MQTT_TOPIC_PREFIX}rx` (default: `meshcore/rx`)

#### Packet Format

MQTT payload contains:

1. Magic bytes: `0x4D, 0x43` ("MC")
2. 2-byte checksum (Fletcher16)
3. Mesh packet data (serialized packet)

### Channel Filtering

If `MQTT_FILTER_CHANNELS` is defined, the bridge will only forward packets from specified channels (for `GRP_TXT` and `GRP_DATA` types).

Example:

```ini
-D MQTT_FILTER_CHANNELS='"0x01,0x02"'
```

### Bridge Configuration via CLI

Room Server supports the following CLI commands for bridge configuration:

#### General Bridge Commands

- `get bridge.enabled` - shows if bridge is enabled
- `set bridge.enabled on/off` - enables/disables bridge
- `get bridge.source` - shows packet source (logTx/logRx)
- `set bridge.source tx/rx` - sets packet source
  - `tx` = forward transmitted packets
  - `rx` = forward received packets

#### MQTT Server Configuration (Dual Server Support)

Room Server supports **two MQTT servers** with runtime switching:

**Server Selection:**
- `get mqtt.server` - shows active server index (0 or 1)
- `set mqtt.server 0/1` - switches to server 0 (primary) or 1 (secondary)
  - Server 0: Primary server (from `MQTT_BROKER` and `MQTT_PORT` build flags)
  - Server 1: Secondary server (from prefs or `MQTT_BROKER2`/`MQTT_PORT2` build flags)

**Secondary Server Configuration:**
- `get mqtt.broker2` - shows IP/hostname of secondary server
- `set mqtt.broker2 <ip>` - sets IP/hostname for secondary server
- `get mqtt.port2` - shows port of secondary server
- `set mqtt.port2 <port>` - sets port for secondary server (1-65535)
- `set mqtt.user2 <username>` - sets username for secondary server (optional)
- `set mqtt.pass2 <password>` - sets password for secondary server (optional)

**Configuration Methods:**

1. **Via Build Flags** (in `platformio.private.ini`):
   ```ini
   -D MQTT_BROKER='"192.168.1.100"'
   -D MQTT_PORT=1883
   ; Optional second server:
   -D MQTT_BROKER2='"192.168.1.101"'
   -D MQTT_PORT2=1883
   -D MQTT_USER2='"user2"'
   -D MQTT_PASS2='"pass2"'
   ```

2. **Via CLI Commands** (runtime configuration):
   ```
   set mqtt.broker2 192.168.1.101
   set mqtt.port2 1883
   set mqtt.user2 myuser
   set mqtt.pass2 mypass
   set mqtt.server 1
   ```

**Notes:**
- Topic prefix (`meshcore/tx` and `meshcore/rx`) remains the same for both servers
- Switching servers disconnects from current server and reconnects to the new one
- Secondary server configuration is saved to device preferences (persistent)
- If secondary server is not configured, only server 0 is available

### Python Scripts

#### read.py - Reading MQTT Messages

Script for reading and decoding MQTT messages from broker:

```bash
venv/bin/python3 read.py [--broker IP] [--port PORT] [--secret HEX]
```

Features:

- Connect to MQTT broker
- Decode mesh packets
- Display human-readable information
- Support for decrypting encrypted messages (if secret is provided)

#### send.py - Sending MQTT Messages

Script for sending messages via MQTT into mesh network:

```bash
venv/bin/python3 send.py [--broker IP] [--port PORT]
```

Features:

- Send text messages
- Send telemetry data
- Send control packets

### Usage Example

1. **Upload firmware** with MQTT bridge support
2. **Configure WiFi and MQTT** in `platformio.private.ini`
3. **Run Python script** to read messages:
   ```bash
   venv/bin/python3 read.py --broker 10.40.196.82 --port 41883 --secret cd95890fe082b80c6f2c2cd06d6fdf9b
   ```
4. **Monitor** mesh packets in real-time

---

## WiFi and Bluetooth CLI Control

### Description

Added ability to enable/disable WiFi and Bluetooth via CLI commands. This functionality allows minimizing power consumption by disabling WiFi and BT when not needed, and enabling them via CLI when needed.

### Configuration

#### Build Variant

For SEEED Xiao S3 WIO board, the following build variant is available:

- `Xiao_S3_WIO_repeater_lowpower` - Repeater with WiFi and BT disabled by default

**Note**: WiFi and BT are compiled into firmware, but not initialized at startup if disabled. This allows enabling them via CLI without recompilation.

### CLI Commands

#### Display Status

```
get wifi
  -> on/off

get bt
  -> on/off
```

#### Enable/Disable

```
set wifi on
  -> OK - WiFi enabled (reboot to apply)

set wifi off
  -> OK - WiFi disabled (reboot to apply)

set bt on
  -> OK - BT enabled (reboot to apply)

set bt off
  -> OK - BT disabled (reboot to apply)
```

**Important**: Changes take effect after device reboot!

### Startup Behavior

At firmware startup, it displays WiFi and BT status:

```
WiFi disabled (use 'set wifi on' to enable)
BT disabled (use 'set bt on' to enable)
```

Or if enabled:

```
WiFi connected! IP: 192.168.1.100
BT enabled (not yet implemented for repeater)
```

### WiFi Initialization

WiFi is initialized only if:

1. `wifi_enabled = 1` in prefs
2. `WITH_MQTT_BRIDGE` is compiled
3. `WIFI_SSID` is defined in build flags

If these conditions are met, firmware will attempt to connect to WiFi network at startup.

### Bluetooth

Bluetooth is prepared for future implementation. Currently, only the state (`bt_enabled`) is saved to prefs, but BT is not initialized.

### Power Savings

Disabling WiFi and BT can significantly reduce power consumption:

- **WiFi disabled**: ~10-50mA savings (depends on mode)
- **BT disabled**: ~5-20mA savings

For maximum savings, use build variant `Xiao_S3_WIO_repeater_lowpower`, which has WiFi and BT disabled by default.

### Super Low Power Variant

For maximum power savings, use `Xiao_S3_WIO_repeater_super_lowpower`. This variant:

1.  Inherits from `lowpower` (WiFi/BT disabled).
2.  **Underclocks CPU to 80MHz** (standard is 240MHz).
3.  **Disables Status LED** blinking logic.

This configuration significantly reduces the active current consumption of the device.

```ini
[env:Xiao_S3_WIO_repeater_super_lowpower]
extends = env:Xiao_S3_WIO_repeater_lowpower
board_build.f_cpu = 80000000L
build_flags =
  ${env:Xiao_S3_WIO_repeater_lowpower.build_flags}
  -D PIN_STATUS_LED=RADIOLIB_NC
```

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

**Usage Example:**

1. **Upload firmware** with companion radio low sleep variant:

   ```bash
   pio run -e Xiao_S3_WIO_companion_radio_low_sleep -t upload --upload-port /dev/cu.usbmodem11301
   ```

2. **Monitor sensor readings** via Serial:

   ```
   [SENSOR] Reading sensors (interval: 60 secs)
     [CH1] Voltage: 1.150 V
     [CH1] Temperature: 27.20°C
     [CH1] Humidity: 46.50%
     [CH1] Pressure: 1013.25 hPa
     [CH1] Altitude: 150.00 m
   [SENSOR] Sensor reading complete
   ```

3. **For production use**, remove `DEBUG_SENSOR_READ` flag to use 30-minute interval:

   ```ini
   build_flags =
     ${Xiao_S3_WIO.build_flags}
     -D ENABLE_LIGHT_SLEEP=1
     -D DISABLE_WIFI_OTA=1
     -D PIN_STATUS_LED=RADIOLIB_NC
     ; 30-minute interval (default when DEBUG_SENSOR_READ is not defined)
     ; Note: BLE_PIN_CODE should NOT be defined for production to save power
   ```

4. **Enable Bluetooth in debug mode** (optional, for USB-powered development):

   When developing and testing with USB power, you can enable Bluetooth for easier configuration:

   ```ini
   build_flags =
     ${Xiao_S3_WIO.build_flags}
     -D ENABLE_LIGHT_SLEEP=1
     -D DISABLE_WIFI_OTA=1
     -D PIN_STATUS_LED=RADIOLIB_NC
     -D DEBUG_SENSOR_READ=1
     ; Enable Bluetooth for debug (device usually USB-powered)
     -D BLE_PIN_CODE=123456
   ```

   **Important:**

   - Bluetooth increases power consumption (~5-20mA)
   - In debug mode, device is usually USB-powered, so this is acceptable
   - For production/external battery use, **remove** `BLE_PIN_CODE` to disable BT and save power
   - Light sleep will still work, but BT will consume additional power

5. **Send sensor data to private channel** (optional):

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

   **Channel Configuration:**

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

### Companion Radio with Deep Sleep for Ultra-Low Power

For companion radio devices that need **ultra-low power consumption** (< 1mA vs ~80mA for light sleep), use `Xiao_S3_WIO_companion_radio_deep_sleep`. This variant:

1. **Implements deep sleep mode** - device enters deep sleep and wakes up only on timer (for sensor reading)
2. **WiFi and Bluetooth disabled** - `DISABLE_WIFI_OTA=1` and no `BLE_PIN_CODE` to save power
3. **Status LED disabled** - `PIN_STATUS_LED=RADIOLIB_NC` to minimize power consumption
4. **Periodic sensor reading** - automatically reads environmental sensors at configurable intervals (30 minutes default)
5. **Device reset on wakeup** - deep sleep causes full reset, wakeup reason is handled in `setup()`

**Key differences from light sleep:**

- **Much lower power consumption**: < 1mA in deep sleep vs ~80mA in light sleep
- **Radio is powered off** during deep sleep (cannot receive packets)
- **Device resets on wakeup** - all state is lost, wakeup reason is detected in `setup()`
- **Only timer wakeup** - device wakes up periodically for sensor reading, not on LoRa packets
- **Ideal for battery-powered sensors** that need to operate for months on battery

**Configuration:**

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

**Wakeup behavior:**

- Device wakes up from deep sleep after `SENSOR_READ_INTERVAL_SECS` (default: 1800 seconds = 30 minutes)
- On wakeup, device performs full reset
- `setup()` detects wakeup reason using `esp_reset_reason() == ESP_RST_DEEPSLEEP`
- Sensors are read immediately after wakeup
- Sensor data is sent to mesh channel (if configured)
- After sending, device waits 5 seconds (or 30 seconds on first startup) then enters deep sleep again

**Power consumption:**

- **Deep sleep**: 
  - ESP32-S3 module alone: ~8-20 µA (only RTC and wakeup timer active)
  - XIAO ESP32S3 development board: ~2-8 mA (due to power LED, voltage regulator, USB-Serial chip)
  - Note: Development board components (power LED, voltage regulator) cannot be disabled via software
- **Active mode** (sensor reading + transmission): ~50-100mA
- **Active duration**: ~5-30 seconds per cycle (depending on startup vs wakeup)
- **Average power**: ~0.1-0.5mA (depending on sensor reading interval)

**Usage Example:**

1. **Upload firmware** with companion radio deep sleep variant:

   ```bash
   pio run -e Xiao_S3_WIO_companion_radio_deep_sleep -t upload --upload-port /dev/cu.usbmodem11301
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

   See [Companion Radio with Light Sleep](#companion-radio-with-light-sleep-and-environmental-sensors) section for channel configuration details.

**Important notes:**

- **Deep sleep resets the device** - all RAM is lost, only RTC memory is preserved
- **Radio cannot receive packets** during deep sleep - device only wakes up on timer
- **First startup** (not from deep sleep): device waits 30 seconds before entering deep sleep
- **Wakeup from deep sleep**: device waits 5 seconds before entering deep sleep again
- **USB connection**: If USB is connected, device may not enter deep sleep (depends on board implementation)
- **Battery voltage**: Measured and included in sensor telemetry on each wakeup

### USB Serial Configuration (When Bluetooth is Disabled)

When Bluetooth is disabled (no `BLE_PIN_CODE` defined), companion radio automatically uses **USB Serial** for configuration and communication.

**How it works:**

1. **Automatic USB Serial Interface:**

   - If `BLE_PIN_CODE` is not defined, companion radio uses `ArduinoSerialInterface` with `Serial` (USB)
   - On ESP32-S3, `Serial` uses USB-Serial-JTAG, so USB connection works automatically
   - Baud rate: 115200

2. **Light Sleep Behavior:**

   - Device detects Serial activity (`Serial.available()`)
   - When Serial commands are received, device stays awake (resets `last_activity` timer)
   - This prevents light sleep while you're configuring the device via USB

3. **Configuration Methods:**

   **A. Via MeshCore Client Protocol:**

   - Connect via USB Serial (e.g., using `picocom`, `screen`, or Serial Monitor)
   - Use binary frame protocol (same as BLE/WiFi clients)
   - All standard companion radio commands are available

   **B. Via CLI Rescue Mode:**

   - Basic CLI commands available directly via Serial:
     - `set pin <6-digit-pin>` - Set BLE PIN (for future use)
     - `rebuild` - Format filesystem
     - `reboot` - Reboot device
     - `ls` - List files
     - `cat <path>` - Display file contents
     - `rm <path>` - Remove file

4. **Connecting via USB:**

   **Linux/macOS:**

   ```bash
   # Using picocom (recommended)
   picocom -b 115200 /dev/ttyUSB0 --imap lfcrlf

   # Or using screen
   screen /dev/ttyUSB0 115200

   # Or using cat (read-only)
   cat /dev/ttyUSB0
   ```

   **Windows:**

   - Use PuTTY, Tera Term, or Arduino Serial Monitor
   - Port: COM port (e.g., COM3)
   - Baud rate: 115200

5. **Example Usage:**

   ```bash
   # Connect to device
   picocom -b 115200 /dev/ttyUSB0 --imap lfcrlf

   # In CLI rescue mode, you can use:
   set pin 123456
   rebuild
   reboot
   ```

6. **Important Notes:**
   - USB Serial works even when `DISABLE_WIFI_OTA=1` is set
   - Device will stay awake while Serial commands are being sent
   - For production use, device will enter light sleep when no Serial activity is detected
   - Serial activity detection prevents light sleep, allowing configuration without interruption

### Usage Example

1. **Upload firmware** with super lowpower variant:

   ```bash
   pio run -e Xiao_S3_WIO_repeater_super_lowpower -t upload
   ```

2. **Check status**:

   ```
   get wifi
     -> off
   get bt
     -> off
   ```

3. **Enable WiFi** (when needed):

   ```
   set wifi on
   reboot
   ```

4. **Disable WiFi** (for power savings):

   ```
   set wifi off
   reboot
   ```

5. **Use light sleep variant** (for ultra-low power with fast wakeup):

   ```bash
   pio run -e Xiao_S3_WIO_repeater_low_sleep -t upload
   ```

   Device will automatically enter light sleep after 5 seconds of inactivity and wake up on incoming packets.

   **Battery monitoring**: Device will automatically wake up every 1 hour to measure battery voltage, even if no packets are received. This provides safety monitoring of battery health. Battery voltage is logged to Serial when measured.

   **Note**: If USB is connected, device will stay awake and not enter light sleep, allowing you to connect via USB from PC even after hard reset.

### Technical Details

#### NodePrefs Structure

New fields added to `NodePrefs`:

- `uint8_t wifi_enabled` - WiFi state (0 = disabled, 1 = enabled)
- `uint8_t bt_enabled` - BT state (0 = disabled, 1 = enabled)

Default values:

- `wifi_enabled = 0` (disabled)
- `bt_enabled = 0` (disabled)

#### Compatibility

- New fields are added at the end of `NodePrefs` structure
- Old configuration files will have these values as 0 (disabled)
- Compatible with existing firmware versions

---

## Related Files

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

---

## Build and Upload

### Building and Uploading Firmware

To build and upload firmware for Xiao S3 WIO variants, use PlatformIO:

```bash
cd /Users/janptacek/git-projects/MeshCore
~/.platformio/penv/bin/platformio run -e Xiao_S3_WIO_repeater_low_sleep -t upload
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

## Notes

- **MQTT Bridge**: Requires WiFi connection and functional MQTT broker
- **WiFi/BT CLI**: Changes require reboot to apply
- **Lowpower variant**: WiFi and BT are compiled but not initialized by default
- **Light Sleep**: USB connection detection keeps device awake for PC access
- **Battery Measurement**: Requires external voltage divider circuit on A0 pin (GPIO 1) - two 200kΩ resistors
- **Battery Safety Monitoring**: In `low_sleep` variant, battery voltage is measured every 1 hour automatically
- **Compatibility**: All changes are backward compatible with existing configurations

---

_Document created: 2025-11-22_
_Last updated: 2025-01-XX_
_Firmware version: battery-test branch (DEV)_

### Uncommitted changes: Battery Voltage Safety Monitoring and Light Sleep Fixes

**Status**: Implemented and tested

**Changes**:

- **Battery percentage calculation updated for load conditions** - Maximum voltage changed from 4200mV (4.2V no-load) to 4100mV (4.1V under load) to account for voltage sag during operation. LiPo batteries under load show ~4.1V even when fully charged, so this provides more accurate percentage readings.
- Added automatic battery voltage measurement every 1 hour (3600 seconds) in `low_sleep` variant
- Battery measurement happens on light sleep timeout wakeup (safety watchdog)
- Measurement uses A0 pin (GPIO 1) with 1/2 voltage divider as per Seeed Studio wiki
- 16-sample averaging to remove communication spikes
- Battery voltage is logged to Serial: `[BATT] Safety check (hourly): Battery: X.XXXV (XXXmV)`
- Corrected PIN_VBAT_READ to GPIO 1 (was incorrectly documented as GPIO 4)
- **Fixed USB detection bug** - USB detection was always returning true, preventing light sleep entry (device consumed 64mA instead of ~10mA)
- **Fixed packet loss after wakeup** - packet that wakes device from light sleep is now processed immediately before re-initialization, preventing trace route timeouts

**Implementation details:**

- Light sleep timeout set to 3600 seconds (1 hour) for safety watchdog
- Battery measurement happens only on timeout wakeup, not on packet wakeup
- Provides battery health monitoring even when no packets are received
- Prevents device from sleeping forever if radio fails (safety watchdog function)
- USB detection disabled (was causing device to never enter light sleep)
- Wakeup packet processing: packet that triggers wakeup (DIO1 interrupt) is processed immediately via `the_mesh.loop()` before interrupt re-initialization
- Radio re-initialization happens AFTER processing wakeup packet to prevent packet loss

**Power consumption fixes:**

- Fixed bug where faulty USB detection (`Serial.availableForWrite() >= 0` always true) prevented light sleep
- Device now correctly enters light sleep and consumes ~10-15mA instead of ~64mA
- Fast wakeup processing ensures packets are not lost during wakeup sequence

**Trace route fixes:**

- Wakeup packet is processed immediately to prevent loss
- Radio state properly managed after wakeup for trace route responses
- Minimal delays (50ms) for fast response after wakeup

**Files**:

- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Battery voltage measurement implementation (GPIO 1)
- `variants/xiao_s3_wio/platformio.ini` - PIN_VBAT_READ=1 configuration
- `examples/simple_repeater/main.cpp` - Light sleep with 1-hour timeout, immediate packet processing, USB detection fix
- `examples/companion_radio/ui-orig/UITask.cpp` - Battery percentage calculation updated to 4100mV max (under load)
- `examples/companion_radio/ui-new/UITask.cpp` - Battery percentage calculation updated to 4100mV max (under load)

picocom -b 115200 --imap lfcrlf /dev/cu.usbmodem11301
cat /dev/cu.usbmodem11301