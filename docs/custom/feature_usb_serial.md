# USB Serial Configuration (When Bluetooth is Disabled)

### Description

When Bluetooth is disabled (no `BLE_PIN_CODE` defined), companion radio automatically uses **USB Serial** for configuration and communication.

### How It Works

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

---

[← Back to Custom Features Index](custom_features.md)
