# WiFi and Bluetooth CLI Control

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

[← Back to Custom Features Index](custom_features.md)
