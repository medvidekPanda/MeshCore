#pragma once

#include "helpers/bridges/BridgeBase.h"

#ifdef WITH_MQTT_BRIDGE

#include <WiFi.h>

/**
 * @brief Bridge implementation using MQTT protocol for packet transport
 *
 * This bridge enables mesh packet transport over MQTT, forwarding all mesh
 * packets to an MQTT broker over WiFi. Acts as a send-only proxy.
 *
 * Features:
 * - MQTT publish for packet forwarding (send-only mode)
 * - All packets are forwarded (proxy mode)
 * - Topic structure: meshcore/{channel_hash} for group messages, meshcore/all for others
 * - Network isolation using topic prefix
 * - Automatic reconnection to MQTT broker
 * - Incoming MQTT messages are discarded
 *
 * Topic Structure:
 * - Group messages (GRP_TXT, GRP_DATA): meshcore/{channel_name} or meshcore/{channel_hash}
 *   - If channel name callback is set and returns a name, uses channel name (e.g., meshcore/MyChannel)
 *   - Otherwise falls back to channel hash in hex (e.g., meshcore/D5)
 * - Other packet types: meshcore/all
 * - No subscription - bridge only sends, does not receive
 * 
 * Channel Name Resolution:
 * - Use setChannelNameCallback() to provide a function that maps channel hash to channel name
 * - If callback is not set or returns NULL, channel hash (hex) is used in topic
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
// Callback function type for getting channel name from channel hash
// Returns channel name if found, NULL otherwise
typedef const char* (*ChannelNameCallback)(uint8_t channel_hash, void* user_data);

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
  
  // Callback for getting channel name from hash
  ChannelNameCallback channel_name_callback;
  void* channel_name_user_data;
  
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
  
  /**
   * @brief Set callback function for getting channel name from channel hash
   *
   * @param callback Function that returns channel name for given hash, or NULL if not found
   * @param user_data User data passed to callback
   */
  void setChannelNameCallback(ChannelNameCallback callback, void* user_data = nullptr) {
    channel_name_callback = callback;
    channel_name_user_data = user_data;
  }
};

#endif // WITH_MQTT_BRIDGE

