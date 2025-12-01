#pragma once

#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_MQTT_BRIDGE

#include <WiFi.h>

/**
 * @brief Bridge implementation using MQTT protocol for packet transport
 *
 * This bridge enables mesh packet transport over MQTT, allowing packets to be
 * forwarded to/from an MQTT broker over WiFi.
 *
 * Features:
 * - MQTT publish/subscribe for packet forwarding
 * - Network isolation using topic prefix
 * - Duplicate packet detection using SimpleMeshTables tracking
 * - Automatic reconnection to MQTT broker
 *
 * Packet Structure:
 * [2 bytes] Magic Header - Used to identify MQTTBridge packets
 * [2 bytes] Fletcher-16 checksum of payload
 * [N bytes] Mesh packet payload
 *
 * Configuration:
 * - Define WITH_MQTT_BRIDGE to enable this bridge
 * - Define MQTT_BROKER with broker hostname/IP
 * - Define MQTT_PORT with broker port (default 1883)
 * - Define MQTT_TOPIC_PREFIX for topic prefix (default "meshcore/")
 * - Optionally define MQTT_USER and MQTT_PASS for authentication
 */
class MQTTBridge : public BridgeBase {
private:
  WiFiClient wifiClient;
  void* mqttClient;  // PubSubClient instance (void* to avoid include dependency)
  
  // Server 1 (primary, from build flags)
  char broker[64];
  uint16_t port;
  char username[32];
  char password[32];
  
  // Server 2 (secondary, from prefs)
  char broker2[64];
  uint16_t port2;
  char username2[32];
  char password2[32];
  
  // Common settings
  char topic_prefix[64];
  char client_id[32];
  uint8_t active_server_index;  // 0 or 1
  
  bool _connected;
  unsigned long last_reconnect_attempt;
  unsigned long reconnect_interval;
  
  static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);
  static MQTTBridge* _instance;
  
  void reconnect();
  void setupMQTTClient();
  const char* getActiveBroker() const;
  uint16_t getActivePort() const;
  const char* getActiveUsername() const;
  const char* getActivePassword() const;
  
public:
  /**
   * @brief Constructs an MQTTBridge instance
   *
   * @param prefs Node preferences for configuration settings
   * @param mgr PacketManager for allocating and queuing packets
   * @param rtc RTCClock for timestamping debug messages
   */
  MQTTBridge(NodePrefs *prefs, mesh::PacketManager *mgr, mesh::RTCClock *rtc);

  /**
   * @brief Initializes the MQTT bridge
   *
   * Sets up WiFi connection (if not already connected) and connects to MQTT broker
   */
  void begin() override;

  /**
   * @brief Stops the MQTT bridge
   *
   * Disconnects from MQTT broker
   */
  void end() override;

  /**
   * @brief Gets the current state of the bridge
   *
   * @return true if connected to MQTT broker, false otherwise
   */
  bool isRunning() const override;

  /**
   * @brief A method to be called on every main loop iteration
   *
   * Handles MQTT client loop and reconnection logic
   */
  void loop() override;

  /**
   * @brief Publishes a mesh packet to MQTT broker
   *
   * @param packet The packet to publish
   */
  void sendPacket(mesh::Packet* packet) override;

  /**
   * @brief Processes a received packet from MQTT
   *
   * @param packet The packet that was received
   */
  void onPacketReceived(mesh::Packet* packet) override;
  
  /**
   * @brief Callback for received MQTT messages
   *
   * @param topic MQTT topic
   * @param payload Message payload
   * @param length Payload length
   */
  void onMessage(char* topic, uint8_t* payload, unsigned int length);
  
  /**
   * @brief Switch to a different MQTT server
   *
   * @param server_index 0 for primary server, 1 for secondary server
   */
  void switchToServer(uint8_t server_index);
  
  /**
   * @brief Get current active server index
   *
   * @return 0 or 1
   */
  uint8_t getActiveServerIndex() const { return active_server_index; }
};

#endif // WITH_MQTT_BRIDGE

