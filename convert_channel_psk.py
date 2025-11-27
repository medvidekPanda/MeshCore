#!/usr/bin/env python3
"""
Helper script to convert channel PSK from app format to base64 format
for use in platformio.ini

Usage:
  python3 convert_channel_psk.py <psk_from_app>

The PSK from app might be:
  - Hex string (32 characters for 16 bytes)
  - Base64 string (24 characters for 16 bytes)
  - URL-encoded (from meshcore://channel/add?secret=...)

This script will detect the format and convert to base64.
"""

import base64
import sys
import urllib.parse

def detect_and_convert_psk(psk_input):
    """Detect PSK format and convert to base64"""
    # Remove URL encoding if present
    psk = urllib.parse.unquote(psk_input)
    
    # Remove whitespace
    psk = psk.strip()
    
    print(f"Input PSK: {psk}")
    print(f"Length: {len(psk)} characters\n")
    
    # Try to detect format
    if len(psk) == 32 and all(c in '0123456789abcdefABCDEF' for c in psk):
        # Hex format (32 chars = 16 bytes)
        print("Detected format: HEX (32 characters)")
        try:
            decoded = bytes.fromhex(psk)
            base64_psk = base64.b64encode(decoded).decode('ascii')
            print(f"✓ Converted to base64: {base64_psk}")
            return base64_psk
        except Exception as e:
            print(f"✗ Error converting hex: {e}")
            return None
            
    elif len(psk) == 24 or (len(psk) == 22 and psk.endswith('==')):
        # Base64 format (24 chars for 16 bytes, or 22+padding)
        print("Detected format: BASE64")
        try:
            # Verify it's valid base64
            decoded = base64.b64decode(psk)
            if len(decoded) == 16:
                print(f"✓ Valid base64 (16 bytes)")
                print(f"  Hex representation: {decoded.hex()}")
                return psk
            else:
                print(f"✗ Invalid length: {len(decoded)} bytes (expected 16)")
                return None
        except Exception as e:
            print(f"✗ Invalid base64: {e}")
            return None
    else:
        print("✗ Unknown format")
        print("Expected:")
        print("  - Hex string: 32 characters (0-9, a-f, A-F)")
        print("  - Base64 string: 24 characters (or 22 with == padding)")
        return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 convert_channel_psk.py <psk_from_app>")
        print("\nExample:")
        print("  python3 convert_channel_psk.py a801b6bfc10957ab20b25e4df66abd4b")
        print("  python3 convert_channel_psk.py qAG2v8EJV6sgsl5N9mq9Sw==")
        sys.exit(1)
    
    psk_input = sys.argv[1]
    base64_psk = detect_and_convert_psk(psk_input)
    
    if base64_psk:
        print("\n" + "="*60)
        print("For platformio.ini, use:")
        print(f'  -D SENSOR_CHANNEL_PSK=\'"{base64_psk}"\'')
        print("="*60)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()

