#include "MQTTBridge.h"

#ifdef WITH_MQTT_BRIDGE

#include <PubSubClient.h>
#include "MeshCore.h"
#include "helpers/BaseSerialInterface.h"

#ifndef MQTT_BROKER
  #error "MQTT_BROKER must be defined"
#endif

#ifndef MQTT_PORT
  #define MQTT_PORT 1883
#endif

#ifndef MQTT_TOPIC_PREFIX
  #define MQTT_TOPIC_PREFIX "meshcore/"
#endif

MQTTBridge* MQTTBridge::_instance = nullptr;

void MQTTBridge::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
  if (_instance) {
    _instance->onMessage(topic, payload, length);
  }
}

MQTTBridge::MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc)
    : BridgeBase(prefs, mgr, rtc), mqttClient(nullptr), _connected(false),
      last_reconnect_attempt(0), reconnect_interval(5000) {
  _instance = this;
  
  // Copy configuration
  strncpy(broker, MQTT_BROKER, sizeof(broker) - 1);
  broker[sizeof(broker) - 1] = 0;
  port = MQTT_PORT;
  strncpy(topic_prefix, MQTT_TOPIC_PREFIX, sizeof(topic_prefix) - 1);
  topic_prefix[sizeof(topic_prefix) - 1] = 0;
  
  #ifdef MQTT_USER
    strncpy(username, MQTT_USER, sizeof(username) - 1);
    username[sizeof(username) - 1] = 0;
  #else
    username[0] = 0;
  #endif
  
  #ifdef MQTT_PASS
    strncpy(password, MQTT_PASS, sizeof(password) - 1);
    password[sizeof(password) - 1] = 0;
  #else
    password[0] = 0;
  #endif
  
  // Generate client ID
  snprintf(client_id, sizeof(client_id), "meshcore_%02X%02X%02X", 
           (unsigned int)prefs->node_name[0], 
           (unsigned int)prefs->node_name[1], 
           (unsigned int)prefs->node_name[2]);
}

void MQTTBridge::setupMQTTClient() {
  if (mqttClient == nullptr) {
    PubSubClient* client = new PubSubClient(wifiClient);
    client->setServer(broker, port);
    client->setCallback(mqttCallback);
    mqttClient = client;
  }
}

void MQTTBridge::begin() {
  #if BRIDGE_DEBUG && ARDUINO
    Serial.printf("%s BRIDGE: Initializing MQTT bridge to %s:%d...\n", getLogDateTime(), broker, port);
  #endif
  
  setupMQTTClient();
  
  // Ensure WiFi is connected
  if (WiFi.status() != WL_CONNECTED) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: WiFi not connected, MQTT bridge cannot start\n", getLogDateTime());
    #endif
    _initialized = false;
    return;
  }
  
  reconnect();
  
  if (_connected) {
    _initialized = true;
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: MQTT bridge connected\n", getLogDateTime());
    #endif
  } else {
    _initialized = false;
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: MQTT bridge connection failed\n", getLogDateTime());
    #endif
  }
}

void MQTTBridge::end() {
  #if BRIDGE_DEBUG && ARDUINO
    Serial.printf("%s BRIDGE: Stopping MQTT bridge...\n", getLogDateTime());
  #endif
  
  if (mqttClient) {
    ((PubSubClient*)mqttClient)->disconnect();
  }
  
  _connected = false;
  _initialized = false;
}

bool MQTTBridge::isRunning() const {
  return _initialized && _connected;
}

void MQTTBridge::reconnect() {
  if (!mqttClient) return;
  
  PubSubClient* client = (PubSubClient*)mqttClient;
  
  // Check if already connected
  if (client->connected()) {
    _connected = true;
    return;
  }
  
  // Try to connect
  bool connected = false;
  if (username[0] != 0) {
    connected = client->connect(client_id, username, password);
  } else {
    connected = client->connect(client_id);
  }
  
  if (connected) {
    _connected = true;
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: MQTT connected\n", getLogDateTime());
    #endif
    
    // Subscribe to receive topic
    char topic[128];
    snprintf(topic, sizeof(topic), "%srx", topic_prefix);
    client->subscribe(topic);
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: Subscribed to %s\n", getLogDateTime(), topic);
    #endif
  } else {
    _connected = false;
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: MQTT connection failed, rc=%d\n", getLogDateTime(), client->state());
    #endif
  }
}

void MQTTBridge::loop() {
  if (!_initialized || !mqttClient) return;
  
  PubSubClient* client = (PubSubClient*)mqttClient;
  
  // Maintain connection
  if (!client->connected()) {
    _connected = false;
    unsigned long now = millis();
    if (now - last_reconnect_attempt >= reconnect_interval) {
      last_reconnect_attempt = now;
      reconnect();
    }
  } else {
    _connected = true;
    client->loop();
  }
}

void MQTTBridge::sendPacket(mesh::Packet* packet) {
  if (!_connected || !mqttClient || !packet) return;
  
  PubSubClient* client = (PubSubClient*)mqttClient;
  
  // Serialize packet
  uint8_t buffer[MAX_FRAME_SIZE + 4];
  uint16_t len = packet->writeTo(buffer + 4);
  if (len <= 0) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: TX failed to serialize packet\n", getLogDateTime());
    #endif
    return;
  }
  
  // Add bridge header
  buffer[0] = (BRIDGE_PACKET_MAGIC >> 8) & 0xFF;
  buffer[1] = BRIDGE_PACKET_MAGIC & 0xFF;
  
  // Calculate checksum
  uint16_t checksum = fletcher16(buffer + 4, len);
  buffer[2] = checksum & 0xFF;
  buffer[3] = (checksum >> 8) & 0xFF;
  
  int total_len = len + 4;
  
  // Publish to MQTT
  char topic[128];
  snprintf(topic, sizeof(topic), "%stx", topic_prefix);
  
  if (client->publish(topic, buffer, total_len)) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: TX published to %s, len=%d\n", getLogDateTime(), topic, total_len);
    #endif
  } else {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: TX publish failed\n", getLogDateTime());
    #endif
  }
}

void MQTTBridge::onMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (length < 4) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: RX packet too small, len=%d\n", getLogDateTime(), length);
    #endif
    return;
  }
  
  // Check magic header
  uint16_t received_magic = ((uint16_t)payload[0] << 8) | payload[1];
  if (received_magic != BRIDGE_PACKET_MAGIC) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: RX invalid magic 0x%04X\n", getLogDateTime(), received_magic);
    #endif
    return;
  }
  
  // Extract checksum
  uint16_t received_checksum = ((uint16_t)payload[3] << 8) | payload[2];
  
  // Validate checksum
  const size_t payload_len = length - 4;
  if (!validateChecksum(payload + 4, payload_len, received_checksum)) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: RX checksum mismatch, rcv=0x%04X\n", getLogDateTime(), received_checksum);
    #endif
    return;
  }
  
  #if BRIDGE_DEBUG && ARDUINO
    Serial.printf("%s BRIDGE: RX, payload_len=%d\n", getLogDateTime(), payload_len);
  #endif
  
  // Parse packet
  mesh::Packet* pkt = _mgr->allocNew();
  if (!pkt) {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: RX failed to allocate packet\n", getLogDateTime());
    #endif
    return;
  }
  
  if (pkt->readFrom(payload + 4, payload_len)) {
    handleReceivedPacket(pkt);
  } else {
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: RX failed to parse packet\n", getLogDateTime());
    #endif
    _mgr->free(pkt);
  }
}

void MQTTBridge::onPacketReceived(mesh::Packet* packet) {
  // This is called from handleReceivedPacket in BridgeBase
  // The packet is already queued for mesh processing
}

#endif // WITH_MQTT_BRIDGE

