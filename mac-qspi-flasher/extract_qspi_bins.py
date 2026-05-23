#!/usr/bin/env python3
"""
Extract raw binary audio data from QSPI header files for DADDesign QSPI Flasher.
Converts int16_t C arrays to little-endian .bin files.
"""

import re
import struct
import os

noise_files = [
    "noise_tape_1.h",
    "noise_vinyl_1.h",
    "noise_tape_2.h",
    "noise_vinyl_2.h",
    "noise_tape_3.h",
    "noise_vinyl_3.h",
]

def extract_to_bin(header_file):
    """Extract int16 array from header and write as raw binary."""
    bin_file = header_file.replace(".h", ".bin")
    
    with open(header_file, 'r') as f:
        content = f.read()
    
    # Find the array data between braces
    match = re.search(r'\{([^}]+)\}', content, re.DOTALL)
    if not match:
        print(f"  ERROR: Could not find array data in {header_file}")
        return
    
    # Extract numbers
    data_str = match.group(1)
    # Remove comments and whitespace, then split by commas
    numbers = [int(x.strip()) for x in data_str.replace('\n', '').replace(' ', '').split(',') if x.strip()]
    
    # Write as little-endian int16 binary
    with open(bin_file, 'wb') as f:
        for val in numbers:
            f.write(struct.pack('<h', val))  # '<h' = little-endian signed short
    
    print(f"  {header_file} → {bin_file} ({len(numbers) * 2} bytes)")

print("Extracting QSPI binary files...\n")
for header in noise_files:
    if os.path.exists(header):
        extract_to_bin(header)
    else:
        print(f"  WARNING: {header} not found")

print("\nDone! Use these .bin files with DADDesign QSPI Flasher:")
print("  1. Flash firmware via web programmer (or dfu-util -a 0)")
print("  2. Run DADDesign QSPI Flasher client on Daisy")
print("  3. Use FlasherServer.exe to upload each .bin to QSPI")
