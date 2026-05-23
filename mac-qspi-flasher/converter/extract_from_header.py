#!/usr/bin/env python3
"""
Extract raw binary data from C header files with int16_t arrays.
Converts int16_t C arrays to little-endian binary files for QSPI flashing.
"""

import re
import struct
import sys
import os

def extract_to_bin(header_file, bin_file=None):
    """Extract int16 array from header and write as raw binary."""
    
    if bin_file is None:
        bin_file = header_file.replace('.h', '.bin')
    
    with open(header_file, 'r') as f:
        content = f.read()
    
    # Find the array data between braces
    match = re.search(r'\{([^}]+)\}', content, re.DOTALL)
    if not match:
        print(f"ERROR: Could not find array data in {header_file}")
        return False
    
    # Extract numbers
    data_str = match.group(1)
    # Remove comments and whitespace, then split by commas
    numbers = [int(x.strip()) for x in data_str.replace('\n', '').replace(' ', '').split(',') if x.strip()]
    
    # Write as little-endian int16 binary
    with open(bin_file, 'wb') as f:
        for val in numbers:
            f.write(struct.pack('<h', val))  # '<h' = little-endian signed short
    
    print(f"Extracted: {header_file} → {bin_file}")
    print(f"  {len(numbers)} samples, {len(numbers) * 2} bytes")
    return True

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Extract binary from C header int16_t arrays')
    parser.add_argument('header', help='Input .h file with int16_t array')
    parser.add_argument('output', nargs='?', help='Output .bin file (auto-named if omitted)')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.header):
        print(f"Error: {args.header} not found")
        sys.exit(1)
    
    success = extract_to_bin(args.header, args.output)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
