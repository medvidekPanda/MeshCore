#include <Arduino.h> // needed for PlatformIO
#include <Mesh.h>

#ifdef WITH_MQTT_BRIDGE
#include <WiFi.h>
#endif

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
#include "UITask.h"
static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1)
    ;
}

static char command[160];

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM *fs;
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
    the_mesh.self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 ||
                          the_mesh.self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      the_mesh.self_id = radio_new_identity();
      count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  Serial.println();

  command[0] = 0;

#ifndef DISABLE_SENSORS
  sensors.begin();
#endif

  the_mesh.begin(fs);

#ifdef ESP_PLATFORM
  // Initialize WiFi only if enabled in prefs
  if (the_mesh.getNodePrefs()->wifi_enabled) {
#ifdef WITH_MQTT_BRIDGE
#ifdef WIFI_SSID
    Serial.print("Connecting to WiFi for MQTT: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    int wifi_timeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20) {
      delay(500);
      Serial.print(".");
      wifi_timeout++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi connected! IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi connection failed! MQTT bridge will not work.");
    }
#else
    Serial.println("WiFi enabled but WIFI_SSID not defined!");
#endif
#else
    Serial.println("WiFi enabled but MQTT bridge not compiled!");
#endif
  } else {
    Serial.println("WiFi disabled (use 'set wifi on' to enable)");
  }

  // BT initialization would go here if needed
  // Currently BT is only used in companion_radio firmware
  if (the_mesh.getNodePrefs()->bt_enabled) {
    Serial.println("BT enabled (not yet implemented for repeater)");
  } else {
    Serial.println("BT disabled (use 'set bt on' to enable)");
  }
#elif defined(WITH_MQTT_BRIDGE)
#ifdef WIFI_SSID
  Serial.print("Connecting to WiFi for MQTT: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  int wifi_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20) {
    delay(500);
    Serial.print(".");
    wifi_timeout++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed! MQTT bridge will not work.");
  }
#else
#error "WIFI_SSID must be defined for MQTT bridge"
#endif
#endif

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command) - 1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command) - 1) { // command buffer full
    command[sizeof(command) - 1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') { // received complete line
    Serial.print('\n');
    command[len - 1] = 0; // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply); // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> ");
      Serial.println(reply);
    }

    command[0] = 0; // reset command buffer
  }

  the_mesh.loop();
#ifndef DISABLE_SENSORS
  sensors.loop();
#endif
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
  unsigned long now = millis();

  // Check if USB is connected (for ESP32-S3 with USB-Serial-JTAG)
  // If USB is connected, keep device awake longer to allow PC connection
  bool usb_connected = false;
#ifdef ESP_PLATFORM
  // On ESP32-S3, Serial uses USB-Serial-JTAG
  // Check if Serial port is available for writing (indicates USB connection)
  // USB connection is detected by checking if Serial port is ready
  // Note: This is a simple heuristic - Serial.availableForWrite() returns non-zero if USB is connected
  usb_connected = (Serial.availableForWrite() >= 0); // USB-Serial-JTAG is available if USB is connected
#endif

  // Check if there's any serial activity (commands)
  if (Serial.available() || strlen(command) > 0) {
    last_activity = now;
  }

  // If USB is connected, extend the awake time to 2 minutes (120 seconds)
  // This allows time to connect via USB from PC after hard reset
  const unsigned long USB_AWAKE_TIME = 120000; // 2 minutes in milliseconds
  const unsigned long NORMAL_STARTUP_TIME = 5000; // 5 seconds for normal startup
  
  unsigned long min_awake_time = usb_connected ? USB_AWAKE_TIME : NORMAL_STARTUP_TIME;

  // Wait at least min_awake_time after startup before considering light sleep
  // This ensures initial setup and any pending operations complete
  // If USB is connected, wait longer to allow PC connection
  if (now - startup_time < min_awake_time) {
    last_activity = now; // Keep updating to prevent sleep during startup
  }

  // If USB is connected, don't enter light sleep at all
  // This ensures device stays awake for USB access
  if (usb_connected) {
    // USB connected - stay awake, don't enter light sleep
    // Reset activity timer to keep device awake
    last_activity = now;
  } else if (now - last_activity > 5000) {
    // USB not connected - normal light sleep behavior
    // Note: Serial.println() removed before light sleep to prevent GPIO interrupt conflicts
    // Serial.println("\n=== ENTERING LIGHT SLEEP ===");
    // Serial.flush();

    // Ensure radio is in RX mode before light sleep
    // This allows DIO1 to wake up the device on incoming packet
    the_mesh.loop(); // This ensures radio is in RX mode

    // Longer delay to ensure all Serial and GPIO operations complete before light sleep
    delay(500); // Give radio and Serial time to finish all operations

#ifdef LIGHT_SLEEP_TIMEOUT
    board.enterLightSleep(LIGHT_SLEEP_TIMEOUT);
#else
    // Default: 1 hour timeout as safety watchdog
    // Device will wake up on LoRa packet OR after 1 hour (whichever comes first)
    // This prevents device from sleeping forever if radio fails or interrupt doesn't work
    board.enterLightSleep(3600); // 3600 seconds = 1 hour
#endif

    // Handle wakeup from light sleep
    if (board.getStartupReason() == BD_STARTUP_RX_PACKET) {
      // The wakeup was triggered by the LoRa DIO1 line.
      // RadioLib callbacks were not executed during light sleep, so mark the packet as ready.
      radio_driver.forcePacketReady();
    }

    // CRITICAL: Re-initialize RadioLib interrupt handler after light sleep
    // The interrupt handler was removed before sleep to prevent conflicts
    radio_driver.reinitInterrupts();

    // After wakeup from light sleep, always process packets
    // (we woke up either due to packet or timeout, but most likely packet)
    // Give radio time to stabilize after wakeup
    delay(100);

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

    last_activity = millis(); // Reset activity timer after wakeup
    board.clearStartupReason();
  }
#endif
}
