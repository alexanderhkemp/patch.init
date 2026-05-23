#!/usr/bin/env python3
"""
Mac QSPI Flasher Sender - sends audio files to Daisy QSPI Flasher client.
Uses pyserial to communicate with Daisy Seed USB CDC.
Protocol: Daisy sends "BLOC"+NumBloc, we respond with "BLOC"+NumBloc+CRC+Data+"END"
"""

import serial
import struct
import sys
import os
import time
import glob

# Protocol constants from Flasher.h
BLOCK_SIZE = 1024
BLOCKS_PER_PAGE = 4
PAGE_SIZE = 4096
QSPI_SIZE = 8 * 1024 * 1024  # 8MB
START_MARKER = b'BLOC'
END_MARKER = b'END'
MSG_SIZE = 6  # "BLOC" + uint16_t NumBloc

# Directory entry size (for tracking files)
MAX_FILENAME = 40
NUM_FILES = 20

def find_daisy_port():
    """Find Daisy Seed USB CDC port on Mac."""
    # Common Mac patterns for Daisy USB
    patterns = [
        '/dev/tty.usbmodem*',
        '/dev/tty.usbserial*',
        '/dev/cu.usbmodem*',
        '/dev/cu.usbserial*'
    ]
    
    for pattern in patterns:
        ports = glob.glob(pattern)
        if ports:
            return ports[0]  # Return first match
    
    return None

def calc_crc(data):
    """Calculate simple sum CRC like Daisy client expects."""
    return sum(data) & 0xFF

def send_file(ser, filepath, start_block=0):
    """Send a single file to QSPI."""
    filename = os.path.basename(filepath)
    filesize = os.path.getsize(filepath)
    
    print(f"\nSending: {filename} ({filesize} bytes)")
    print(f"  Starting at block {start_block}")
    
    with open(filepath, 'rb') as f:
        data = f.read()
    
    # Pad to block boundary
    padding = (BLOCK_SIZE - (len(data) % BLOCK_SIZE)) % BLOCK_SIZE
    data += b'\x00' * padding
    
    num_blocks = len(data) // BLOCK_SIZE
    print(f"  Total blocks: {num_blocks}")
    
    block_num = start_block
    errors = 0
    max_errors = 10
    
    while block_num < start_block + num_blocks:
        # Wait for request from Daisy
        msg = ser.read(MSG_SIZE)
        if len(msg) != MSG_SIZE:
            print(f"  Timeout waiting for request, retrying...")
            errors += 1
            if errors > max_errors:
                print("  Too many errors, aborting!")
                return False, block_num
            continue
        
        marker, requested_block = struct.unpack('<4sH', msg)
        
        if marker != START_MARKER:
            print(f"  Unexpected marker: {marker}")
            errors += 1
            continue
        
        # If Daisy requests a previous block, we had an error
        if requested_block < block_num:
            print(f"  Retransmitting block {requested_block}")
            block_num = requested_block
            errors = 0
        
        # Send the block
        offset = (block_num - start_block) * BLOCK_SIZE
        block_data = data[offset:offset + BLOCK_SIZE]
        crc = calc_crc(block_data)
        
        # Determine if this is the last block
        end_trans = 1 if (block_num == start_block + num_blocks - 1) else 0
        
        # Build packet: "BLOC" + NumBloc + CRC + EndTrans + Data + "END"
        packet = struct.pack('<4sHBB', START_MARKER, block_num, crc, end_trans)
        packet += block_data
        packet += END_MARKER
        
        ser.write(packet)
        ser.flush()
        
        block_num += 1
        errors = 0
        
        # Progress indicator
        if block_num % 10 == 0:
            progress = (block_num - start_block) / num_blocks * 100
            print(f"  Progress: {progress:.1f}%", end='\r')
    
    print(f"  Done! Sent {num_blocks} blocks")
    return True, block_num

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Flash audio files to Daisy QSPI')
    parser.add_argument('--port', help='Serial port (auto-detect if not specified)')
    parser.add_argument('files', nargs='+', help='Binary files to flash')
    parser.add_argument('--list', action='store_true', help='List available ports and exit')
    
    args = parser.parse_args()
    
    if args.list:
        print("Available serial ports:")
        for pattern in ['/dev/tty.usbmodem*', '/dev/tty.usbserial*', '/dev/cu.usbmodem*', '/dev/cu.usbserial*']:
            ports = glob.glob(pattern)
            for p in ports:
                print(f"  {p}")
        return
    
    # Find port
    port = args.port
    if not port:
        port = find_daisy_port()
        if not port:
            print("Error: Could not find Daisy port automatically.")
            print("Use --list to see available ports, or specify with --port")
            sys.exit(1)
    
    print(f"Using port: {port}")
    
    # Open serial port
    try:
        ser = serial.Serial(port, 115200, timeout=5)
    except serial.SerialException as e:
        print(f"Error opening port: {e}")
        sys.exit(1)
    
    print("Waiting for Daisy QSPI Flasher client...")
    print("(Make sure you've flashed the FlasherClient to your Daisy)")
    print()
    
    # Wait for first message from Daisy
    print("Waiting for handshake...")
    msg = ser.read(MSG_SIZE)
    if len(msg) != MSG_SIZE:
        print("Error: No response from Daisy. Is the FlasherClient running?")
        ser.close()
        sys.exit(1)
    
    marker, block = struct.unpack('<4sH', msg)
    if marker != START_MARKER:
        print(f"Error: Invalid handshake from Daisy: {marker}")
        ser.close()
        sys.exit(1)
    
    print(f"Daisy ready, requesting block {block}")
    print()
    
    # Send directory header first (blank for now, or populate with file info)
    # Actually, let's skip directory and just send raw files back-to-back
    # The user's firmware will need to know the layout
    
    current_block = 0
    file_layout = []
    
    for filepath in args.files:
        if not os.path.exists(filepath):
            print(f"Warning: {filepath} not found, skipping")
            continue
        
        success, next_block = send_file(ser, filepath, current_block)
        
        if not success:
            print(f"Failed to send {filepath}")
            break
        
        file_layout.append({
            'name': os.path.basename(filepath),
            'start_block': current_block,
            'end_block': next_block - 1,
            'size': os.path.getsize(filepath)
        })
        
        current_block = next_block
        print()
    
    print("Flashing complete!")
    print("\nFile layout in QSPI:")
    for f in file_layout:
        print(f"  {f['name']}: blocks {f['start_block']}-{f['end_block']} ({f['size']} bytes)")
    
    ser.close()

if __name__ == '__main__':
    main()
