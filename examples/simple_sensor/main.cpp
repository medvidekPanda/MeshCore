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

// Debug logging macros - only active when MESH_DEBUG or SENSOR_DEBUG is defined
#if (defined(MESH_DEBUG) || defined(SENSOR_DEBUG)) && defined(ARDUINO)
  #include <Arduino.h>
  #define SENSOR_LOG_PRINT(...) Serial.print(__VA_ARGS__)
  #define SENSOR_LOG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define SENSOR_LOG_PRINT(...) {}
  #define SENSOR_LOG_PRINTLN(...) {}
#endif

// Constants
const uint32_t SENSOR_DATA_WAIT_MS = 30000;        // 30 seconds
const uint32_t FINAL_PROCESSING_LOOPS = 50;
const uint32_t FINAL_RADIO_WAIT_MS = 5000;         // 5 seconds
const int COMPANION_ID_HEX_LEN = 64;

class MyMesh : public SensorMesh {
public:
  bool allow_time_sync = true;  // Defaultně povoleno, vypne se po synchronizaci nebo při deep sleep
  
  void setAllowTimeSync(bool allow) {
    allow_time_sync = allow;
  }

  // Přepisujeme loop() aby během čekání na time sync neposílal advertisementy
  void loop() {
    // Během čekání na time sync pouze posloucháme (mesh::Mesh::loop()), ale NEPOSÍLÁME advertisementy
    if (allow_time_sync) {
      // Pouze příjem paketů, žádné posílání
      mesh::Mesh::loop();
    } else {
      // Normální režim - voláme SensorMesh::loop() který může posílat advertisementy
      SensorMesh::loop();
    }
  }

  void onAdvertRecv(mesh::Packet* packet, const mesh::Identity& id, uint32_t timestamp, const uint8_t* app_data, size_t app_data_len) override {
    SENSOR_LOG_PRINT("[ADVERT] Received from ");
    #if defined(MESH_DEBUG) || defined(SENSOR_DEBUG)
      mesh::Utils::printHex(Serial, id.pub_key, 4);
      Serial.print(" timestamp=");
      Serial.print(timestamp);
      Serial.print(" allow_sync=");
      Serial.println(allow_time_sync ? "YES" : "NO");
    #endif
    
         // Pokud je povolena synchronizace a cas v paketu je validni (> 1.1.2020)
         if (allow_time_sync && timestamp > 1577836800) {
            SENSOR_LOG_PRINT("[ADVERT] Valid timestamp received: ");
            SENSOR_LOG_PRINTLN(timestamp);
            SENSOR_LOG_PRINTLN("[ADVERT] Updating RTC and system time...");
            
            // Aktualizuj cas v RTC modulu i v ESP32
            getRTCClock()->setCurrentTime(timestamp);
            
            // Overeni aktualizace
            uint32_t new_time = getRTCClock()->getCurrentTime();
            SENSOR_LOG_PRINT("[ADVERT] Time updated! New time: ");
            SENSOR_LOG_PRINTLN(new_time);
            
            // Po uspesne synchronizaci vypni dalsi pokusy
            allow_time_sync = false;
            SENSOR_LOG_PRINTLN("[ADVERT] Time sync disabled - continuing program");
         } else if (allow_time_sync && timestamp <= 1577836800) {
            SENSOR_LOG_PRINTLN("[ADVERT] Timestamp invalid (too old) - ignoring");
         }
  }

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
  static bool validateHexString(const char* str, int expected_len) {
    if (!str) return false;
    int len = strlen(str);
    if (len != expected_len) return false;
    
    for (int i = 0; i < len; i++) {
      char c = str[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        return false;
      }
    }
    return true;
  }

  bool validateCompanionId(const char* id) {
    return validateHexString(id, COMPANION_ID_HEX_LEN);
  }

  // Helper: Open file for reading (platform-specific)
  static File openFileForRead(FILESYSTEM* fs, const char* filename) {
#if defined(RP2040_PLATFORM)
    return fs->open(filename, "r");
#else
    return fs->open(filename);
#endif
  }

  // Helper: Open file for writing (platform-specific)
  static File openFileForWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    fs->remove(filename);
    return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    return fs->open(filename, "w");
#else
    return fs->open(filename, "w", true);
#endif
  }

  // Load companion ID from filesystem (if exists and valid, otherwise use default from build flag)
  void loadCompanionId(FILESYSTEM* fs) {
    const char* filename = "/companion_id";
    if (fs->exists(filename)) {
      File file = openFileForRead(fs, filename);
      if (file) {
        char loaded_id[65];
        int len = file.readBytes(loaded_id, sizeof(loaded_id) - 1);
        loaded_id[len] = '\0';
        file.close();
        
        if (validateHexString(loaded_id, COMPANION_ID_HEX_LEN)) {
          strncpy(companion_id, loaded_id, sizeof(companion_id) - 1);
          companion_id[sizeof(companion_id) - 1] = '\0';
          SENSOR_LOG_PRINT("[LOG] Companion ID loaded from file: ");
          SENSOR_LOG_PRINTLN(companion_id);
          return;
        } else {
          SENSOR_LOG_PRINTLN("[LOG] Warning: Invalid companion ID in file, using default");
        }
      }
    }
    // Use default from build flag
    strncpy(companion_id, COMPANION_ID, sizeof(companion_id) - 1);
    companion_id[sizeof(companion_id) - 1] = '\0';
    SENSOR_LOG_PRINT("[LOG] Companion ID from build flag: ");
    SENSOR_LOG_PRINTLN(companion_id);
  }

  // Save companion ID to filesystem (permanent storage - survives reboots and power cycles)
  void saveCompanionId(FILESYSTEM* fs) {
    const char* filename = "/companion_id";
    File file = openFileForWrite(fs, filename);
    if (file) {
      size_t written = file.write((const uint8_t*)companion_id, strlen(companion_id));
      file.flush();  // Ensure data is written to storage
      file.close();
      if (written == strlen(companion_id)) {
        SENSOR_LOG_PRINT("[LOG] Companion ID saved: ");
        SENSOR_LOG_PRINTLN(companion_id);
      } else {
        SENSOR_LOG_PRINTLN("[LOG] Warning: Failed to write companion ID completely");
      }
    } else {
      SENSOR_LOG_PRINTLN("[LOG] Error: Cannot open file for writing companion ID");
    }
  }

  // Public methods for external access
  void sendSensorDataToCompanionPublic(CayenneLPP& telemetry) {
    sendSensorDataToCompanion(telemetry);
  }

  void sendWarningToCompanionPublic(const char* message) {
    sendMessageToChannel(message);
  }


  // Set filesystem for companion ID operations
  void setFilesystem(FILESYSTEM* fs) {
    _companion_fs = fs;
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
      SENSOR_LOG_PRINTLN("[LOG] Error: invalid hex secret key");
      return channel;  // Return with zeroed secret
    }
    
    // Create hash from secret (SHA256, use first byte as hash)
    mesh::Utils::sha256(channel.hash, sizeof(channel.hash), channel.secret, 16);
    
    return channel;
  }

  void sendSensorDataToCompanion(CayenneLPP& telemetry) {
    // Parse telemetry data to extract values
    LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
    uint8_t channel_id, type;
    float temp_sht40 = NAN, humidity_sht40 = NAN, pressure_bmp280 = NAN, voltage = NAN;
    bool found_sht40_temp = false, found_sht40_humidity = false;
    
    while (reader.readHeader(channel_id, type)) {
      switch (type) {
        case LPP_TEMPERATURE: {
          float temp_val;
          if (reader.readTemperature(temp_val) && !found_sht40_temp) {
            temp_sht40 = temp_val;
            found_sht40_temp = true;
          }
          break;
        }
        case LPP_RELATIVE_HUMIDITY: {
          float hum_val;
          if (reader.readRelativeHumidity(hum_val) && !found_sht40_humidity) {
            humidity_sht40 = hum_val;
            found_sht40_humidity = true;
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
          if (reader.readVoltage(volt_val) && isnan(voltage)) {
            voltage = volt_val;
          }
          break;
        }
        default:
          reader.skipData(type);
          break;
      }
    }
    
    // Check if sensors are communicating
    bool sensors_ok = !(isnan(temp_sht40) && isnan(humidity_sht40) && isnan(pressure_bmp280));
    
    char text_data[128];
    uint32_t timestamp = getRTCClock()->getCurrentTime();
    int len;
    
    if (!sensors_ok && telemetry.getSize() <= 4) {
      len = snprintf(text_data, sizeof(text_data), "Sensor communication error: sensors not responding (time: %u)", timestamp);
    } else if (!sensors_ok) {
      len = snprintf(text_data, sizeof(text_data), "Sensor partial error: SHT40/BMP280 not responding (time: %u, voltage: %.3fV)", 
        timestamp, isnan(voltage) ? 0.0f : voltage);
    } else {
      len = snprintf(text_data, sizeof(text_data), "%u,%.1f,%.1f,%.1f,%.3f",
        timestamp,
        isnan(temp_sht40) ? 0.0f : temp_sht40,
        isnan(humidity_sht40) ? 0.0f : humidity_sht40,
        isnan(pressure_bmp280) ? 0.0f : pressure_bmp280,
        isnan(voltage) ? 0.0f : voltage
      );
    }
    
    if (len >= (int)sizeof(text_data)) {
      SENSOR_LOG_PRINTLN("[LOG] Error: text data too long");
      return;
    }
    
    sendMessageToChannel(text_data);
  }

  void sendMessageToChannel(const char* message) {
    mesh::GroupChannel channel = createChannelFromHexSecret(SENSOR_CHANNEL_SECRET);
    
    // Get sender name (first 4 bytes of public key as hex string)
    char sender_name[10];
    mesh::Utils::toHex(sender_name, self_id.pub_key, 4);
    sender_name[8] = '\0';
    
    // Create message in format "sender_name: message"
    int message_len = strlen(message);
    int sender_name_len = strlen(sender_name);
    int total_text_len = sender_name_len + 2 + message_len;  // "sender: message"
    
    if (total_text_len > MAX_PACKET_PAYLOAD - 5) {
      SENSOR_LOG_PRINTLN("[LOG] Error: message too long");
      return;
    }
    
    uint8_t data[MAX_PACKET_PAYLOAD];
    uint32_t timestamp = getRTCClock()->getCurrentTimeUnique();
    memcpy(data, &timestamp, 4);
    data[4] = 0;  // TXT_TYPE_PLAIN
    
    sprintf((char *)&data[5], "%s: %s", sender_name, message);
    int actual_len = strlen((char *)&data[5]);
    
    // Create group packet
    mesh::Packet* pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, channel, data, 5 + actual_len);
    if (pkt == NULL) {
      SENSOR_LOG_PRINTLN("[LOG] Error: cannot create packet");
      return;
    }
    
    // Send as flood (works across multiple hops)
    sendFlood(pkt);
    last_room_msg_time = getRTCClock()->getCurrentTime();
  }

  void onSensorDataRead() override {
    // Record battery data and check alerts
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);
    battery_data.recordData(getRTCClock(), batt_voltage);
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
      
      // Validate hex string
      if (!validateHexString(new_id, COMPANION_ID_HEX_LEN)) {
        strcpy(reply, "Error: Companion ID must be 64 hex characters");
        return true;
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
          File file = openFileForRead(_companion_fs, filename);
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

  SENSOR_LOG_PRINT("Sensor ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  SENSOR_LOG_PRINTLN();

  command[0] = 0;

  sensors.begin();
  the_mesh.setFilesystem(fs);
  the_mesh.begin(fs);
  the_mesh.loadCompanionId(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

// #ifndef ENABLE_DEEP_SLEEP
//   // send out initial Advertisement to the mesh (only in non-deep-sleep mode)
//   // In deep sleep mode, sensor only listens for ADVERT packets, doesn't send them
//   // the_mesh.sendSelfAdvertisement(16000);
// #endif

  // Check reset reason to determine if this is first startup
  bool is_deepsleep_wakeup = false;
  #ifdef ESP32
    esp_reset_reason_t reset_reason = esp_reset_reason();
    is_deepsleep_wakeup = (reset_reason == ESP_RST_DEEPSLEEP);
    
    SENSOR_LOG_PRINT("[STARTUP] Reset reason: ");
    SENSOR_LOG_PRINT(reset_reason);
    SENSOR_LOG_PRINTLN(is_deepsleep_wakeup ? " (DEEP_SLEEP)" : " (POWER-ON/OTHER)");
  #endif

  // Configure time sync based on startup type
  if (is_deepsleep_wakeup) {
    the_mesh.setAllowTimeSync(false);
    SENSOR_LOG_PRINTLN("[STARTUP] Deep sleep wakeup - time sync DISABLED");
  } else {
    SENSOR_LOG_PRINTLN("[STARTUP] First startup - time sync ENABLED (waiting for advert)");
  }

    // On first startup, wait 20 seconds to allow time sync from advert
  // IMPORTANT: During this wait, NO sensor data will be sent - only listening for ADVERT
  if (!is_deepsleep_wakeup) {
    SENSOR_LOG_PRINTLN("[LOG] First startup - waiting 20 seconds for time sync from ADVERT");
    SENSOR_LOG_PRINTLN("[LOG] During wait: NO sensor data will be sent, only listening for ADVERT");
    
    unsigned long wait_start = millis();
    while (millis() - wait_start < 20000) {  // 20 seconds
      // Zobraz progress kazdych 5 sekund
      unsigned long elapsed = millis() - wait_start;
      if (elapsed % 5000 < 100) {
        SENSOR_LOG_PRINT("[LOG] Waiting for time sync... ");
        SENSOR_LOG_PRINT(elapsed / 1000);
        SENSOR_LOG_PRINTLN("s elapsed (NO data transmission during wait)");
      }
      
      // Během čekání pouze posloucháme mesh (pro ADVERT) a udržujeme senzory, ale NEPOSÍLÁME data
      the_mesh.loop();  // Poslouchá ADVERT pakety
      sensors.loop();   // Udržuje senzory, ale neposílá data
      rtc_clock.tick();
      delay(100);
    }
    
    // Po 20 sekundach - pokud neprisel ADVERT, zkus nacist cas z RTC modulu
    if (the_mesh.allow_time_sync) {
      SENSOR_LOG_PRINTLN("[LOG] No ADVERT received - trying to load time from RTC module");
      uint32_t rtc_time = rtc_clock.getCurrentTime();
      if (rtc_time > 1577836800) {  // Validni cas (> 1.1.2020)
        SENSOR_LOG_PRINT("[LOG] RTC module time loaded: ");
        SENSOR_LOG_PRINTLN(rtc_time);
        // Cas z RTC modulu je uz nacteny v systemu, jen vypneme dalsi pokusy o sync
        the_mesh.setAllowTimeSync(false);
      } else {
        SENSOR_LOG_PRINTLN("[LOG] RTC module time invalid or not available");
        SENSOR_LOG_PRINTLN("[LOG] No valid time available - will NOT send sensor data");
      }
    } else {
      SENSOR_LOG_PRINTLN("[LOG] Time was synchronized from ADVERT");
    }
  }
  
  // Read and send sensor data ONLY if we have valid time
  // Valid time means: either deep sleep wakeup (time already set) or allow_time_sync == false (time was synced)
  bool should_send_data = is_deepsleep_wakeup || !the_mesh.allow_time_sync;
  
  if (should_send_data) {
    SENSOR_LOG_PRINTLN("[LOG] Valid time available - reading and sending sensor data");
    
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    sensors.querySensors(0xFF, telemetry);
    
    // Send sensor data
    the_mesh.loop();
    delay(100);
    the_mesh.sendSensorDataToCompanionPublic(telemetry);
    
    // Wait for data to be sent
    unsigned long start_wait = millis();
    while (millis() - start_wait < SENSOR_DATA_WAIT_MS) {
      the_mesh.loop();
      sensors.loop();
      rtc_clock.tick();
      delay(50);
    }
  } else {
    SENSOR_LOG_PRINTLN("[LOG] No valid time - skipping sensor data transmission");
  }

  // Final processing before deep sleep (always, for both debug and deep sleep mode)
  for (int i = 0; i < FINAL_PROCESSING_LOOPS; i++) {
    the_mesh.loop();
    sensors.loop();
    rtc_clock.tick();
    delay(100);
  }
  
  delay(FINAL_RADIO_WAIT_MS);

#ifdef ENABLE_DEEP_SLEEP
  {
  // Deep sleep mode: Sensor-only mode - only wakes up on timer, not on LoRa packets
  // Sensor read interval: 30 minutes (1800 seconds) for production
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 1800  // 30 minutes for production
  #endif
  
deep_sleep:
  SENSOR_LOG_PRINT("[LOG] Entering deep sleep (interval: ");
  SENSOR_LOG_PRINT(SENSOR_READ_INTERVAL_SECS);
  SENSOR_LOG_PRINTLN(" seconds)");
  
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
    
    // Save current time to RTC memory before deep sleep
    // This ensures time continues correctly after wakeup
    // getCurrentTime() auto-saves time, but we also need to save sleep duration
    rtc_clock.getCurrentTime(); // This auto-saves time to RTC memory
    // For ESP32RTCClock, we need to save sleep duration explicitly
    #if defined(ESP_PLATFORM)
      extern ESP32RTCClock fallback_clock;
      fallback_clock.saveTimeBeforeSleep(SENSOR_READ_INTERVAL_SECS);
    #endif
    
    // Enter deep sleep - only timer wakeup, no radio wakeup
    board.enterDeepSleep(SENSOR_READ_INTERVAL_SECS, -1, false);
    // CPU halts here and never returns - device will reset on wakeup
    
    // If we reach here, deep sleep failed!
    while(1) { delay(1000); }
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
    // Read sensor data
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    sensors.querySensors(0xFF, telemetry);
    
    // Send sensor data
    the_mesh.loop();
    delay(100);
    the_mesh.sendSensorDataToCompanionPublic(telemetry);
    
    // Process mesh operations to send the message
    for (int i = 0; i < 20; i++) {
      the_mesh.loop();
      sensors.loop();
      rtc_clock.tick();
      delay(50);
    }
    
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
    SENSOR_LOG_PRINT(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      SENSOR_LOG_PRINT("  -> "); SENSOR_LOG_PRINTLN(reply);
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
