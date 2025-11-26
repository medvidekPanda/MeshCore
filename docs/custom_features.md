# Custom Features - MQTT Bridge and WiFi/BT Control

This document describes custom changes added to MeshCore firmware that are not part of the standard distribution.

## Contents

1. [MQTT Bridge for Room Server](#mqtt-bridge-for-room-server)
2. [WiFi and Bluetooth CLI Control](#wifi-and-bluetooth-cli-control)
3. [Light Sleep for Ultra-Low Power](#light-sleep-variant)

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
  ; Optional:
  ;  -D MQTT_USER='"username"'
  ;  -D MQTT_PASS='"password"'
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

- `get bridge.enabled` - shows if bridge is enabled
- `set bridge.enabled on/off` - enables/disables bridge
- `get bridge.source` - shows packet source (logTx/logRx)
- `set bridge.source tx/rx` - sets packet source
  - `tx` = forward transmitted packets
  - `rx` = forward received packets

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

**Key features:**
- Device enters light sleep after 5 seconds of inactivity
- Automatically wakes up on incoming LoRa packet
- Radio stays in RX mode during sleep (unlike deep sleep)
- Interrupt handler is properly managed to prevent conflicts
- Minimal power consumption while maintaining fast response
- **USB detection** - if USB is connected, device stays awake to allow PC access
- **Extended awake time** - after hard reset with USB connected, device stays awake for 2 minutes

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

**Note**: If `LIGHT_SLEEP_TIMEOUT` is not defined, device will sleep indefinitely until a packet is received.

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

**Power savings**:
- Light sleep: ~5-15mA (vs ~50-100mA active)
- Radio stays in RX mode during sleep
- Fast response time compared to deep sleep

**USB detection**:
- Automatically detects USB connection using `Serial.availableForWrite()`
- If USB is connected, device never enters light sleep
- After hard reset with USB connected, device stays awake for 2 minutes
- Allows PC connection via USB even after power loss

**Files**:
- `variants/xiao_s3_wio/XiaoS3WIOBoard.h` - Light sleep implementation
- `src/helpers/radiolib/RadioLibWrappers.h/cpp` - Interrupt reinitialization
- `examples/simple_repeater/main.cpp` - Light sleep integration with USB detection
- `variants/xiao_s3_wio/platformio.ini` - Light sleep variant

---

## Notes

- **MQTT Bridge**: Requires WiFi connection and functional MQTT broker
- **WiFi/BT CLI**: Changes require reboot to apply
- **Lowpower variant**: WiFi and BT are compiled but not initialized by default
- **Light Sleep**: USB connection detection keeps device awake for PC access
- **Compatibility**: All changes are backward compatible with existing configurations

---

*Document created: 2025-11-22*
*Last updated: 2025-01-XX*
*Firmware version: add-mqtt branch (DEV)*
