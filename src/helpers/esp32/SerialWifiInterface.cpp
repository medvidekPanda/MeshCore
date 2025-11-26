#include "SerialWifiInterface.h"
#include <WiFi.h>

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void SerialWifiInterface::begin(int port) {
  // wifi setup is handled outside of this class, only starts the server
  server.begin(port);
  _recv_state = RECV_STATE_IDLE;
  _frame_len = 0;
  rx_len = 0;
}

// ---------- public methods
void SerialWifiInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();
  _recv_state = RECV_STATE_IDLE;
  _frame_len = 0;
  rx_len = 0;
}

void SerialWifiInterface::disable() {
  _isEnabled = false;
}

size_t SerialWifiInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    WIFI_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d\n", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      WIFI_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

bool SerialWifiInterface::isWriteBusy() const {
  return false;
}

size_t SerialWifiInterface::checkRecvFrame(uint8_t dest[]) {
  // check if new client connected
  auto newClient = server.available();
  if (newClient) {

    // disconnect existing client
    deviceConnected = false;
    client.stop();

    // switch active connection to new client
    client = newClient;
    
  }

  if (client.connected()) {
    if (!deviceConnected) {
      WIFI_DEBUG_PRINTLN("Got connection");
      deviceConnected = true;
    }
  } else {
    if (deviceConnected) {
      deviceConnected = false;
      WIFI_DEBUG_PRINTLN("Disconnected");
    }
  }

  if (deviceConnected) {
    if (send_queue_len > 0) {   // first, check send queue
      
      _last_write = millis();
      int len = send_queue[0].len;

      uint8_t pkt[3+len]; // use same header as serial interface so client can delimit frames
      pkt[0] = '>';
      pkt[1] = (len & 0xFF);  // LSB
      pkt[2] = (len >> 8);    // MSB
      memcpy(&pkt[3], send_queue[0].buf, send_queue[0].len);
      client.write(pkt, 3 + len);
      send_queue_len--;
      for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
        send_queue[i] = send_queue[i + 1];
      }
    } else {
      // Parse frame protocol similar to ArduinoSerialInterface
      while (client.available()) {
        int c = client.read();
        if (c < 0) break;

        switch (_recv_state) {
          case RECV_STATE_IDLE:
            if (c == '<') {
              _recv_state = RECV_STATE_HDR_FOUND;
            }
            break;
          case RECV_STATE_HDR_FOUND:
            _frame_len = (uint8_t)c;   // LSB
            _recv_state = RECV_STATE_LEN1_FOUND;
            break;
          case RECV_STATE_LEN1_FOUND:
            _frame_len |= ((uint16_t)c) << 8;   // MSB
            rx_len = 0;
            _recv_state = _frame_len > 0 && _frame_len <= MAX_FRAME_SIZE ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
            break;
          default:
            if (rx_len < MAX_FRAME_SIZE) {
              recv_queue[0].buf[rx_len] = (uint8_t)c;   // rest of frame will be discarded if > MAX
            }
            rx_len++;
            if (rx_len >= _frame_len) {  // received a complete frame?
              if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;    // truncate
              memcpy(dest, recv_queue[0].buf, _frame_len);
              _recv_state = RECV_STATE_IDLE;  // reset state, for next frame
              return _frame_len;
            }
        }
      }
    }
  }

  return 0;
}

bool SerialWifiInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}