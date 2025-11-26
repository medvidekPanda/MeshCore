import paho.mqtt.client as mqtt
import struct
from datetime import datetime
import hashlib
import hmac
from Crypto.Cipher import AES
from Crypto.Util.Padding import unpad

def fletcher16(data):
    """Fletcher16 checksum calculation"""
    sum1 = 0
    sum2 = 0
    for byte in data:
        sum1 = (sum1 + byte) % 255
        sum2 = (sum2 + sum1) % 255
    return (sum2 << 8) | sum1

# Payload types
PAYLOAD_TYPE_REQ = 0x00
PAYLOAD_TYPE_RESPONSE = 0x01
PAYLOAD_TYPE_TXT_MSG = 0x02
PAYLOAD_TYPE_ACK = 0x03
PAYLOAD_TYPE_ADVERT = 0x04
PAYLOAD_TYPE_GRP_TXT = 0x05
PAYLOAD_TYPE_GRP_DATA = 0x06
PAYLOAD_TYPE_ANON_REQ = 0x07
PAYLOAD_TYPE_PATH = 0x08
PAYLOAD_TYPE_TRACE = 0x09
PAYLOAD_TYPE_MULTIPART = 0x0A
PAYLOAD_TYPE_CONTROL = 0x0B
PAYLOAD_TYPE_RAW_CUSTOM = 0x0F

PAYLOAD_TYPE_NAMES = {
    PAYLOAD_TYPE_REQ: "REQ",
    PAYLOAD_TYPE_RESPONSE: "RESPONSE",
    PAYLOAD_TYPE_TXT_MSG: "TXT_MSG",
    PAYLOAD_TYPE_ACK: "ACK",
    PAYLOAD_TYPE_ADVERT: "ADVERT",
    PAYLOAD_TYPE_GRP_TXT: "GRP_TXT",
    PAYLOAD_TYPE_GRP_DATA: "GRP_DATA",
    PAYLOAD_TYPE_ANON_REQ: "ANON_REQ",
    PAYLOAD_TYPE_PATH: "PATH",
    PAYLOAD_TYPE_TRACE: "TRACE",
    PAYLOAD_TYPE_MULTIPART: "MULTIPART",
    PAYLOAD_TYPE_CONTROL: "CONTROL",
    PAYLOAD_TYPE_RAW_CUSTOM: "RAW_CUSTOM"
}

# Route types
ROUTE_TYPE_TRANSPORT_FLOOD = 0x00
ROUTE_TYPE_FLOOD = 0x01
ROUTE_TYPE_DIRECT = 0x02
ROUTE_TYPE_TRANSPORT_DIRECT = 0x03

ROUTE_TYPE_NAMES = {
    ROUTE_TYPE_TRANSPORT_FLOOD: "TRANSPORT_FLOOD",
    ROUTE_TYPE_FLOOD: "FLOOD",
    ROUTE_TYPE_DIRECT: "DIRECT",
    ROUTE_TYPE_TRANSPORT_DIRECT: "TRANSPORT_DIRECT"
}

def decode_mesh_packet(payload):
    """Decode mesh packet from MQTT payload"""
    if len(payload) < 4:
        return None
    
    # Check magic
    magic = struct.unpack('>H', payload[0:2])[0]
    if magic != 0xC03E:
        print(f"Invalid magic: 0x{magic:04X}")
        return None
    
    # Verify checksum
    received_checksum = struct.unpack('<H', payload[2:4])[0]
    packet_data = payload[4:]
    calculated_checksum = fletcher16(packet_data)
    
    if received_checksum != calculated_checksum:
        print(f"Checksum mismatch: received=0x{received_checksum:04X}, calculated=0x{calculated_checksum:04X}")
        return None
    
    # Parse mesh packet
    if len(packet_data) < 1:
        return None
    
    header = packet_data[0]
    route_type = header & 0x03
    payload_type = (header >> 2) & 0x0F
    payload_ver = (header >> 6) & 0x03
    has_transport = (header & 0x80) != 0
    
    offset = 1
    
    transport_codes = None
    if has_transport:
        if len(packet_data) < offset + 4:
            return None
        transport_codes = struct.unpack('<HH', packet_data[offset:offset+4])
        offset += 4
    
    if len(packet_data) < offset + 1:
        return None
    
    path_len = packet_data[offset]
    offset += 1
    
    if len(packet_data) < offset + path_len:
        return None
    
    path = packet_data[offset:offset+path_len]
    offset += path_len
    
    payload_data = packet_data[offset:]
    
    return {
        'header': header,
        'route_type': route_type,
        'payload_type': payload_type,
        'payload_ver': payload_ver,
        'has_transport': has_transport,
        'transport_codes': transport_codes,
        'path': path,
        'path_len': path_len,
        'payload': payload_data,
        'payload_len': len(payload_data)
    }

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print("✓ Connected to MQTT broker successfully!")
    else:
        print(f"✗ Failed to connect to MQTT broker. Reason code: {reason_code}")

def on_subscribe(client, userdata, mid, reason_codes, properties):
    print("✓ Subscribed to topics")
    for topic, reason_code in zip(["meshcore/tx"], reason_codes):
        if reason_code == 0:
            print(f"  - {topic}: OK")
        else:
            print(f"  - {topic}: Failed (reason code: {reason_code})")

def on_disconnect(client, userdata, reason_code, properties):
    print(f"✗ Disconnected from MQTT broker. Reason code: {reason_code}")

def extract_readable_text(data):
    """Try to extract readable ASCII text from binary data"""
    readable = []
    for byte in data:
        if 32 <= byte <= 126:  # Printable ASCII
            readable.append(chr(byte))
        elif byte == 0:
            readable.append('\\0')
        else:
            readable.append('.')
    return ''.join(readable)

# Global secret for decryption (set by user)
DECRYPT_SECRET = None

def set_decrypt_secret(secret_hex):
    """Set the secret key for decryption (32 bytes hex string)"""
    global DECRYPT_SECRET
    # Accept any length and pad/truncate to 32 bytes
    secret_bytes = bytes.fromhex(secret_hex)
    if len(secret_bytes) < 32:
        # Pad with zeros if shorter
        DECRYPT_SECRET = secret_bytes + b'\x00' * (32 - len(secret_bytes))
        print(f"✓ Decrypt secret set (padded to 32 bytes): {secret_hex}...")
    elif len(secret_bytes) > 32:
        # Truncate if longer
        DECRYPT_SECRET = secret_bytes[:32]
        print(f"✓ Decrypt secret set (truncated to 32 bytes): {secret_hex[:64]}...")
    else:
        DECRYPT_SECRET = secret_bytes
        print(f"✓ Decrypt secret set: {secret_hex[:16]}...")

def mac_then_decrypt(shared_secret, encrypted_data):
    """Decrypt data using MAC-then-decrypt scheme (AES-128-ECB with 2-byte HMAC)"""
    CIPHER_MAC_SIZE = 2
    CIPHER_BLOCK_SIZE = 16
    PUB_KEY_SIZE = 32
    
    if len(encrypted_data) <= CIPHER_MAC_SIZE:
        return None
    
    # Extract MAC and encrypted payload
    mac = encrypted_data[:CIPHER_MAC_SIZE]
    ciphertext = encrypted_data[CIPHER_MAC_SIZE:]
    
    # Verify MAC (HMAC-SHA256, first 2 bytes)
    # Note: MeshCore uses PUB_KEY_SIZE (32 bytes) for HMAC key, not just 16
    h = hmac.new(shared_secret[:PUB_KEY_SIZE], ciphertext, hashlib.sha256)
    calculated_mac = h.digest()[:CIPHER_MAC_SIZE]
    
    if mac != calculated_mac:
        return None  # MAC verification failed
    
    # Decrypt using AES-128-ECB (MeshCore uses ECB block mode, not CBC)
    if len(ciphertext) % CIPHER_BLOCK_SIZE != 0:
        return None
    
    try:
        # Use first 16 bytes of secret for AES-128 key
        cipher = AES.new(shared_secret[:16], AES.MODE_ECB)
        decrypted = cipher.decrypt(ciphertext)
        
        # Remove padding (PKCS7)
        padding_len = decrypted[-1]
        if padding_len > CIPHER_BLOCK_SIZE or padding_len == 0:
            # Try to find null terminator or valid text
            return decrypted.rstrip(b'\x00')
        
        # Verify padding
        if all(decrypted[-i] == padding_len for i in range(1, padding_len + 1)):
            return decrypted[:-padding_len]
        else:
            # Return without padding removal if padding is invalid
            return decrypted.rstrip(b'\x00')
    except Exception as e:
        return None

def try_decrypt_group_packet(payload, payload_type):
    """Try to decrypt GRP_TXT or GRP_DATA packet"""
    if not DECRYPT_SECRET or len(payload) < 3:  # channel_hash + MAC
        return None
    
    # Skip channel_hash (1 byte), rest is MAC + encrypted data
    encrypted_data = payload[1:]
    return mac_then_decrypt(DECRYPT_SECRET, encrypted_data)

def try_decrypt_peer_packet(payload, payload_type):
    """Try to decrypt TXT_MSG, REQ, RESPONSE, or PATH packet"""
    if not DECRYPT_SECRET or len(payload) < 4:  # dest_hash + src_hash + MAC
        return None
    
    # Skip dest_hash (1 byte) and src_hash (1 byte), rest is MAC + encrypted data
    encrypted_data = payload[2:]
    return mac_then_decrypt(DECRYPT_SECRET, encrypted_data)

def decode_group_message(data, payload_type):
    """Decode decrypted group message"""
    if payload_type == PAYLOAD_TYPE_GRP_TXT and len(data) >= 5:
        # Format: timestamp (4 bytes) + flags (1 byte) + "name: message"
        timestamp = struct.unpack('<I', data[:4])[0]
        flags = data[4]
        txt_type = (flags >> 2) & 0x3F
        
        if txt_type == 0:  # Plain text
            message = data[5:].decode('utf-8', errors='ignore')
            dt = datetime.fromtimestamp(timestamp) if timestamp > 0 else None
            if dt:
                print(f"    Timestamp: {timestamp} ({dt.strftime('%Y-%m-%d %H:%M:%S')})")
            else:
                print(f"    Timestamp: {timestamp}")
            print(f"    Message: {message}")

def decode_peer_message(data, payload_type):
    """Decode decrypted peer message"""
    if payload_type == PAYLOAD_TYPE_TXT_MSG and len(data) >= 5:
        # Format: timestamp (4 bytes) + flags (1 byte) + text
        timestamp = struct.unpack('<I', data[:4])[0]
        flags = data[4]
        text = data[5:].decode('utf-8', errors='ignore').rstrip('\x00')
        dt = datetime.fromtimestamp(timestamp) if timestamp > 0 else None
        if dt:
            print(f"    Timestamp: {timestamp} ({dt.strftime('%Y-%m-%d %H:%M:%S')})")
        else:
            print(f"    Timestamp: {timestamp}")
        print(f"    Message: {text}")
    elif payload_type == PAYLOAD_TYPE_REQ:
        if len(data) >= 4:
            tag = struct.unpack('<I', data[:4])[0]
            print(f"    Request tag: 0x{tag:08X}")
            if len(data) > 4:
                print(f"    Request data: {data[4:].hex()}")
    elif payload_type == PAYLOAD_TYPE_RESPONSE:
        if len(data) >= 4:
            tag = struct.unpack('<I', data[:4])[0]
            print(f"    Response tag: 0x{tag:08X}")
            if len(data) > 4:
                print(f"    Response data: {data[4:].hex()}")

def decode_advert_packet(payload):
    """Decode ADVERT packet - contains identity, timestamp, signature, and app_data"""
    PUB_KEY_SIZE = 32
    SIGNATURE_SIZE = 64
    
    if len(payload) < PUB_KEY_SIZE + 4 + SIGNATURE_SIZE:
        return None
    
    offset = 0
    pub_key = payload[offset:offset+PUB_KEY_SIZE]
    offset += PUB_KEY_SIZE
    
    timestamp = struct.unpack('<I', payload[offset:offset+4])[0]
    offset += 4
    
    signature = payload[offset:offset+SIGNATURE_SIZE]
    offset += SIGNATURE_SIZE
    
    # Decode app_data
    app_data = payload[offset:]
    if len(app_data) < 1:
        return {
            'pub_key': pub_key.hex(),
            'timestamp': timestamp,
            'signature': signature.hex(),
            'type': None,
            'name': None,
            'lat': None,
            'lon': None
        }
    
    flags = app_data[0]
    adv_type = flags & 0x0F
    has_latlon = (flags & 0x10) != 0
    has_feat1 = (flags & 0x20) != 0
    has_feat2 = (flags & 0x40) != 0
    has_name = (flags & 0x80) != 0
    
    i = 1
    lat = lon = None
    extra1 = extra2 = None
    
    if has_latlon and len(app_data) >= i + 8:
        lat = struct.unpack('<i', app_data[i:i+4])[0] / 1e6
        lon = struct.unpack('<i', app_data[i+4:i+8])[0] / 1e6
        i += 8
    
    if has_feat1 and len(app_data) >= i + 2:
        extra1 = struct.unpack('<H', app_data[i:i+2])[0]
        i += 2
    
    if has_feat2 and len(app_data) >= i + 2:
        extra2 = struct.unpack('<H', app_data[i:i+2])[0]
        i += 2
    
    name = None
    if has_name and len(app_data) > i:
        name_bytes = app_data[i:]
        # Find null terminator or end of data
        name_len = 0
        for b in name_bytes:
            if b == 0:
                break
            name_len += 1
        if name_len > 0:
            name = name_bytes[:name_len].decode('utf-8', errors='ignore')
    
    type_names = {
        0: "NONE",
        1: "CHAT",
        2: "REPEATER",
        3: "ROOM",
        4: "SENSOR"
    }
    
    return {
        'pub_key': pub_key.hex(),
        'timestamp': timestamp,
        'signature': signature.hex()[:16] + '...',  # Truncate for display
        'type': type_names.get(adv_type, f"UNKNOWN({adv_type})"),
        'name': name,
        'lat': lat,
        'lon': lon,
        'extra1': extra1,
        'extra2': extra2
    }

def on_message(client, userdata, message):
    print(f"\n=== Message on {message.topic} ===")
    print(f"Total length: {len(message.payload)} bytes")
    
    packet = decode_mesh_packet(message.payload)
    if packet:
        payload_type_name = PAYLOAD_TYPE_NAMES.get(packet['payload_type'], f"UNKNOWN(0x{packet['payload_type']:X})")
        route_type_name = ROUTE_TYPE_NAMES.get(packet['route_type'], f"UNKNOWN(0x{packet['route_type']:X})")
        
        print(f"Header: 0x{packet['header']:02X}")
        print(f"  Route type: {route_type_name} (0x{packet['route_type']:X})")
        print(f"  Payload type: {payload_type_name} (0x{packet['payload_type']:X})")
        print(f"  Payload version: {packet['payload_ver']}")
        print(f"  Has transport codes: {packet['has_transport']}")
        if packet['transport_codes']:
            print(f"  Transport codes: {packet['transport_codes']}")
        print(f"Path length: {packet['path_len']}")
        if packet['path_len'] > 0:
            print(f"Path: {packet['path'].hex()}")
        print(f"Payload length: {packet['payload_len']} bytes")
        
        # Try to decode based on payload type
        if packet['payload_type'] == PAYLOAD_TYPE_ADVERT:
            advert = decode_advert_packet(packet['payload'])
            if advert:
                print(f"  Identity (pub_key): {advert['pub_key']}")
                print(f"  Type: {advert['type']}")
                if advert['timestamp']:
                    dt = datetime.fromtimestamp(advert['timestamp'])
                    print(f"  Timestamp: {advert['timestamp']} ({dt.strftime('%Y-%m-%d %H:%M:%S')})")
                if advert['name']:
                    print(f"  Name: {advert['name']}")
                if advert['lat'] is not None and advert['lon'] is not None and (advert['lat'] != 0 or advert['lon'] != 0):
                    print(f"  Location: {advert['lat']:.6f}, {advert['lon']:.6f}")
        elif packet['payload_type'] in [PAYLOAD_TYPE_GRP_TXT, PAYLOAD_TYPE_GRP_DATA]:
            # Group messages are encrypted
            if len(packet['payload']) >= 1:
                channel_hash = packet['payload'][0]
                print(f"  Channel hash: 0x{channel_hash:02X}")
                # Try to decrypt if secret is available
                decrypted = try_decrypt_group_packet(packet['payload'], packet['payload_type'])
                if decrypted:
                    print(f"  ✓ Decrypted successfully!")
                    decode_group_message(decrypted, packet['payload_type'])
                else:
                    print(f"  ⚠ Encrypted data (requires channel secret to decrypt)")
        elif packet['payload_type'] in [PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_PATH]:
            # These are encrypted with shared secret
            if DECRYPT_SECRET:
                decrypted = try_decrypt_peer_packet(packet['payload'], packet['payload_type'])
                if decrypted:
                    print(f"  ✓ Decrypted successfully!")
                    decode_peer_message(decrypted, packet['payload_type'])
                else:
                    print(f"  ✗ Decryption failed (wrong secret or invalid MAC)")
            else:
                print(f"  ⚠ Encrypted data (requires shared secret to decrypt)")
        elif packet['payload_type'] == PAYLOAD_TYPE_ACK:
            # ACK packets are not encrypted
            if len(packet['payload']) >= 4:
                ack_crc = struct.unpack('<I', packet['payload'][:4])[0]
                print(f"  ACK CRC: 0x{ack_crc:08X}")
                if advert['extra1'] is not None:
                    print(f"  Extra1: {advert['extra1']}")
                if advert['extra2'] is not None:
                    print(f"  Extra2: {advert['extra2']}")
        
        # Show payload as hex (first 128 bytes if long) - only if not already decoded
        if packet['payload_type'] not in [PAYLOAD_TYPE_ADVERT, PAYLOAD_TYPE_ACK]:
            if packet['payload_len'] <= 128:
                print(f"Payload (hex): {packet['payload'].hex()}")
            else:
                print(f"Payload (hex, first 128 bytes): {packet['payload'][:128].hex()}...")
            
            # Try to extract readable text (only for unencrypted types)
            if packet['payload_type'] in [PAYLOAD_TYPE_CONTROL, PAYLOAD_TYPE_RAW_CUSTOM]:
                readable = extract_readable_text(packet['payload'])
                if readable.strip() and len([c for c in readable if c.isprintable() and c != '.']) > 5:
                    print(f"Payload (readable): {readable}")
    else:
        print("Failed to decode packet")

def create_mesh_packet(payload_type, route_type, path, payload_data):
    """Create a mesh packet with given parameters"""
    # Build packet structure
    packet = bytearray()
    
    # Header byte
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
    # Create buffer with space for header
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

def send_mqtt_packet(client, packet_data, topic="meshcore/rx"):
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
    return send_mqtt_packet(client, packet)

def send_control_data(client, data, route_type=ROUTE_TYPE_FLOOD):
    """Send control/discovery data packet"""
    packet = create_mesh_packet(PAYLOAD_TYPE_CONTROL, route_type, None, data)
    return send_mqtt_packet(client, packet)

def send_text_message(client, text, dest_hash=None, src_hash=None, route_type=ROUTE_TYPE_FLOOD):
    """
    Send a simple text message (simplified - without encryption)
    Note: This is a simplified version. Real TXT_MSG requires encryption and shared secrets.
    For testing, use RAW_CUSTOM or CONTROL packets instead.
    """
    # For now, just send as RAW_CUSTOM
    print("Note: Using RAW_CUSTOM packet. Real TXT_MSG requires encryption.")
    return send_raw_data(client, text.encode('utf-8'), route_type)

# Set decrypt secret if provided
import sys

# Default secret (change this to your channel secret)
DEFAULT_SECRET = "cd95890fe082b80c6f2c2cd06d6fdf9b"  # Your channel secret

if len(sys.argv) > 1:
    secret = sys.argv[1]
    set_decrypt_secret(secret)
elif DEFAULT_SECRET:
    print(f"Using default decrypt secret: {DEFAULT_SECRET[:16]}...")
    set_decrypt_secret(DEFAULT_SECRET)
else:
    print("No decrypt secret provided. Use: python3 read.py <secret_hex>")
    print("Example: python3 read.py cd95890fe082b80c6f2c2cd06d6fdf9b...")
    print()

# Connect to MQTT broker
print("Connecting to MQTT broker at 10.40.196.82:41883...")
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_subscribe = on_subscribe
client.on_disconnect = on_disconnect
client.on_message = on_message

client.connect("10.40.196.82", 41883, 60)
client.subscribe("meshcore/tx")  # Subscribe to outgoing packets
# client.subscribe("meshcore/rx")  # Or subscribe to incoming packets

print("Waiting for messages...")
print("\nTo send data, use:")
print("  send_raw_data(client, b'your data')")
print("  send_control_data(client, b'telemetry data')")
print("  send_text_message(client, 'Hello mesh!')")
print("\nOr import functions and use in your code:")
print("  from read import send_raw_data, send_control_data")
print()

# Start loop in background to allow sending messages
client.loop_start()

# Keep script running
try:
    while True:
        import time
        time.sleep(1)
except KeyboardInterrupt:
    print("\nDisconnecting...")
    client.loop_stop()
    client.disconnect()