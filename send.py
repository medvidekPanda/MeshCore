#!/usr/bin/env python3
"""
Script for sending messages and telemetry data to mesh network via MQTT
"""

import paho.mqtt.client as mqtt
import struct
import sys
import time

def fletcher16(data):
    """Fletcher16 checksum calculation"""
    sum1 = 0
    sum2 = 0
    for byte in data:
        sum1 = (sum1 + byte) % 255
        sum2 = (sum2 + sum1) % 255
    return (sum2 << 8) | sum1

# Payload types
PAYLOAD_TYPE_RAW_CUSTOM = 0x0F
PAYLOAD_TYPE_CONTROL = 0x0B

# Route types
ROUTE_TYPE_FLOOD = 0x01
ROUTE_TYPE_DIRECT = 0x02

def create_mesh_packet(payload_type, route_type, path, payload_data):
    """Create a mesh packet with given parameters"""
    packet = bytearray()
    
    # Header byte: payload_type (4 bits) + route_type (2 bits) + version (2 bits, set to 0)
    header = (payload_type << 2) | (route_type & 0x03)
    packet.append(header)
    
    # Path length and path
    if path is None:
        path = b''
    packet.append(len(path))
    packet.extend(path)
    
    # Payload
    if payload_data is None:
        payload_data = b''
    packet.extend(payload_data)
    
    return bytes(packet)

def encode_for_mqtt(packet_data):
    """Encode mesh packet for MQTT (add magic header and checksum)"""
    buffer = bytearray(4 + len(packet_data))
    
    # Magic header
    buffer[0] = 0xC0
    buffer[1] = 0x3E
    
    # Calculate checksum
    checksum = fletcher16(packet_data)
    buffer[2] = checksum & 0xFF
    buffer[3] = (checksum >> 8) & 0xFF
    
    # Packet data
    buffer[4:] = packet_data
    
    return bytes(buffer)

def send_packet(client, packet_data, topic="meshcore/rx"):
    """Send a mesh packet to MQTT broker"""
    mqtt_payload = encode_for_mqtt(packet_data)
    result = client.publish(topic, mqtt_payload)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"✓ Packet sent to {topic} ({len(mqtt_payload)} bytes)")
        return True
    else:
        print(f"✗ Failed to send packet: {result.rc}")
        return False

def send_raw_data(client, data, route_type=ROUTE_TYPE_FLOOD):
    """Send raw custom data packet"""
    packet = create_mesh_packet(PAYLOAD_TYPE_RAW_CUSTOM, route_type, None, data)
    return send_packet(client, packet)

def send_control_data(client, data, route_type=ROUTE_TYPE_FLOOD):
    """Send control/discovery data packet"""
    packet = create_mesh_packet(PAYLOAD_TYPE_CONTROL, route_type, None, data)
    return send_packet(client, packet)

def send_telemetry(client, sensor_name, value, unit=""):
    """Send telemetry data as control packet"""
    # Format: "sensor_name:value:unit"
    data = f"{sensor_name}:{value}:{unit}".encode('utf-8')
    return send_control_data(client, data)

def main():
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python3 send.py <message>")
        print("  python3 send.py --telemetry <sensor> <value> [unit]")
        print("  python3 send.py --raw <hex_data>")
        print()
        print("Examples:")
        print("  python3 send.py 'Hello mesh!'")
        print("  python3 send.py --telemetry temperature 23.5 C")
        print("  python3 send.py --telemetry humidity 65 %")
        print("  python3 send.py --raw 48656c6c6f")  # "Hello" in hex
        sys.exit(1)
    
    broker = "10.40.196.82"
    port = 41883
    topic = "meshcore/rx"
    
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    
    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            print(f"✓ Connected to MQTT broker at {broker}:{port}")
        else:
            print(f"✗ Failed to connect: {reason_code}")
            sys.exit(1)
    
    client.on_connect = on_connect
    client.connect(broker, port, 60)
    client.loop_start()
    
    # Wait for connection
    time.sleep(1)
    
    if sys.argv[1] == "--telemetry":
        if len(sys.argv) < 4:
            print("Error: --telemetry requires sensor name and value")
            sys.exit(1)
        sensor = sys.argv[2]
        value = sys.argv[3]
        unit = sys.argv[4] if len(sys.argv) > 4 else ""
        send_telemetry(client, sensor, value, unit)
    elif sys.argv[1] == "--raw":
        if len(sys.argv) < 3:
            print("Error: --raw requires hex data")
            sys.exit(1)
        try:
            hex_data = sys.argv[2].replace(" ", "")
            data = bytes.fromhex(hex_data)
            send_raw_data(client, data)
        except ValueError as e:
            print(f"Error: Invalid hex data: {e}")
            sys.exit(1)
    else:
        # Send as text message (raw data)
        message = " ".join(sys.argv[1:])
        send_raw_data(client, message.encode('utf-8'))
    
    time.sleep(0.5)  # Wait for message to be sent
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()

