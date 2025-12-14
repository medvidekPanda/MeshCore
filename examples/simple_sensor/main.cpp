#include "SensorMesh.h"
#include <helpers/sensors/LPPDataHelpers.h>
#include <math.h>      // for isnan()
#include <Wire.h>

#ifdef ESP_PLATFORM
  #include <driver/gpio.h>
  #include <esp_system.h>  // for esp_reset_reason()
  #ifndef ESP32S3
    #include <driver/rtc_io.h>
  #endif
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

// Debug mode: When MESH_DEBUG is defined, disable sleep modes
#ifdef MESH_DEBUG
  #undef ENABLE_DEEP_SLEEP   // Disable deep sleep in debug mode
#endif

// Sensor debug mode: When SENSOR_DEBUG is defined, disable deep sleep
#ifdef SENSOR_DEBUG
  #undef ENABLE_DEEP_SLEEP   // Disable deep sleep in sensor debug mode
  #undef SENSOR_READ_INTERVAL_SECS  // Override any existing value
  #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for sensor debug mode
#endif

// Debug mode: When MESH_DEBUG is defined, also set interval to 60 seconds for debug
#ifdef MESH_DEBUG
  #ifndef SENSOR_DEBUG  // Only override if SENSOR_DEBUG didn't already set it
    #undef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for debug mode
  #endif
#endif

#ifndef SENSOR_CHANNEL_SECRET
  #error "SENSOR_CHANNEL_SECRET must be defined in platformio.ini or platformio.private.ini"
#endif

#ifndef SENSOR_CHANNEL_NAME
  #error "SENSOR_CHANNEL_NAME must be defined in platformio.ini or platformio.private.ini"
#endif

#ifndef COMPANION_ID
  #define COMPANION_ID "0000000000000000000000000000000000000000000000000000000000000000"
#endif

class MyMesh : public SensorMesh {
public:
  MyMesh(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables), 
       battery_data(12*24, 5*60),    // 24 hours worth of battery data, every 5 minutes
       last_room_msg_time(0),
       _companion_fs(nullptr)
  {
    // Initialize with default from build flag
    strncpy(companion_id, COMPANION_ID, sizeof(companion_id) - 1);
    companion_id[sizeof(companion_id) - 1] = '\0';
  }

  // Validate companion ID format (64 hex characters)
  bool validateCompanionId(const char* id) {
    if (!id) return false;
    int len = strlen(id);
    if (len != 64) return false;
    
    // Check all characters are valid hex
    for (int i = 0; i < len; i++) {
      char c = id[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        return false;
      }
    }
    return true;
  }

  // Load companion ID from filesystem (if exists and valid, otherwise use default from build flag)
  void loadCompanionId(FILESYSTEM* fs) {
    const char* filename = "/companion_id";
    if (fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
      File file = fs->open(filename, "r");
#else
      File file = fs->open(filename);
#endif
      if (file) {
        char loaded_id[65];
        int len = file.readBytes(loaded_id, sizeof(loaded_id) - 1);
        loaded_id[len] = '\0';
        file.close();
        
        // Validate loaded value - if invalid (corrupted or power loss during write), use default
        if (validateCompanionId(loaded_id)) {
          strncpy(companion_id, loaded_id, sizeof(companion_id) - 1);
          companion_id[sizeof(companion_id) - 1] = '\0';
          Serial.print("[LOG] Companion ID načteno ze souboru: ");
          Serial.println(companion_id);
          Serial.print("[LOG] Companion ID délka: ");
          Serial.println(strlen(companion_id));
          return;
        } else {
          Serial.print("[LOG] Varování: Companion ID v souboru je neplatné (možná poškozeno při výpadku napájení): ");
          Serial.println(loaded_id);
          Serial.println("[LOG] Použije se default z build flagu");
        }
      }
    }
    // Use default from build flag (either file doesn't exist or loaded value was invalid)
    strncpy(companion_id, COMPANION_ID, sizeof(companion_id) - 1);
    companion_id[sizeof(companion_id) - 1] = '\0';
    Serial.print("[LOG] Companion ID z build flagu: ");
    Serial.println(companion_id);
  }

  // Save companion ID to filesystem (permanent storage - survives reboots and power cycles)
  void saveCompanionId(FILESYSTEM* fs) {
    const char* filename = "/companion_id";
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    fs->remove(filename);
    File file = fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = fs->open(filename, "w");
#else
    File file = fs->open(filename, "w", true);
#endif
    if (file) {
      size_t written = file.write((const uint8_t*)companion_id, strlen(companion_id));
      file.flush();  // Ensure data is written to storage (important for power loss protection)
      file.close();
      if (written == strlen(companion_id)) {
        Serial.print("[LOG] Companion ID trvale uloženo do souboru: ");
        Serial.println(companion_id);
        Serial.println("[LOG] Tato hodnota bude použita při každém startu zařízení");
      } else {
        Serial.println("[LOG] Varování: Companion ID se nepodařilo zapsat kompletně");
      }
    } else {
      Serial.println("[LOG] Chyba: Nelze otevřít soubor pro zápis companion ID");
    }
  }

  // Public method to send sensor data to companion (called from setup())
  void sendSensorDataToCompanionPublic(CayenneLPP& telemetry) {
    sendSensorDataToCompanion(telemetry);
  }

  // Public method to send warning message to channel (called from setup())
  void sendWarningToCompanionPublic(const char* message) {
    sendMessageToChannel(message);
  }

  // Check if time needs update (older than 1 day) - public method
  bool needsTimeUpdate(FILESYSTEM* fs) {
    uint32_t last_update = loadLastTimeUpdate(fs);
    uint32_t current_time = getRTCClock()->getCurrentTime();
    
    // Minimum reasonable timestamp: January 1, 2024 (1704067200)
    // If current time is below this, it's likely uninitialized or wrong
    const uint32_t MIN_REASONABLE_TIME = 1704067200;  // 2024-01-01 00:00:00 UTC
    
    if (last_update == 0) {
      // Never updated - check if current time is reasonable
      if (current_time < MIN_REASONABLE_TIME) {
        Serial.print("[LOG] Time needs update: never updated and current time is too low (");
        Serial.print(current_time);
        Serial.println(")");
        return true;
      }
      // If current time seems reasonable but never saved, still update to save it
      Serial.println("[LOG] Time needs update: never updated (will save current time)");
      return true;
    }
    
    // Check if current time is unreasonably low (likely uninitialized after power loss)
    if (current_time < MIN_REASONABLE_TIME) {
      Serial.print("[LOG] Time needs update: current time is too low (");
      Serial.print(current_time);
      Serial.println(") - likely uninitialized after power loss");
      return true;
    }
    
    const uint32_t ONE_DAY_SECS = 24 * 60 * 60;  // 86400 seconds
    
    // Check if difference is at least 1 day (in either direction)
    uint32_t diff = (current_time > last_update) ? (current_time - last_update) : (last_update - current_time);
    
    if (diff >= ONE_DAY_SECS) {
      Serial.print("[LOG] Time needs update: last_update=");
      Serial.print(last_update);
      Serial.print(", current_time=");
      Serial.print(current_time);
      Serial.print(", diff=");
      Serial.println(diff);
      return true;
    }
    
    return false;
  }

  // Public flag to signal time update from advert (used in setup())
  bool _time_updated_from_advert = false;

  // Flag to ignore all non-ADVERT packets (used during time sync wait)
  bool _ignore_non_advert_packets = false;

  // Time sync notification info (to send after ADVERT processing is complete)
  bool _send_time_sync_notification = false;
  char _time_sync_repeater_id[17];
  uint32_t _time_sync_old_time;
  uint32_t _time_sync_new_time;

  // Set filesystem for companion ID operations
  void setFilesystem(FILESYSTEM* fs) {
    _companion_fs = fs;
  }

  // Override onRecvPacket to ignore non-ADVERT packets when flag is set
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override {
    // If flag is set, ignore all packets except ADVERT
    if (_ignore_non_advert_packets && pkt->getPayloadType() != PAYLOAD_TYPE_ADVERT) {
      // Ignore this packet - don't process it
      return ACTION_RELEASE;
    }
    
    // Normal processing for ADVERT packets or when flag is not set
    return SensorMesh::onRecvPacket(pkt);
  }

protected:
  /* ========================== custom logic here ========================== */
  Trigger low_batt, critical_batt;
  TimeSeriesData  battery_data;
  uint32_t last_room_msg_time;
  char companion_id[65];  // 64 hex chars + null terminator

  // Create GroupChannel from hex secret key
  mesh::GroupChannel createChannelFromHexSecret(const char* hex_secret) {
    mesh::GroupChannel channel;
    
    // Initialize secret to zeros (32 bytes total, but we only use first 16)
    memset(channel.secret, 0, sizeof(channel.secret));
    
    // Convert hex string to binary (32 hex chars = 16 bytes)
    if (!mesh::Utils::fromHex(channel.secret, 16, hex_secret)) {
      Serial.println("[LOG] Chyba: neplatný hex secret key");
      return channel;  // Return with zeroed secret
    }
    
    // Create hash from secret (SHA256, use first byte as hash)
    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, 16);
    
    return channel;
  }

  void sendSensorDataToCompanion(CayenneLPP& telemetry) {
    Serial.println("[LOG] sendSensorDataToCompanion: začátek");
    
    // Parse telemetry data to extract values (same as companion_radio)
    LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
    uint8_t channel_id, type;
    float temp_sht40 = NAN, humidity_sht40 = NAN, pressure_bmp280 = NAN, voltage = NAN;
    bool found_sht40_temp = false, found_sht40_humidity = false;
    
    while (reader.readHeader(channel_id, type)) {
      switch (type) {
        case LPP_TEMPERATURE: {
          float temp_val;
          if (reader.readTemperature(temp_val)) {
            if (!found_sht40_temp) {
              temp_sht40 = temp_val;
              found_sht40_temp = true;
            }
          }
          break;
        }
        case LPP_RELATIVE_HUMIDITY: {
          float hum_val;
          if (reader.readRelativeHumidity(hum_val)) {
            if (!found_sht40_humidity) {
              humidity_sht40 = hum_val;
              found_sht40_humidity = true;
            }
          }
          break;
        }
        case LPP_BAROMETRIC_PRESSURE: {
          float press_val;
          if (reader.readPressure(press_val)) {
            pressure_bmp280 = press_val;
          }
          break;
        }
        case LPP_VOLTAGE: {
          float volt_val;
          if (reader.readVoltage(volt_val)) {
            if (isnan(voltage)) {
              voltage = volt_val;
            }
          }
          break;
        }
        default:
          reader.skipData(type);
          break;
      }
    }
    
    // Check if sensors are communicating
    // If all sensor values are NAN (except voltage which should always be present), send error message
    bool sensors_ok = !(isnan(temp_sht40) && isnan(humidity_sht40) && isnan(pressure_bmp280));
    
    char text_data[128];
    int len;
    
    if (!sensors_ok && telemetry.getSize() <= 4) {
      // No sensor data at all - sensors not communicating
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      len = snprintf(text_data, sizeof(text_data), "Sensor communication error: sensors not responding (time: %u)", timestamp);
      Serial.println("[LOG] Chyba: senzory nekomunikují");
    } else if (!sensors_ok) {
      // Some sensors missing but we have some data (at least voltage)
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      len = snprintf(text_data, sizeof(text_data), "Sensor partial error: SHT40/BMP280 not responding (time: %u, voltage: %.3fV)", 
        timestamp, isnan(voltage) ? 0.0f : voltage);
      Serial.println("[LOG] Varování: některé senzory nekomunikují");
    } else {
      // All sensors OK - send normal CSV format
      uint32_t timestamp = getRTCClock()->getCurrentTime();
      len = snprintf(text_data, sizeof(text_data), "%u,%.1f,%.1f,%.1f,%.3f",
        timestamp,
        isnan(temp_sht40) ? 0.0f : temp_sht40,
        isnan(humidity_sht40) ? 0.0f : humidity_sht40,
        isnan(pressure_bmp280) ? 0.0f : pressure_bmp280,
        isnan(voltage) ? 0.0f : voltage
      );
    }
    
    Serial.print("[LOG] Parsed values: temp=");
    Serial.print(temp_sht40);
    Serial.print(", humidity=");
    Serial.print(humidity_sht40);
    Serial.print(", pressure=");
    Serial.print(pressure_bmp280);
    Serial.print(", voltage=");
    Serial.println(voltage);
    Serial.print("[LOG] Formatted data: ");
    Serial.println(text_data);
    
    if (len >= (int)sizeof(text_data)) {
      Serial.println("[LOG] Chyba: text data příliš dlouhé");
      return;
    }
    
    // Odeslat do kanálu místo na companion
    sendMessageToChannel(text_data);
  }

  void sendMessageToChannel(const char* message) {
    Serial.println("[LOG] sendMessageToChannel: začátek");
    
    // Channel secret key from build flag (32 hex chars = 16 bytes)
    const char* channel_secret_hex = SENSOR_CHANNEL_SECRET;
    
    // Create GroupChannel from hex secret
    mesh::GroupChannel channel = createChannelFromHexSecret(channel_secret_hex);
    
    Serial.print("[LOG] Channel secret (hex): ");
    Serial.println(channel_secret_hex);
    Serial.print("[LOG] Channel hash: ");
    mesh::Utils::printHex(Serial, channel.hash, sizeof(channel.hash));
    Serial.println();
    
    // Get sender name (use first 4 bytes of public key as hex string)
    char sender_name[10];
    mesh::Utils::toHex(sender_name, self_id.pub_key, 4);
    sender_name[8] = '\0';
    
    Serial.print("[LOG] Sender name: ");
    Serial.println(sender_name);
    
    // Vytvořit zprávu ve formátu "sender_name: message"
    int message_len = strlen(message);
    int sender_name_len = strlen(sender_name);
    int total_text_len = sender_name_len + 2 + message_len;  // "sender: message"
    
    if (total_text_len > MAX_PACKET_PAYLOAD - 5) {
      Serial.println("[LOG] Chyba: zpráva je příliš dlouhá");
      return;
    }
    
    uint8_t data[MAX_PACKET_PAYLOAD];
    uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
    uint32_t current_time = getRTCClock()->getCurrentTime();
    Serial.print("[LOG] Aktuální čas: ");
    Serial.print(current_time);
    Serial.print(", unique timestamp: ");
    Serial.println(timestamp);
    memcpy(data, &timestamp, 4);
    data[4] = 0;  // TXT_TYPE_PLAIN
    
    // Format: "sender_name: message"
    sprintf((char *)&data[5], "%s: %s", sender_name, message);
    int actual_len = strlen((char *)&data[5]);
    
    Serial.print("[LOG] Formátovaná zpráva: ");
    Serial.println((char *)&data[5]);
    Serial.print("[LOG] Vytváření group datagramu, délka: ");
    Serial.println(5 + actual_len);
    
    // Vytvořit group packet
    mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, data, 5 + actual_len);
    if (pkt == NULL) {
      Serial.println("[LOG] Chyba: nelze vytvořit packet");
      return;
    }
    
    Serial.println("[LOG] Packet vytvořen, odesílání flood...");
    
    // Odeslat jako flood (funguje i přes více hopů)
    sendFlood(pkt);
    
    Serial.print("[LOG] Zpráva odeslána do kanálu ");
    Serial.println(SENSOR_CHANNEL_NAME);
    last_room_msg_time = getRTCClock()->getCurrentTime();
  }

  void onSensorDataRead() override {
    // Just record battery data and check alerts (same as companion_radio)
    // Sending to companion is handled in loop() for debug mode and setup() for deep sleep mode
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);
    Serial.print("[LOG] onSensorDataRead: napětí baterie = ");
    Serial.println(batt_voltage);

    battery_data.recordData(getRTCClock(), batt_voltage);   // record battery
    alertIf(batt_voltage < 3.4f, critical_batt, HIGH_PRI_ALERT, "Battery is critical!");
    alertIf(batt_voltage < 3.6f, low_batt, LOW_PRI_ALERT, "Battery is low");
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago, MinMaxAvg dest[], int max_num) override {
    battery_data.calcMinMaxAvg(getRTCClock(), start_secs_ago, end_secs_ago, &dest[0], TELEM_CHANNEL_SELF, LPP_VOLTAGE);
    return 1;
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    // Handle "set companion.id <hex_string>" command
    if (memcmp(command, "set companion.id ", 18) == 0) {
      const char* new_id = &command[18];
      int len = strlen(new_id);
      
      // Validate hex string length (64 hex chars for 32-byte public key)
      if (len != 64) {
        strcpy(reply, "Error: Companion ID must be 64 hex characters");
        return true;
      }
      
      // Validate hex characters
      for (int i = 0; i < len; i++) {
        char c = new_id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
          strcpy(reply, "Error: Invalid hex character in companion ID");
          return true;
        }
      }
      
      // Copy new ID
      strncpy(companion_id, new_id, sizeof(companion_id) - 1);
      companion_id[sizeof(companion_id) - 1] = '\0';
      
      // Save to filesystem permanently - this value will persist across reboots and power cycles
      if (_companion_fs) {
        saveCompanionId(_companion_fs);
        // Verify it was saved correctly by reading it back
        char verify_id[65];
        bool saved_ok = false;
        const char* filename = "/companion_id";
        if (_companion_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
          File file = _companion_fs->open(filename, "r");
#else
          File file = _companion_fs->open(filename);
#endif
          if (file) {
            int len = file.readBytes(verify_id, sizeof(verify_id) - 1);
            verify_id[len] = '\0';
            file.close();
            if (strcmp(verify_id, companion_id) == 0) {
              saved_ok = true;
            }
          }
        }
        
        if (saved_ok) {
          sprintf(reply, "OK - Companion ID permanently saved: %s", companion_id);
        } else {
          sprintf(reply, "Warning - Companion ID set but verification failed: %s", companion_id);
        }
      } else {
        strcpy(reply, "Error: Filesystem not available");
      }
      return true;
    }
    
    // Handle "get companion.id" command
    if (strcmp(command, "get companion.id") == 0) {
      // Check if value is from file or build flag
      const char* filename = "/companion_id";
      bool from_file = false;
      if (_companion_fs && _companion_fs->exists(filename)) {
        from_file = true;
      }
      
      if (from_file) {
        sprintf(reply, "Companion ID (from file): %s", companion_id);
      } else {
        sprintf(reply, "Companion ID (from build flag): %s", companion_id);
      }
      return true;
    }
    
    if (strcmp(command, "magic") == 0) {    // example 'custom' command handling
      strcpy(reply, "**Magic now done**");
      return true;   // handled
    }
    return false;  // not handled
  }

  // Load last time update timestamp from filesystem
  uint32_t loadLastTimeUpdate(FILESYSTEM* fs) {
    const char* filename = "/last_time_update";
    if (fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
      File file = fs->open(filename, "r");
#else
      File file = fs->open(filename);
#endif
      if (file) {
        uint32_t timestamp = 0;
        if (file.readBytes((char*)&timestamp, sizeof(timestamp)) == sizeof(timestamp)) {
          file.close();
          Serial.print("[LOG] Last time update loaded: ");
          Serial.println(timestamp);
          return timestamp;
        }
        file.close();
      }
    }
    Serial.println("[LOG] No previous time update found");
    return 0;  // Never updated
  }

  // Save last time update timestamp to filesystem
  void saveLastTimeUpdate(FILESYSTEM* fs, uint32_t timestamp) {
    const char* filename = "/last_time_update";
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    fs->remove(filename);
    File file = fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = fs->open(filename, "w");
#else
    File file = fs->open(filename, "w", true);
#endif
    if (file) {
      size_t written = file.write((const uint8_t*)&timestamp, sizeof(timestamp));
      file.flush();
      file.close();
      if (written == sizeof(timestamp)) {
        Serial.print("[LOG] Last time update saved: ");
        Serial.println(timestamp);
      } else {
        Serial.println("[LOG] Warning: Failed to write last time update completely");
      }
    } else {
      Serial.println("[LOG] Error: Cannot open file for writing last time update");
    }
  }

  // Override onAdvertRecv to detect time updates
  // IMPORTANT: This is ONLY called for PAYLOAD_TYPE_ADVERT packets, not for other packet types
  // Other packet types (like REQ for telemetry) do NOT update time - they use sender's timestamp
  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override {
    // Call parent method first
    SensorMesh::onAdvertRecv(packet, id, timestamp, app_data, app_data_len);
    
    Serial.println("[LOG] ============================================");
    Serial.println("[LOG] onAdvertRecv called - ADVERT PAKET PŘIJAT!");
    Serial.print("[LOG] Timestamp z advertu: ");
    Serial.println(timestamp);
    Serial.print("[LOG] Sender ID (first 8 bytes): ");
    for (int i = 0; i < 8 && i < PUB_KEY_SIZE; i++) {
      if (id.pub_key[i] < 0x10) Serial.print("0");
      Serial.print(id.pub_key[i], HEX);
    }
    Serial.println();
    Serial.println("[LOG] ============================================");
    
    // VŽDY věříme repeateru - má správný čas!
    // Pouze základní kontrola, že timestamp není úplně nesmyslný (před rokem 2020 nebo po roce 2100)
    const uint32_t MIN_REASONABLE_TIME = 1577836800;  // 2020-01-01 00:00:00 UTC - velmi široký rozsah
    const uint32_t MAX_REASONABLE_TIME = 4102444800;  // 2100-01-01 00:00:00 UTC - velmi široký rozsah
    
    uint32_t curr_time = getRTCClock()->getCurrentTime();
    
    // Pouze základní kontrola, že timestamp není úplně nesmyslný
    if (timestamp < MIN_REASONABLE_TIME) {
      Serial.print("[LOG] Advert timestamp příliš starý (");
      Serial.print(timestamp);
      Serial.print(", min: ");
      Serial.print(MIN_REASONABLE_TIME);
      Serial.println(") - ignorujeme (možná chyba v repeateru)");
      return;
    }
    
    if (timestamp > MAX_REASONABLE_TIME) {
      Serial.print("[LOG] Advert timestamp příliš daleko v budoucnosti (");
      Serial.print(timestamp);
      Serial.print(", max: ");
      Serial.print(MAX_REASONABLE_TIME);
      Serial.println(") - ignorujeme (možná chyba v repeateru)");
      return;
    }
    
    // VŽDY přijmeme timestamp z repeateru, pokud prošel základní kontrolou
    // Repeater má správný čas, takže mu věříme!
    bool should_update = true;
    
    if (should_update) {
      // Time was synchronized from advert
      Serial.print("[LOG] Time synchronized from advert: ");
      Serial.print(curr_time);
      Serial.print(" -> ");
      Serial.println(timestamp);
      
      // CRITICAL: Actually update the RTC clock with the new timestamp FIRST
      // This is what makes the time sync work - without this, the clock doesn't actually change!
      // This must be done BEFORE saving to filesystem, so the clock is updated immediately
      uint32_t new_time = timestamp + 1;  // Set time to timestamp + 1 second (same as companion_radio)
      getRTCClock()->setCurrentTime(new_time);
      Serial.print("[LOG] RTC clock updated to: ");
      Serial.println(getRTCClock()->getCurrentTime());
      
      // Signal that time was updated (must be set before filesystem operations)
      _time_updated_from_advert = true;
      
      // Save timestamp of this update (if filesystem is available)
      // IMPORTANT: Save the ACTUAL time that was set in RTC (timestamp + 1), not the original timestamp
      // This ensures consistency between RTC clock and saved timestamp
      if (_companion_fs) {
        Serial.print("[LOG] Saving time update to filesystem: ");
        Serial.print("old_time=");
        Serial.print(curr_time);
        Serial.print(", new_time=");
        Serial.print(new_time);
        Serial.print(", timestamp=");
        Serial.println(timestamp);
        saveLastTimeUpdate(_companion_fs, new_time);  // Save timestamp + 1 to match RTC clock
        
        // Verify it was saved correctly
        uint32_t saved_time = loadLastTimeUpdate(_companion_fs);
        if (saved_time == new_time) {
          Serial.println("[LOG] ✓ Time update saved and verified correctly");
        } else {
          Serial.print("[LOG] ✗ ERROR: Time update verification failed! Saved: ");
          Serial.print(saved_time);
          Serial.print(", expected: ");
          Serial.println(new_time);
        }
        
        // Format repeater identifier (first 8 bytes of public key = 16 hex chars)
        // Store info for later notification (will be sent AFTER onAdvertRecv completes)
        for (int i = 0; i < 8; i++) {
          sprintf(&_time_sync_repeater_id[i * 2], "%02x", id.pub_key[i]);
        }
        _time_sync_repeater_id[16] = '\0';
        _time_sync_old_time = curr_time;
        _time_sync_new_time = timestamp;
        _send_time_sync_notification = true;
        
        Serial.print("[LOG] Time sync info stored for notification (from repeater: ");
        Serial.print(_time_sync_repeater_id);
        Serial.println(")");
      } else {
        Serial.println("[LOG] WARNING: Filesystem not available - time update NOT saved!");
      }
    }
  }
  
private:
  FILESYSTEM* _companion_fs;
  /* ======================================================================= */
};

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();
  
  Serial.println("[LOG] Setup: inicializace dokončena");

  command[0] = 0;

  sensors.begin();
  Serial.println("[LOG] Sensors inicializovány");

  the_mesh.setFilesystem(fs);
  the_mesh.begin(fs);
  the_mesh.loadCompanionId(fs);
  Serial.println("[LOG] Mesh inicializován");

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#ifndef ENABLE_DEEP_SLEEP
  // send out initial Advertisement to the mesh (only in non-deep-sleep mode)
  // In deep sleep mode, sensor only listens for ADVERT packets, doesn't send them
  the_mesh.sendSelfAdvertisement(16000);
#endif

#ifdef ENABLE_DEEP_SLEEP
  {
  // Deep sleep mode: Sensor-only mode - only wakes up on timer, not on LoRa packets
  // Sensor read interval: 30 minutes (1800 seconds) for production
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 1800  // 30 minutes for production
  #endif
  
  // Check if this is first startup (not from deep sleep)
  #ifdef ESP32
    esp_reset_reason_t reason = esp_reset_reason();
    bool is_first_startup = (reason != ESP_RST_DEEPSLEEP);
    Serial.print("[LOG] Reset reason: ");
    Serial.print(reason);
    Serial.print(" (ESP_RST_DEEPSLEEP=");
    Serial.print(ESP_RST_DEEPSLEEP);
    Serial.print("), is_first_startup=");
    Serial.println(is_first_startup);
  #else
    bool is_first_startup = true;  // For non-ESP32 platforms, assume first startup
    Serial.println("[LOG] Non-ESP32 platform, assuming first startup");
  #endif
  
  // On first startup, always wait for advert packet BEFORE sending data (light sleep)
  // After deep sleep wakeup, check if time needs update and wait if necessary
  bool wait_for_advert = false;
  if (is_first_startup) {
    // Always wait for advert on first startup - don't send data until we get time sync
    wait_for_advert = true;
    Serial.println("[LOG] První start - čekání na advert paket (light sleep) před odesláním dat");
    Serial.println("[LOG] Nebudeme posílat data, dokud nepřijde advert paket");
  } else {
    // After deep sleep wakeup, check if time needs update
    bool needs_update = the_mesh.needsTimeUpdate(fs);
    if (needs_update) {
      wait_for_advert = true;
      Serial.println("[LOG] Probudil se z deep sleep - čas potřebuje update, čekání na advert paket");
    } else {
      Serial.println("[LOG] Probudil se z deep sleep - čas je OK, odesíláme data");
    }
  }
  
  Serial.print("[LOG] wait_for_advert=");
  Serial.println(wait_for_advert);
  
  // Declare variable before goto (to avoid compilation error)
  bool should_send_data = false;
  
  // Wait for advert packet if needed (BEFORE sending data)
  // Ignore all packets except ADVERT for 60 minutes
  if (wait_for_advert) {
    Serial.println("[LOG] Začátek čekání na ADVERT paket (light sleep) - 60 minut");
    Serial.println("[LOG] Ignorujeme všechny pakety kromě ADVERT s novým časem");
    Serial.println("[LOG] Radio stays in RX mode during light sleep to receive advert packets");
    
    // Set flag to ignore all non-ADVERT packets
    the_mesh._ignore_non_advert_packets = true;
    
    // Reset flag for time update detection
    the_mesh._time_updated_from_advert = false;
    
    // Use light sleep to wait for advert packets (radio stays in RX mode)
    // Wake up only on LoRa packet (GPIO wakeup) or after max timeout (60 minutes)
    const uint32_t MAX_WAIT_TIME_SECS = 60 * 60;    // 60 minutes
    unsigned long wait_start = millis();
    uint32_t wait_elapsed_secs = 0;
    
    // Ensure radio is in RX mode before entering light sleep
    the_mesh.loop();
    delay(100); // Give radio time to enter RX mode
    
    // Verify radio is in RX mode
    extern WRAPPER_CLASS radio_driver;
    bool is_rx = radio_driver.isInRecvMode();
    Serial.print("[LOG] Radio in RX mode before sleep: ");
    Serial.println(is_rx ? "YES" : "NO");
    
    if (!is_rx) {
      Serial.println("[LOG] WARNING: Radio not in RX mode! Forcing RX mode...");
      the_mesh.loop();
      delay(100);
      is_rx = radio_driver.isInRecvMode();
      Serial.print("[LOG] Radio in RX mode after retry: ");
      Serial.println(is_rx ? "YES" : "NO");
    }
    
    Serial.println("[LOG] Entering light sleep - will wake up on ADVERT packet or after 60 minutes");
    Serial.println("[LOG] All non-ADVERT packets will be ignored");
    
    // Counter for ignored packets (REQ, etc.) that wake us up
    int ignored_packets = 0;
    
    while (wait_elapsed_secs < MAX_WAIT_TIME_SECS && !the_mesh._time_updated_from_advert) {
      // Enter light sleep with long timeout (wakes up on LoRa packet OR after timeout)
      // Radio stays in RX mode during light sleep - this is key!
      // When any packet arrives, DIO1 interrupt wakes device immediately
      // We will check if it's ADVERT, if not, ignore it and continue waiting
      uint32_t remaining_time = MAX_WAIT_TIME_SECS - wait_elapsed_secs;
      
      // Ensure radio is in RX mode before each sleep
      the_mesh.loop();
      delay(50);
      
      Serial.print("[LOG] Entering light sleep, remaining: ");
      Serial.print(remaining_time);
      Serial.println(" seconds");
      
      board.enterLightSleep(remaining_time, -1);
      
      // Log wakeup reason
      esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
      Serial.print("[LOG] Woke up from light sleep, reason: ");
      if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.print("GPIO");
        #ifdef P_LORA_DIO_1
          Serial.print(" (DIO1=");
          Serial.print(digitalRead(P_LORA_DIO_1));
          Serial.print(")");
        #endif
      } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.print("TIMER");
      } else {
        Serial.print("UNKNOWN(");
        Serial.print(wakeup_reason);
        Serial.print(")");
      }
      Serial.println();
      
      // Update elapsed time BEFORE checking wakeup reason
      // This ensures we know how long we've been waiting
      wait_elapsed_secs = (millis() - wait_start) / 1000;
      
      // Check for timeout first
      if (wait_elapsed_secs >= MAX_WAIT_TIME_SECS) {
        Serial.println("[LOG] Woke up from light sleep - timeout reached (60 minut)");
        Serial.println("[LOG] Žádný ADVERT paket nepřijat během 60 minut");
        break;
      }
      
      // Check if we woke up due to packet (either via startup reason OR radio has packet ready)
      bool has_packet = (board.getStartupReason() == BD_STARTUP_RX_PACKET);
      Serial.print("[LOG] Startup reason: ");
      Serial.print(board.getStartupReason());
      Serial.print(" (BD_STARTUP_RX_PACKET=");
      Serial.print(BD_STARTUP_RX_PACKET);
      Serial.print("), has_packet=");
      Serial.println(has_packet);
      
      // Also check if radio has packet ready (in case startup reason wasn't set correctly)
      // This handles cases where multiple packets arrive quickly
      if (!has_packet) {
        // Check DIO1 pin directly
        #ifdef P_LORA_DIO_1
          bool dio1_high = (digitalRead(P_LORA_DIO_1) == HIGH);
          Serial.print("[LOG] DIO1 pin level: ");
          Serial.println(dio1_high ? "HIGH" : "LOW");
          if (dio1_high) {
            Serial.println("[LOG] DIO1 is HIGH but startup reason not set - forcing packet ready");
            radio_driver.forcePacketReady();
            has_packet = true;
          }
        #endif
        
        // Try to check if radio has packet
        the_mesh.loop();  // This will process any pending packets
        if (the_mesh._time_updated_from_advert) {
          has_packet = true;  // ADVERT was processed
          Serial.println("[LOG] ADVERT detected via loop() even though startup reason was not RX_PACKET");
        }
      }
      
      if (has_packet) {
        // Paket přijat - zpracujeme JEN ADVERT, vše ostatní ignorujeme
        radio_driver.forcePacketReady();
        
        // CRITICAL: Call loop() multiple times to fully process packet
        // One loop() call is not enough!
        Serial.println("[LOG] Processing packet...");
        for (int i = 0; i < 10; i++) {
          the_mesh.loop();
          delay(10);
        }
        
        if (the_mesh._time_updated_from_advert) {
          // ADVERT s časem přijat - hotovo!
          Serial.println("[LOG] ✓ ADVERT přijat - čas synchronizován!");
          delay(5000); // Wait for system stabilization
          break;
        }
        
        // Non-ADVERT paket - ignorovat, reinit radio a zpět do spánku
        ignored_packets++;
        radio_driver.reinitInterrupts();
        the_mesh.loop();
        
        // Wait for DIO1 to go LOW before going back to sleep
        // This prevents immediate wakeup if pin is still HIGH
        #ifdef P_LORA_DIO_1
          while (digitalRead(P_LORA_DIO_1) == HIGH) {
            delay(10);
          }
        #endif
        delay(50);  // Additional delay to stabilize
        continue;  // Zpět na začátek smyčky BEZ logování
      }
      
      // No packet and no timeout - go back to sleep
      // This can happen if wakeup was spurious or DIO1 was briefly HIGH
      // Wait for DIO1 to go LOW before going back to sleep
      #ifdef P_LORA_DIO_1
        while (digitalRead(P_LORA_DIO_1) == HIGH) {
          delay(10);
        }
      #endif
      delay(100);  // Additional delay to prevent rapid wakeup loop
      continue;
    }
    
    // After loop, check if time was updated
    // Reset flag to allow all packets again
    the_mesh._ignore_non_advert_packets = false;
    
    if (!the_mesh._time_updated_from_advert) {
      // Timeout - žádný ADVERT nepřijat během 60 minut
      Serial.println("[LOG] ========================================");
      Serial.println("[LOG] Time sync timeout (60 minut)");
      Serial.print("[LOG] Ignorováno paketů: ");
      Serial.println(ignored_packets);
      Serial.println("[LOG] Čas NEBYL synchronizován - jdeme do deep sleep");
      Serial.println("[LOG] Při dalším probuzení zkontrolujeme, jestli čas potřebuje update");
      Serial.println("[LOG] ========================================");
      
      // NEPOSÍLÁME data - čas není synchronizován
      // Při probuzení z deep sleep zkontrolujeme needsTimeUpdate() a znovu počkáme na ADVERT
      // Jdeme rovnou do deep sleep
      goto deep_sleep;
    } else {
      Serial.println("[LOG] ✓ Time synchronized from advert - proceeding to send data");
    }
  }
  
  // Read sensors and send data
  // Two scenarios:
  // 1. Time was updated from advert (_time_updated_from_advert = true) - send normal data
  // 2. Time was OK before (wait_for_advert = false) - send normal data
  
  if (the_mesh._time_updated_from_advert) {
    // Scenario 1: Time was successfully updated from advert
    Serial.println("[LOG] Čas byl synchronizován z advertu - odesíláme normální data");
    should_send_data = true;
  } else if (!wait_for_advert) {
    // Scenario 2: Time was OK before (didn't need to wait)
    Serial.println("[LOG] Čas byl OK před startem - odesíláme normální data");
    should_send_data = true;
  }
  
  if (should_send_data) {
    // NOTE: In deep sleep mode, we send ADVERT only on first startup (not after wakeup)
    // This ensures companion radio knows us as a contact, so it can process our TXT_MSG messages
    // After first startup, we skip ADVERT to save power and reduce airtime
    if (is_first_startup) {
      Serial.println("[LOG] První start - odesílání ADVERT paketu, aby companion znal senzor jako kontakt...");
      the_mesh.sendSelfAdvertisement(0);  // Send immediately (no delay)
      the_mesh.loop();  // Process to actually send the ADVERT
      delay(1000);  // Wait 1 second for ADVERT to be transmitted
      Serial.println("[LOG] ADVERT paket odeslán");
    } else {
      Serial.println("[LOG] Probudil se z deep sleep - přeskočení ADVERT (companion už nás zná)");
    }
    
    // Send time sync notification FIRST (if available) - BEFORE sensor data
    if (the_mesh._send_time_sync_notification) {
      Serial.println("[LOG] Odesílání zprávy o time sync na companion...");
      char time_update_msg[160];
      int len = snprintf(time_update_msg, sizeof(time_update_msg), 
        "Time synchronized: %u -> %u (diff: %d seconds, from: %s)", 
        the_mesh._time_sync_old_time, the_mesh._time_sync_new_time, 
        (int)(the_mesh._time_sync_new_time - the_mesh._time_sync_old_time), 
        the_mesh._time_sync_repeater_id);
      if (len > 0 && len < (int)sizeof(time_update_msg)) {
        the_mesh.sendWarningToCompanionPublic(time_update_msg);
        the_mesh._send_time_sync_notification = false; // Clear flag
        
        // CRITICAL: Wait for time sync message to be transmitted BEFORE sending sensor data
        Serial.println("[LOG] Čekání na odeslání zprávy o time sync (15 sekund)...");
        unsigned long sync_msg_start = millis();
        while (millis() - sync_msg_start < 15000) {  // Wait 15 seconds
          the_mesh.loop(); // Process mesh operations to send queued messages
          sensors.loop();
          rtc_clock.tick();
          delay(50);
        }
        Serial.println("[LOG] Zpráva o time sync by měla být odeslána");
      }
    }
    
    // Read sensor data (same as companion_radio)
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // Query sensors with all permissions enabled for local reading
    sensors.querySensors(0xFF, telemetry);
    
    // Send sensor data to companion
    Serial.println("[LOG] Odesílání sensor dat na companion");
    the_mesh.loop();
    delay(100);
    the_mesh.sendSensorDataToCompanionPublic(telemetry);
    
    // Wait for data to be sent - call loop() repeatedly to process mesh operations
    // This ensures the message is actually transmitted (ACK window)
    Serial.println("[LOG] Čekání na odeslání zprávy (30 sekund)...");
    unsigned long start_wait = millis();
    unsigned long last_log = 0;
    while (millis() - start_wait < 30000) {  // Wait 30 seconds for transmission (increased from 20)
      the_mesh.loop(); // Process mesh operations to send queued messages
      sensors.loop();
      rtc_clock.tick();
      
      // Log progress every 5 seconds
      if (millis() - last_log >= 5000) {
        Serial.print("[LOG] Čekání na odeslání zprávy: ");
        Serial.print((millis() - start_wait) / 1000);
        Serial.println(" sekund...");
        last_log = millis();
      }
      
      delay(50); // Small delay to prevent tight loop
    }
    
    // Final loops to ensure all operations complete and message is sent
    Serial.println("[LOG] Finální zpracování před deep sleep (50x loop)...");
    for (int i = 0; i < 50; i++) {  // Increased from 30 to 50
      the_mesh.loop();
      sensors.loop();
      rtc_clock.tick();
      delay(100);
    }
    
    // Additional wait to ensure radio finishes transmission
    Serial.println("[LOG] Finální čekání před vypnutím radia (5 sekund)...");
    delay(5000);
    
    Serial.println("[LOG] Zpráva by měla být odeslána, přechod do deep sleep");
  }
  
deep_sleep:
  
  Serial.println("[LOG] ========================================");
  Serial.println("[LOG] Přechod do deep sleep");
  Serial.print("[LOG] Interval: ");
  Serial.print(SENSOR_READ_INTERVAL_SECS);
  Serial.println(" sekund");
  Serial.println("[LOG] ========================================");
  
  // Flush Serial to ensure all data is sent before sleep
  Serial.flush();
  delay(500);
  
  // Power down I2C sensors and bus before deep sleep to save power
    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.end();
      pinMode(PIN_BOARD_SDA, INPUT_PULLDOWN);
      pinMode(PIN_BOARD_SCL, INPUT_PULLDOWN);
    #endif
    
    // Power down ADC (A0 pin for battery measurement) before deep sleep to save power
    #if defined(A0) || defined(PIN_VBAT_READ)
      int batt_pin = -1;
      #if defined(A0)
        batt_pin = A0;
      #elif defined(PIN_VBAT_READ)
        batt_pin = PIN_VBAT_READ;
      #endif
      if (batt_pin >= 0) {
        pinMode(batt_pin, INPUT);
      }
    #endif
    
    delay(50); // Small delay after powering down peripherals
    
    // Power down LoRa radio before deep sleep to save power
    extern RADIO_CLASS radio;
    radio.standby();
    delay(50);
    radio.sleep();
    delay(100); // Delay to ensure radio fully enters sleep mode
    
    delay(50); // Additional delay before entering deep sleep
    
    Serial.println("[LOG] Entering deep sleep...");
    
    // Enter deep sleep - only timer wakeup, no radio wakeup
    board.enterDeepSleep(SENSOR_READ_INTERVAL_SECS, -1, false); // false = no radio wakeup
    // CPU halts here and never returns - device will reset on wakeup
    
    // If we reach here, deep sleep failed!
    while(1) { delay(1000); } // Hang here
  }
#endif // ENABLE_DEEP_SLEEP
}

void loop() {
#ifdef ENABLE_DEEP_SLEEP
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 1800  // 30 minutes for production
  #endif
  // Deep sleep mode: This should NEVER be reached!
  // If we get here, deep sleep failed in setup()
  delay(5000);
  // Try to enter deep sleep again
  board.enterDeepSleep(SENSOR_READ_INTERVAL_SECS, -1, false);
  delay(1000);
#else
  // Normal mode (no deep sleep):
  // Debug mode: Read sensors periodically without sleep
  // Device stays awake for easier development and debugging
  static unsigned long last_sensor_read_millis = 0;
  
  // Sensor read interval: Use SENSOR_READ_INTERVAL_SECS if defined, otherwise 60 seconds for debug
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for debug (default)
  #endif
  
  // Read sensors periodically in debug mode
  // Use millis() for timing in debug mode (more reliable than RTC)
  unsigned long curr_millis = millis();
  unsigned long interval_millis = SENSOR_READ_INTERVAL_SECS * 1000;
  
  if (curr_millis >= last_sensor_read_millis + interval_millis || last_sensor_read_millis == 0) {
    Serial.print("\n[LOG] Čtení senzorů (interval: ");
    Serial.print(SENSOR_READ_INTERVAL_SECS);
    Serial.print(" secs, millis: ");
    Serial.println(curr_millis);
    
    // Read sensor data (same as companion_radio)
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // Query sensors with all permissions enabled for local reading
    sensors.querySensors(0xFF, telemetry);
    
    Serial.print("[LOG] Telemetry size after query: ");
    Serial.print(telemetry.getSize());
    Serial.println(" bytes");
    
    // Send sensor data to companion
    Serial.println("[LOG] Odesílání sensor dat na companion");
    the_mesh.loop();
    delay(100);
    the_mesh.sendSensorDataToCompanionPublic(telemetry);
    
    // Process mesh operations to send the message
    // Call loop() multiple times to ensure message is transmitted
    Serial.println("[LOG] Zpracování mesh operací pro odeslání zprávy...");
    for (int i = 0; i < 20; i++) {
      the_mesh.loop();
      sensors.loop();
      rtc_clock.tick();
      delay(50);
    }
    Serial.println("[LOG] Čtení a odesílání senzorů dokončeno");
    
    last_sensor_read_millis = curr_millis;
  }
  
  // Handle serial commands
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
#endif // !ENABLE_DEEP_SLEEP
}
