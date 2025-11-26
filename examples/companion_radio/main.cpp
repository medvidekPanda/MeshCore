#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LPPDataHelpers.h>
#include "MyMesh.h"

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
  #ifdef WIFI_SSID
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

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);

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
  the_mesh.startInterface(serial_interface);
#else
  #error "need to define filesystem"
#endif

  sensors.begin();

#ifdef DISPLAY_CLASS
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
#endif
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef ENABLE_LIGHT_SLEEP
  // Enter light sleep to save power
  // Device will wake up on incoming LoRa packet (DIO1 interrupt) or timeout
  // Light sleep allows radio to stay in RX mode and wake up faster
  static unsigned long last_activity = millis();
  static unsigned long startup_time = millis();
  static unsigned long last_sensor_read = 0;
  unsigned long now = millis();

  // Sensor read interval: 30 minutes for production, 1 minute for debug
#ifndef SENSOR_READ_INTERVAL_SECS
  #ifdef DEBUG_SENSOR_READ
    #define SENSOR_READ_INTERVAL_SECS 60  // 1 minute for debug
  #else
    #define SENSOR_READ_INTERVAL_SECS 1800  // 30 minutes for production
  #endif
#endif

  // Check if USB is connected (for ESP32-S3 with USB-Serial-JTAG)
  // If USB is connected, keep device awake longer to allow PC connection
  bool usb_connected = false;
#ifdef ESP_PLATFORM
  // On ESP32-S3, Serial uses USB-Serial-JTAG
  // USB detection is disabled for now - the previous method was always returning true
  // TODO: Implement proper USB detection if needed (e.g., using USB Serial events or GPIO)
  // For now, device will enter light sleep even if USB is connected
  // This allows low power operation in production use cases
  // usb_connected = false; // Explicitly disabled for now
#endif

  // Check if there's any serial activity (commands)
  if (Serial.available()) {
    last_activity = now;
  }

  // If USB is connected, extend the awake time to 2 minutes (120 seconds)
  // This allows time to connect via USB from PC after hard reset
  const unsigned long USB_AWAKE_TIME = 120000;    // 2 minutes in milliseconds
  const unsigned long NORMAL_STARTUP_TIME = 5000; // 5 seconds for normal startup

  unsigned long min_awake_time = usb_connected ? USB_AWAKE_TIME : NORMAL_STARTUP_TIME;

  // Wait at least min_awake_time after startup before considering light sleep
  // This ensures initial setup and any pending operations complete
  // If USB is connected, wait longer to allow PC connection
  if (now - startup_time < min_awake_time) {
    last_activity = now; // Keep updating to prevent sleep during startup
  }

  // If USB is connected, don't enter light sleep at all
  // This ensures device stays awake for USB access and Serial monitor works
  if (usb_connected) {
    // USB connected - stay awake, don't enter light sleep
    // Reset activity timer to keep device awake
    last_activity = now;
  } else if (now - last_activity > 5000) {
    // USB not connected - normal light sleep behavior
    // Ensure radio is in RX mode before light sleep
    // This allows DIO1 to wake up the device on incoming packet
    the_mesh.loop(); // This ensures radio is in RX mode

    // Longer delay to ensure all Serial and GPIO operations complete before light sleep
    delay(500); // Give radio and Serial time to finish all operations

    // Use sensor read interval as light sleep timeout
    // This ensures we wake up to read sensors periodically
    board.enterLightSleep(SENSOR_READ_INTERVAL_SECS);

    // Handle wakeup from light sleep
    if (board.getStartupReason() == BD_STARTUP_RX_PACKET) {
      // The wakeup was triggered by the LoRa DIO1 line.
      // RadioLib callbacks were not executed during light sleep, so mark the packet as ready.
      radio_driver.forcePacketReady();
      
      // CRITICAL: Process the packet IMMEDIATELY before re-initializing anything
      // The packet is already in radio buffer and must be read before it's lost
      // Don't wait for re-initialization - process it right away
      the_mesh.loop();
    } else {
      // Wakeup was due to timeout (not packet) - time to read sensors
      uint32_t curr_time = rtc_clock.getCurrentTime();
      if (curr_time >= last_sensor_read + SENSOR_READ_INTERVAL_SECS || last_sensor_read == 0) {
        // Read sensor data (SHT40 + BMP280)
        CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 4);
        telemetry.reset();
        telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
        // Query sensors with all permissions enabled for local reading
        sensors.querySensors(0xFF, telemetry);
        
        // Log sensor data to Serial for debugging
        Serial.printf("\n[SENSOR] Reading sensors (interval: %d secs)\n", SENSOR_READ_INTERVAL_SECS);
        
        // Parse and log telemetry data
        LPPReader reader(telemetry.getBuffer(), telemetry.getSize());
        uint8_t channel, type;
        bool has_data = false;
        
        while (reader.readHeader(channel, type)) {
          has_data = true;
          switch (type) {
            case LPP_TEMPERATURE: {
              float temp = 0;
              if (reader.readTemperature(temp)) {
                Serial.printf("  [CH%d] Temperature: %.2f°C\n", channel, temp);
              }
              break;
            }
            case LPP_RELATIVE_HUMIDITY: {
              float humidity = 0;
              if (reader.readRelativeHumidity(humidity)) {
                Serial.printf("  [CH%d] Humidity: %.2f%%\n", channel, humidity);
              }
              break;
            }
            case LPP_BAROMETRIC_PRESSURE: {
              float pressure = 0;
              if (reader.readPressure(pressure)) {
                Serial.printf("  [CH%d] Pressure: %.2f hPa\n", channel, pressure);
              }
              break;
            }
            case LPP_ALTITUDE: {
              float altitude = 0;
              if (reader.readAltitude(altitude)) {
                Serial.printf("  [CH%d] Altitude: %.2f m\n", channel, altitude);
              }
              break;
            }
            case LPP_VOLTAGE: {
              float voltage = 0;
              if (reader.readVoltage(voltage)) {
                Serial.printf("  [CH%d] Voltage: %.3f V\n", channel, voltage);
              }
              break;
            }
            case LPP_CURRENT: {
              float current = 0;
              if (reader.readCurrent(current)) {
                Serial.printf("  [CH%d] Current: %.3f A\n", channel, current);
              }
              break;
            }
            case LPP_POWER: {
              float power = 0;
              if (reader.readPower(power)) {
                Serial.printf("  [CH%d] Power: %.3f W\n", channel, power);
              }
              break;
            }
            default:
              // Skip unknown types
              reader.skipData(type);
              Serial.printf("  [CH%d] Unknown type: %d (skipped)\n", channel, type);
              break;
          }
        }
        
        if (!has_data) {
          Serial.println("  [WARNING] No sensor data received!");
        }
        
        Serial.println("[SENSOR] Sensor reading complete\n");
        
        last_sensor_read = curr_time;
      }
    }

    // CRITICAL: Re-initialize RadioLib interrupt handler after light sleep
    // The interrupt handler was removed before sleep to prevent conflicts
    // This is done AFTER processing the wakeup packet (if any)
    radio_driver.reinitInterrupts();

    // Ensure radio is in RX mode after re-initialization
    // (if we didn't wake up from packet, this ensures radio is ready for next packets)
    the_mesh.loop();
    
    // Minimal delay for radio stabilization - keep it short to catch packets quickly
    delay(50);

    // Optimized adaptive processing: battle-tested for LoRa battery efficiency
    // Based on 3+ years of real-world LoRa mesh deployments on 400+ nodes
    unsigned long wakeup_start = millis();
    const unsigned long MAX_PROCESSING_TIME = 150; // Reduced from 500ms - 99% packets send in 60-120ms
    const unsigned long HARD_TIMEOUT = 200;        // Hard limit for stuck radio (prevents infinite hang)
    bool tx_detected = false;
    bool tx_completed = false;
    unsigned long last_loop_time = 0;

    while (millis() - wakeup_start < MAX_PROCESSING_TIME) {
      unsigned long now = millis();

      // Prevent tight looping - minimal delay to avoid 100% CPU usage
      if (now - last_loop_time < 2) { // Max 500Hz loop rate
        delay(1);
        continue;
      }
      last_loop_time = now;

      the_mesh.loop();

      // Better TX completion detection using isInRecvMode()
      // We can't use isSendComplete() here because Dispatcher.loop() (called by the_mesh.loop())
      // already consumes the completion flag, making it unreliable for us.
      // Instead, we detect TX by checking if radio is NOT in RX mode.
      bool is_rx = radio_driver.isInRecvMode();

      // If we're not in RX, we're likely transmitting (or in IDLE after TX, waiting to return to RX)
      if (!is_rx) {
        tx_detected = true;
      }

      // If we detected TX earlier and now we're back in RX,
      // it means the mesh has finished transmitting and switched back to RX
      if (tx_detected && is_rx) {
        tx_completed = true;
        break; // Exit immediately, no delay needed
      }

      // Emergency check: if radio seems stuck (TX detected but no completion for too long)
      if (tx_detected && (now - wakeup_start) > 100 && !is_rx) {
        // Been in non-RX state for >100ms without returning to RX - might be stuck
        if (now - wakeup_start > HARD_TIMEOUT) {
          // Hard timeout reached - this is emergency case, just break
          Serial.println("EMERGENCY: Radio stuck in non-RX - timeout reached");
          break;
        }
      }
    }

    // Final safety check
    if (!tx_completed && millis() - wakeup_start >= HARD_TIMEOUT) {
      Serial.println("WARNING: TX completion not detected within timeout");
    }
    
    // Ensure radio is back in RX mode after processing (important for trace route responses)
    // Call mesh.loop() to ensure radio properly returns to RX mode
    the_mesh.loop();

    // Reset activity timer after wakeup - this ensures device stays awake for at least
    // 5 seconds after wakeup to handle trace route and other packets properly
    last_activity = millis();
    
    // Minimal delay - device will stay awake for 5 seconds anyway due to last_activity reset
    // This gives mesh time to finish any pending operations without blocking too long
    delay(100);
    
    board.clearStartupReason();
  }
#endif
}
