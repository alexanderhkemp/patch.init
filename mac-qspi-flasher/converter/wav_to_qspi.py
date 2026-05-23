#!/usr/bin/env python3
"""
Convert WAV files to C float arrays for SDRAM embedding.
Expects 16-bit/48kHz stereo WAV files, outputs 5-second loops.
"""

import wave
import struct
import os
import sys

# Configuration
SAMPLE_RATE = 48000
DURATION = 5.0  # seconds
TARGET_SAMPLES = int(SAMPLE_RATE * DURATION)
AUDIO_DIR = "/Users/Alexander Kemp/CascadeProjects/SDcardcheapheat/cheapheataudio"
OUTPUT_DIR = "."

def wav_to_float_array(filepath, target_samples):
    """Read WAV file and convert to float array [-1.0, 1.0]."""
    with wave.open(filepath, 'rb') as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        rate = wf.getframerate()
        n_frames = wf.getnframes()
        
        print(f"  Channels: {n_channels}, Width: {sampwidth}, Rate: {rate}, Frames: {n_frames}")
        
        # Read all frames
        raw_data = wf.readframes(n_frames)
        
        # Convert to float [-1.0, 1.0]
        if sampwidth == 2:
            # 16-bit signed
            fmt = f"<{n_frames * n_channels}h"
            samples = struct.unpack(fmt, raw_data)
            float_samples = [s / 32768.0 for s in samples]
        elif sampwidth == 3:
            # 24-bit (rare)
            float_samples = []
            for i in range(0, len(raw_data), 3):
                b = raw_data[i:i+3]
                val = int.from_bytes(b, byteorder='little', signed=True)
                float_samples.append(val / 8388608.0)
        else:
            raise ValueError(f"Unsupported sample width: {sampwidth}")
        
        # Handle mono -> stereo expansion
        if n_channels == 1:
            print("  Converting mono to stereo...")
            stereo = []
            for s in float_samples:
                stereo.extend([s, s])
            float_samples = stereo
            n_channels = 2
        
        # Trim or pad to target length (5 seconds)
        target_frames = target_samples * 2  # stereo interleaved
        if len(float_samples) > target_frames:
            float_samples = float_samples[:target_frames]
            print(f"  Trimmed to {target_samples} samples ({target_samples/SAMPLE_RATE:.1f}s)")
        elif len(float_samples) < target_frames:
            # Loop the content to fill 5 seconds
            print(f"  Looping to reach {target_samples} samples...")
            while len(float_samples) < target_frames:
                remaining = target_frames - len(float_samples)
                float_samples.extend(float_samples[:min(remaining, len(float_samples))])
        
        return float_samples

def write_c_array(filename, varname, data):
    """Write int16 array as C header file with QSPIFLASH placement."""
    header_path = os.path.join(OUTPUT_DIR, filename)
    
    # Convert float [-1,1] to int16
    int_data = [max(-32768, min(32767, int(val * 32768.0))) for val in data]
    
    with open(header_path, 'w') as f:
        f.write(f"// Auto-generated noise loop: {varname}\n")
        f.write(f"// Samples: {len(data)//2} frames @ 48kHz stereo = {len(data)//2/SAMPLE_RATE:.2f}s\n")
        f.write(f"// Storage: 16-bit int (~50% size reduction vs float)\n")
        f.write(f"// Total size: {len(data) * 2} bytes\n")
        f.write(f"// Placed in QSPIFLASH to avoid overflowing main FLASH\n\n")
        f.write(f"#ifndef {varname.upper()}_H\n")
        f.write(f"#define {varname.upper()}_H\n\n")
        f.write(f"#include <stddef.h>\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"static constexpr size_t {varname}_count = {len(data)};\n\n")
        # Use section attribute to place in QSPIFLASH
        f.write(f"static const int16_t __attribute__((section(\".qspiflash_text\"))) {varname}[{len(data)}] = {{\n    ")
        
        # Write data in rows of 16 values
        for i, val in enumerate(int_data):
            if i > 0:
                if i % 16 == 0:
                    f.write(",\n    ")
                else:
                    f.write(", ")
            f.write(f"{val}")
        
        f.write("\n};\n\n")
        f.write(f"#endif // {varname.upper()}_H\n")
    
    print(f"  Written: {header_path} ({len(data) * 2} bytes - 16-bit)")

def main():
    print("Converting noise loops for cheap-heat-guitar...\n")
    
    # Define pairs (tape, vinyl)
    pairs = [
        ("noise_tape_1", "cheapheat_tape.wav", "noise_vinyl_1", "cheapheat_vinyl.wav"),
        ("noise_tape_2", "cheapheat_tape2.wav", "noise_vinyl_2", "cheapheat_vinyl2.wav"),
        ("noise_tape_3", "cheapheat_tape3.wav", "noise_vinyl_3", "cheapheat_vinyl3.wav"),
    ]
    
    total_size = 0
    
    for tape_var, tape_file, vinyl_var, vinyl_file in pairs:
        print(f"\n--- Pair: {tape_var} + {vinyl_var} ---")
        
        # Convert tape
        tape_path = os.path.join(AUDIO_DIR, tape_file)
        if os.path.exists(tape_path):
            print(f"Processing {tape_file}...")
            tape_data = wav_to_float_array(tape_path, TARGET_SAMPLES)
            write_c_array(f"{tape_var}.h", tape_var, tape_data)
            total_size += len(tape_data) * 4
        else:
            print(f"  WARNING: {tape_file} not found")
        
        # Convert vinyl
        vinyl_path = os.path.join(AUDIO_DIR, vinyl_file)
        if os.path.exists(vinyl_path):
            print(f"Processing {vinyl_file}...")
            vinyl_data = wav_to_float_array(vinyl_path, TARGET_SAMPLES)
            write_c_array(f"{vinyl_var}.h", vinyl_var, vinyl_data)
            total_size += len(vinyl_data) * 4
        else:
            print(f"  WARNING: {vinyl_file} not found")
    
    print(f"\n--- Summary ---")
    print(f"Total converted: {total_size} bytes ({total_size/1024/1024:.2f} MB)")
    print(f"Header files written to: {OUTPUT_DIR}")

if __name__ == "__main__":
    main()
