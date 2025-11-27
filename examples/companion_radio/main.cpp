#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LPPDataHelpers.h>
#include "MyMesh.h"
#include <helpers/ChannelDetails.h>
#include <math.h>      // for isnan()

#ifdef ESP_PLATFORM
  #include <driver/gpio.h>
  #ifndef ESP32S3
    #include <driver/rtc_io.h>
  #endif
#endif

// Debug mode: When MESH_DEBUG is defined, disable sleep modes and enable BT
// This allows easier development with USB power
#ifdef MESH_DEBUG
  #undef ENABLE_DEEP_SLEEP   // Disable deep sleep in debug mode
  #ifdef DISABLE_WIFI_OTA
    #undef DISABLE_WIFI_OTA   // Allow BT/WiFi in debug mode
  #endif
#endif

// Sensor debug mode: When SENSOR_DEBUG is defined, disable deep sleep
// This allows easier development and debugging of sensor readings
#ifdef SENSOR_DEBUG
  #undef ENABLE_DEEP_SLEEP   // Disable deep sleep in sensor debug mode
  #undef SENSOR_READ_INTERVAL_SECS  // Override any existing value
  #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for sensor debug mode
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef ENABLE_DEEP_SLEEP
    // Deep sleep mode: Always use Serial interface, no BT/WiFi to save power
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #elif defined(WIFI_SSID)
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

#ifdef SENSOR_CHANNEL_NAME
// Forward declaration
void sendSensorDataToChannel(CayenneLPP& telemetry);
#endif

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for Serial to be ready
  
  #ifdef SENSOR_DEBUG
    Serial.println("\n\n=== SENSOR_DEBUG MODE ACTIVE ===");
    Serial.printf("ENABLE_DEEP_SLEEP: %s\n", 
      #ifdef ENABLE_DEEP_SLEEP
        "DEFINED"
      #else
        "NOT DEFINED"
      #endif
    );
    Serial.printf("SENSOR_CHANNEL_NAME: %s\n", 
      #ifdef SENSOR_CHANNEL_NAME
        SENSOR_CHANNEL_NAME
      #else
        "NOT DEFINED"
      #endif
    );
    Serial.println("=== Starting setup ===\n");
  #endif

  board.begin();

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
  }
#endif

  // Wake up radio from sleep mode if it was put to sleep before deep sleep
  // This ensures radio is ready for initialization
  #ifdef ENABLE_DEEP_SLEEP
    extern RADIO_CLASS radio;
    radio.standby(); // Wake radio from sleep mode before initialization
    delay(10); // Small delay for radio to wake up
  #endif
  
  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef BLE_PIN_CODE
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  serial_interface.begin(dev_name, the_mesh.getBLEPin());
#else
  serial_interface.begin(Serial);
#endif
  the_mesh.startInterface(serial_interface);
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
    the_mesh.startInterface(serial_interface);
#elif defined(ESP32)
  SPIFFS.begin(true);
  store.begin();
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#ifdef ENABLE_DEEP_SLEEP
  // Deep sleep mode: Use Serial only, no BT/WiFi to save power
  serial_interface.begin(Serial);
#else
  // Normal mode: Use BT/WiFi if configured
  #ifdef WIFI_SSID
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    serial_interface.begin(TCP_PORT);
  #elif defined(BLE_PIN_CODE)
    char dev_name[32+16];
    sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
    serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #elif defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
#endif
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif

#ifdef ENABLE_DEEP_SLEEP
  // Deep sleep mode: Sensor-only mode - only wakes up on timer, not on LoRa packets
  // Sensor read interval: 30 minutes (1800 seconds) for production
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 1800  // 30 minutes for production
  #endif
  
  // Check if this is first startup (not from deep sleep)
  esp_reset_reason_t reason = esp_reset_reason();
  bool is_first_startup = (reason != ESP_RST_DEEPSLEEP);
  
  // On first startup only, wait 30 seconds before sending data
  // After deep sleep wakeup, send data immediately without waiting
  if (is_first_startup) {
    unsigned long start_wait = millis();
    while (millis() - start_wait < 30000) {
      the_mesh.loop();
      sensors.loop();
      rtc_clock.tick();
      delay(100);
    }
  }
  
  // Read sensors and send data (immediately after deep sleep wakeup, or after 30s wait on first startup)
  {
    // Read sensor data (SHT40 + BMP280)
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // Query sensors with all permissions enabled for local reading
    sensors.querySensors(0xFF, telemetry);
    
    // Send sensor data to private channel if configured
    #ifdef SENSOR_CHANNEL_NAME
      // Ensure mesh is ready before sending
      the_mesh.loop();
      delay(100);
      sendSensorDataToChannel(telemetry);
    #endif
    
    // Wait for data to be sent - call loop() repeatedly to process mesh operations
    // This ensures the message is actually transmitted before entering deep sleep
    unsigned long start_wait = millis();
    while (millis() - start_wait < 5000) {  // Wait 5 seconds for transmission
      the_mesh.loop(); // Process mesh operations to send queued messages
      sensors.loop();
      rtc_clock.tick();
      delay(100); // Small delay to prevent tight loop
    }
    
    // Final loops to ensure all operations complete and message is sent
    for (int i = 0; i < 10; i++) {
      the_mesh.loop();
      delay(100);
    }
    
    // Flush Serial to ensure all data is sent before sleep
    Serial.flush();
    delay(500);
    
    // Note: USB Serial will be disabled in board.enterDeepSleep() to allow proper deep sleep
    // USB connection can prevent deep sleep or consume power (~5-10mA)
    
    // Power down I2C sensors and bus before deep sleep to save power
    // I2C pull-up resistors and sensors can consume significant power (~5-10mA)
    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      // Disable I2C bus to reduce power consumption
      Wire.end();
      // Set I2C pins to input mode with pull-down to minimize leakage
      // This prevents floating pins and reduces current through pull-up resistors
      pinMode(PIN_BOARD_SDA, INPUT_PULLDOWN);
      pinMode(PIN_BOARD_SCL, INPUT_PULLDOWN);
    #endif
    
    // Power down display if present
    #ifdef DISPLAY_CLASS
      // Display may consume power even when not actively used
      // Some displays have power-down commands, but for now we just ensure it's not active
    #endif
    
    // Power down ADC (A0 pin for battery measurement) before deep sleep to save power
    // ADC can consume power even when not actively reading
    #if defined(A0) || defined(PIN_VBAT_READ)
      int batt_pin = -1;
      #if defined(A0)
        batt_pin = A0;
      #elif defined(PIN_VBAT_READ)
        batt_pin = PIN_VBAT_READ;
      #endif
      if (batt_pin >= 0) {
        // Disable ADC and set pin to input to minimize leakage
        // On ESP32-S3, ADC pins should be set to input to reduce power consumption
        pinMode(batt_pin, INPUT);
      }
    #endif
    
    delay(50); // Small delay after powering down peripherals
    
    // Power down LoRa radio before deep sleep to save power
    // RADIO_CLASS is defined in platformio.ini, radio is declared in target.cpp
    extern RADIO_CLASS radio;
    
    // Stop any ongoing radio operations first
    radio.standby();
    delay(50);
    
    // Put radio to sleep mode - this should reduce power consumption to ~0.1uA
    // For SX1262, sleep() puts chip in lowest power mode
    radio.sleep();
    delay(100); // Delay to ensure radio fully enters sleep mode
    
    delay(50); // Additional delay before entering deep sleep to ensure all operations complete
    
    // Enter deep sleep - only timer wakeup, no radio wakeup
    // Will wake up after SENSOR_READ_INTERVAL_SECS (30 minutes)
    // NOTE: XIAO ESP32S3 development board may have higher consumption in deep sleep due to:
    // - Power LED (not controllable via software) - consumes ~1-2mA
    // - Voltage regulator quiescent current - consumes ~0.5-1mA
    // - USB-Serial chip (if present) - consumes ~0.5-1mA
    // For accurate ESP32-S3 deep sleep measurement (~8-20µA), measure directly on module, not dev board
    board.enterDeepSleep(SENSOR_READ_INTERVAL_SECS, -1, false); // false = no radio wakeup
    // CPU halts here and never returns - device will reset on wakeup
    
    // If we reach here, deep sleep failed!
    while(1) { delay(1000); } // Hang here
  }
#endif // ENABLE_DEEP_SLEEP
}

#ifdef SENSOR_CHANNEL_NAME
// Function to send sensor telemetry data to a private channel
void sendSensorDataToChannel(CayenneLPP& telemetry) {
  #ifdef SENSOR_DEBUG
    Serial.printf("[SENSOR] sendSensorDataToChannel called, telemetry size: %d\n", telemetry.getSize());
  #endif
  
  // Find channel by name
  ChannelDetails channel;
  bool channel_found = false;
  
  // Try to find channel by name (search through all channels)
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, channel)) {
      if (strcmp(channel.name, SENSOR_CHANNEL_NAME) == 0) {
        channel_found = true;
        #ifdef SENSOR_DEBUG
          Serial.printf("[SENSOR] Found channel '%s' at index %d\n", SENSOR_CHANNEL_NAME, i);
        #endif
        break;
      }
    }
  }
  
  // If channel not found, try to create it with default PSK
  if (!channel_found) {
    #ifdef SENSOR_DEBUG
      Serial.printf("[SENSOR] Channel '%s' not found, trying to create\n", SENSOR_CHANNEL_NAME);
    #endif
    #ifdef SENSOR_CHANNEL_PSK
      // Create channel with provided PSK
      ChannelDetails* new_channel = the_mesh.addChannel(SENSOR_CHANNEL_NAME, SENSOR_CHANNEL_PSK);
      if (new_channel) {
        channel = *new_channel;
        channel_found = true;
        #ifdef SENSOR_DEBUG
          Serial.printf("[SENSOR] Created channel '%s' successfully\n", SENSOR_CHANNEL_NAME);
        #endif
      } else {
        #ifdef SENSOR_DEBUG
          Serial.printf("[SENSOR] Failed to create channel '%s'\n", SENSOR_CHANNEL_NAME);
        #endif
      }
    #else
      #ifdef SENSOR_DEBUG
        Serial.printf("[SENSOR] No PSK defined, cannot create channel\n");
      #endif
      return;
    #endif
  }
  
  if (!channel_found) {
    #ifdef SENSOR_DEBUG
      Serial.printf("[SENSOR] Channel not found and cannot be created, aborting\n");
    #endif
    return;
  }
  
  // Format sensor data as compact text for PAYLOAD_TYPE_GRP_TXT
  // Format: "timestamp,temp,humidity,pressure,altitude,voltage" (comma-separated values)
  // This is more efficient than binary and can be easily parsed on server
  char text_data[MAX_TEXT_LEN];
  uint32_t timestamp = rtc_clock.getCurrentTime();
  
  // Parse telemetry data to extract values
  // We need: temperature from SHT40, humidity from SHT40, pressure from BMP280, voltage from battery
  // Note: All sensors use TELEM_CHANNEL_SELF, so we parse in order:
  // Battery voltage is added first, then SHT40: temperature, humidity, then BMP280: temperature, pressure, altitude
  LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
  uint8_t channel_id, type;
  float temp_sht40 = NAN, humidity_sht40 = NAN, pressure_bmp280 = NAN, voltage = NAN;
  bool found_sht40_temp = false, found_sht40_humidity = false;
  
  while (reader.readHeader(channel_id, type)) {
    switch (type) {
      case LPP_TEMPERATURE: {
        float temp_val;
        if (reader.readTemperature(temp_val)) {
          // First temperature is from SHT40, subsequent ones are from BMP280 or other sensors
          // We want the first one (SHT40)
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
          // First humidity is from SHT40
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
          // Pressure is from BMP280 (only BMP280 provides pressure)
          pressure_bmp280 = press_val;
        }
        break;
      }
      case LPP_VOLTAGE: {
        float volt_val;
        if (reader.readVoltage(volt_val)) {
          // Battery voltage is added first in telemetry, so use the first voltage reading
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
  
  // Format as compact CSV: timestamp,temp,humidity,pressure,voltage
  // temp = SHT40 temperature, humidity = SHT40 humidity, pressure = BMP280 pressure
  // Use 1 decimal place for efficiency, NAN values as 0.0
  int len = snprintf(text_data, sizeof(text_data), "%u,%.1f,%.1f,%.1f,%.3f",
    timestamp,
    isnan(temp_sht40) ? 0.0f : temp_sht40,
    isnan(humidity_sht40) ? 0.0f : humidity_sht40,
    isnan(pressure_bmp280) ? 0.0f : pressure_bmp280,
    isnan(voltage) ? 0.0f : voltage
  );
  
  #ifdef SENSOR_DEBUG
    Serial.printf("[SENSOR] Parsed values: temp=%.2f, humidity=%.2f, pressure=%.2f, voltage=%.3f\n",
                  temp_sht40, humidity_sht40, pressure_bmp280, voltage);
    Serial.printf("[SENSOR] Formatted data: %s (len=%d)\n", text_data, len);
  #endif
  
  if (len >= (int)sizeof(text_data)) {
    #ifdef SENSOR_DEBUG
      Serial.printf("[SENSOR] Text data too long (%d bytes, max %d)\n", len, (int)sizeof(text_data) - 1);
    #endif
    return;
  }
  
  // Use sendGroupMessage which handles PAYLOAD_TYPE_GRP_TXT properly
  // Use node name as sender name so multiple sensors can be distinguished
  const char* sender_name = the_mesh.getNodeName();
  bool sent = the_mesh.sendGroupMessage(timestamp, channel.channel, sender_name, text_data, len);
  
  #ifdef SENSOR_DEBUG
    if (sent) {
      Serial.printf("[SENSOR] Message sent successfully to channel '%s' from '%s'\n", SENSOR_CHANNEL_NAME, sender_name);
    } else {
      Serial.printf("[SENSOR] Failed to send message to channel '%s'\n", SENSOR_CHANNEL_NAME);
    }
  #endif
}
#endif

void loop() {
  #ifdef SENSOR_DEBUG
    static unsigned long last_debug_print = 0;
    if (millis() - last_debug_print > 10000) { // Print every 10 seconds
      Serial.printf("[LOOP] Running, millis: %lu\n", millis());
      last_debug_print = millis();
    }
  #endif

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
#endif // ENABLE_DEEP_SLEEP

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

// Normal mode (no deep sleep):
#if !defined(ENABLE_DEEP_SLEEP)
  // Debug mode: Read sensors periodically without sleep
  // Device stays awake for easier development and debugging
  static unsigned long last_sensor_read_millis = 0;
  
  // Sensor read interval: 1 minute for debug
  #ifndef SENSOR_READ_INTERVAL_SECS
    #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for debug
  #endif
  
  // Read sensors periodically in debug mode
  // Use millis() for timing in debug mode (more reliable than RTC)
  unsigned long curr_millis = millis();
  unsigned long interval_millis = SENSOR_READ_INTERVAL_SECS * 1000;
  
  if (curr_millis >= last_sensor_read_millis + interval_millis || last_sensor_read_millis == 0) {
    #ifdef SENSOR_DEBUG
      Serial.printf("\n[SENSOR] Reading sensors (interval: %d secs, millis: %lu)\n", SENSOR_READ_INTERVAL_SECS, curr_millis);
    #endif
    
    // Read sensor data (SHT40 + BMP280)
    CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    // Query sensors with all permissions enabled for local reading
    sensors.querySensors(0xFF, telemetry);
    
    #ifdef SENSOR_DEBUG
      Serial.printf("[SENSOR] Telemetry size after query: %d bytes\n", telemetry.getSize());
    #endif
    
    // Parse telemetry data
    LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
    uint8_t channel, type;
    
    while (reader.readHeader(channel, type)) {
      switch (type) {
        case LPP_TEMPERATURE: {
          float temp = 0;
          reader.readTemperature(temp);
          break;
        }
        case LPP_RELATIVE_HUMIDITY: {
          float humidity = 0;
          reader.readRelativeHumidity(humidity);
          break;
        }
        case LPP_BAROMETRIC_PRESSURE: {
          float pressure = 0;
          reader.readPressure(pressure);
          break;
        }
        case LPP_ALTITUDE: {
          float altitude = 0;
          reader.readAltitude(altitude);
          break;
        }
        case LPP_VOLTAGE: {
          float voltage = 0;
          reader.readVoltage(voltage);
          break;
        }
        case LPP_CURRENT: {
          float current = 0;
          reader.readCurrent(current);
          break;
        }
        case LPP_POWER: {
          float power = 0;
          reader.readPower(power);
          break;
        }
        default:
          // Skip unknown types
          reader.skipData(type);
          break;
      }
    }
    
    // Send sensor data to private channel if configured
    #ifdef SENSOR_CHANNEL_NAME
      #ifdef SENSOR_DEBUG
        Serial.printf("[SENSOR] Sending data to channel '%s'\n", SENSOR_CHANNEL_NAME);
      #endif
      // Ensure mesh is ready before sending
      the_mesh.loop();
      delay(100);
      sendSensorDataToChannel(telemetry);
      
      // Process mesh operations to send the message
      // Call loop() multiple times to ensure message is transmitted
      #ifdef SENSOR_DEBUG
        Serial.printf("[SENSOR] Processing mesh operations to send message...\n");
      #endif
      for (int i = 0; i < 20; i++) {
        the_mesh.loop();
        sensors.loop();
        rtc_clock.tick();
        delay(50);
      }
      #ifdef SENSOR_DEBUG
        Serial.printf("[SENSOR] Sensor reading and sending complete\n");
      #endif
    #else
      #ifdef SENSOR_DEBUG
        Serial.printf("[SENSOR] SENSOR_CHANNEL_NAME not defined, skipping channel send\n");
      #endif
    #endif
    
    last_sensor_read_millis = curr_millis;
  }
#endif // !ENABLE_DEEP_SLEEP
}
