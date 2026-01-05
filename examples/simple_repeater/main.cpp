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

// For power saving
unsigned long lastActive = 0; // mark last active time
unsigned long nextSleepinSecs = 120; // next sleep in seconds. The first sleep (if enabled) is after 2 minutes from boot

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

  // For power saving
  lastActive = millis(); // mark last active time since boot

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

  if (the_mesh.getNodePrefs()->powersaving_enabled &&                     // To check if power saving is enabled
      the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) { // To check if it is time to sleep
    if (!the_mesh.hasPendingWork()) { // No pending work. Safe to sleep
      board.sleep(1800);             // To sleep. Wake up after 30 minutes or when receiving a LoRa packet
      lastActive = millis();
      nextSleepinSecs = 5;  // Default: To work for 5s and sleep again
    } else {
      nextSleepinSecs += 5; // When there is pending work, to work another 5s
    }
  }
}
