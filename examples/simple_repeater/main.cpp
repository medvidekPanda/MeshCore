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

  // Check if there's any serial activity (commands)
  if (Serial.available() || strlen(command) > 0) {
    last_activity = now;
  }

  // Wait at least 5 seconds after startup before considering light sleep
  // This ensures initial setup and any pending operations complete
  if (now - startup_time < 5000) {
    last_activity = now; // Keep updating to prevent sleep during startup
  }

  // If no activity for 5 seconds, enter light sleep
  if (now - last_activity > 5000) {
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
    board.enterLightSleep(0); // No timeout, wake only on LoRa packet
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

    // Call loop() multiple times to ensure packet is fully processed and response sent
    // First few calls process the received packet, later calls send the response
    // Total time: ~1 second to ensure response is sent
    for (int i = 0; i < 50; i++) {
      the_mesh.loop();
      delay(20); // Delay to allow TX to complete
    }

    last_activity = millis(); // Reset activity timer after wakeup
    board.clearStartupReason();
  }
#endif
}
