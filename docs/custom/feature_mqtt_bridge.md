# MQTT Bridge for Room Server

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

[← Back to Custom Features Index](custom_features.md)
