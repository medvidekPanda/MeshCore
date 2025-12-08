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
      last_reconnect_attempt(0), reconnect_interval(5000),
      channel_name_callback(nullptr), channel_name_user_data(nullptr) {
  _instance = this;
  
  // Copy configuration for server 1 (primary, from build flags)
  strncpy(broker, MQTT_BROKER, sizeof(broker) - 1);
  broker[sizeof(broker) - 1] = 0;
  port = MQTT_PORT;
  
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
  
  // Copy configuration for server 2 (secondary, from prefs or build flags)
  #ifdef MQTT_BROKER2
    strncpy(broker2, MQTT_BROKER2, sizeof(broker2) - 1);
    broker2[sizeof(broker2) - 1] = 0;
  #else
    // Load from prefs if available
    if (prefs->mqtt_broker2[0] != 0) {
      strncpy(broker2, prefs->mqtt_broker2, sizeof(broker2) - 1);
      broker2[sizeof(broker2) - 1] = 0;
    } else {
      broker2[0] = 0;
    }
  #endif
  
  #ifdef MQTT_PORT2
    port2 = MQTT_PORT2;
  #else
    port2 = prefs->mqtt_port2 > 0 ? prefs->mqtt_port2 : 1883;
  #endif
  
  #ifdef MQTT_USER2
    strncpy(username2, MQTT_USER2, sizeof(username2) - 1);
    username2[sizeof(username2) - 1] = 0;
  #else
    if (prefs->mqtt_user2[0] != 0) {
      strncpy(username2, prefs->mqtt_user2, sizeof(username2) - 1);
      username2[sizeof(username2) - 1] = 0;
    } else {
      username2[0] = 0;
    }
  #endif
  
  #ifdef MQTT_PASS2
    strncpy(password2, MQTT_PASS2, sizeof(password2) - 1);
    password2[sizeof(password2) - 1] = 0;
  #else
    if (prefs->mqtt_pass2[0] != 0) {
      strncpy(password2, prefs->mqtt_pass2, sizeof(password2) - 1);
      password2[sizeof(password2) - 1] = 0;
    } else {
      password2[0] = 0;
    }
  #endif
  
  // Set active server index from prefs (default to 0)
  active_server_index = prefs->mqtt_server_index;
  if (active_server_index > 1) active_server_index = 0;
  
  strncpy(topic_prefix, MQTT_TOPIC_PREFIX, sizeof(topic_prefix) - 1);
  topic_prefix[sizeof(topic_prefix) - 1] = 0;
  
  // Generate client ID
  snprintf(client_id, sizeof(client_id), "meshcore_%02X%02X%02X", 
           (unsigned int)prefs->node_name[0], 
           (unsigned int)prefs->node_name[1], 
           (unsigned int)prefs->node_name[2]);
}

void MQTTBridge::setupMQTTClient() {
  if (mqttClient == nullptr) {
    PubSubClient* client = new PubSubClient(wifiClient);
    client->setCallback(mqttCallback);
    mqttClient = client;
  }
  // Update server address based on active server
  PubSubClient* client = (PubSubClient*)mqttClient;
  client->setServer(getActiveBroker(), getActivePort());
}

const char* MQTTBridge::getActiveBroker() const {
  return active_server_index == 1 ? broker2 : broker;
}

uint16_t MQTTBridge::getActivePort() const {
  return active_server_index == 1 ? port2 : port;
}

const char* MQTTBridge::getActiveUsername() const {
  return active_server_index == 1 ? username2 : username;
}

const char* MQTTBridge::getActivePassword() const {
  return active_server_index == 1 ? password2 : password;
}

void MQTTBridge::switchToServer(uint8_t server_index) {
  if (server_index > 1) return;
  
  if (server_index != active_server_index) {
    // Disconnect from current server
    if (mqttClient && _connected) {
      ((PubSubClient*)mqttClient)->disconnect();
      _connected = false;
    }
    
    active_server_index = server_index;
    
    // Update prefs
    if (_prefs) {
      _prefs->mqtt_server_index = server_index;
    }
    
    // Reconfigure client
    setupMQTTClient();
    
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: Switched to server %d (%s:%d)\n", 
                     getLogDateTime(), server_index, getActiveBroker(), getActivePort());
    #endif
  }
}

void MQTTBridge::begin() {
  #if BRIDGE_DEBUG && ARDUINO
    Serial.printf("%s BRIDGE: Initializing MQTT bridge to %s:%d (server %d)...\n", 
                   getLogDateTime(), getActiveBroker(), getActivePort(), active_server_index);
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
  
  // Update server address in case it changed
  client->setServer(getActiveBroker(), getActivePort());
  
  // Try to connect
  bool connected = false;
  const char* user = getActiveUsername();
  const char* pass = getActivePassword();
  if (user[0] != 0) {
    connected = client->connect(client_id, user, pass);
  } else {
    connected = client->connect(client_id);
  }
  
  if (connected) {
    _connected = true;
    #if BRIDGE_DEBUG && ARDUINO
      Serial.printf("%s BRIDGE: MQTT connected\n", getLogDateTime());
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
  
  // Build topic based on packet type and channel
  char topic[128];
  uint8_t payload_type = packet->getPayloadType();
  
  // For group messages (GRP_TXT, GRP_DATA), use channel name or hash as topic suffix
  if (payload_type == PAYLOAD_TYPE_GRP_TXT || payload_type == PAYLOAD_TYPE_GRP_DATA) {
    if (packet->payload_len > 0) {
      // First byte of payload is channel hash
      uint8_t channel_hash = packet->payload[0];
      
      // Try to get channel name from callback
      const char* channel_name = nullptr;
      if (channel_name_callback) {
        channel_name = channel_name_callback(channel_hash, channel_name_user_data);
      }
      
      if (channel_name && channel_name[0] != 0) {
        // Use channel name in topic (sanitize for MQTT topic - replace invalid chars)
        char sanitized_name[64];
        int j = 0;
        for (int i = 0; channel_name[i] != 0 && j < sizeof(sanitized_name) - 1; i++) {
          char c = channel_name[i];
          // MQTT topic allows: A-Z, a-z, 0-9, and some special chars, but we'll keep it simple
          if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || 
              c == '-' || c == '_' || c == '.') {
            sanitized_name[j++] = c;
          } else if (c == ' ') {
            sanitized_name[j++] = '_';  // Replace spaces with underscores
          }
        }
        sanitized_name[j] = 0;
        snprintf(topic, sizeof(topic), "%s%s", topic_prefix, sanitized_name);
      } else {
        // Fallback to channel hash if name not available
        snprintf(topic, sizeof(topic), "%s%02X", topic_prefix, channel_hash);
      }
    } else {
      // Fallback if payload is empty
      snprintf(topic, sizeof(topic), "%sall", topic_prefix);
    }
  } else {
    // For other packet types, use "all" as suffix
    snprintf(topic, sizeof(topic), "%sall", topic_prefix);
  }
  
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
  // Incoming messages are discarded - bridge only sends packets to MQTT
  // This callback should not be called since we don't subscribe to any topics,
  // but keeping it for safety in case MQTT client receives unexpected messages
  #if BRIDGE_DEBUG && ARDUINO
    Serial.printf("%s BRIDGE: RX message discarded (bridge is send-only), topic=%s, len=%d\n", 
                   getLogDateTime(), topic, length);
  #endif
}

void MQTTBridge::onPacketReceived(mesh::Packet* packet) {
  // This is called from handleReceivedPacket in BridgeBase
  // The packet is already queued for mesh processing
}

#endif // WITH_MQTT_BRIDGE

