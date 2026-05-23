# QSPI File Converters

Tools to convert various file formats to raw binary for QSPI flashing.

## wav_to_qspi.py

Converts WAV audio files to raw 16-bit PCM binary format suitable for QSPI flash.

```bash
python3 wav_to_qspi.py input.wav output.bin

# Specify sample rate and duration
python3 wav_to_qspi.py input.wav output.bin --rate 48000 --duration 5.0
```

## extract_from_header.py

Extracts binary data from C header files (int16_t arrays) to raw .bin files.

Useful when your audio was previously converted to C headers but you need the raw binary for QSPI flashing.

```bash
python3 extract_from_header.py input.h output.bin
```

## Generic Binary

Any file can be flashed to QSPI as raw binary. Just ensure your firmware knows the format:

```bash
# Copy any file as-is
cp mydata.raw mydata.bin

# Or convert with standard tools
xxd -p input.dat > output.hex  # hex dump
xxd -r -p output.hex output.bin  # back to binary
```

## File Format Guidelines

For best results with Daisy audio playback:

| Format | Sample Rate | Bit Depth | Channels |
|--------|-------------|-----------|----------|
| WAV | 48000 | 16-bit | Stereo |
| RAW PCM | 48000 | 16-bit | Stereo or Mono |

QSPI reads are fast enough for real-time audio streaming at 48kHz stereo.

## Memory Calculation

For audio files:

```
Size = Sample_Rate × Duration × Channels × (Bit_Depth / 8)

Example: 5 seconds, 48kHz, stereo, 16-bit
Size = 48000 × 5 × 2 × 2 = 960,000 bytes (~938KB)
```

The 8MB QSPI can hold:
- ~8.7 seconds of stereo 48kHz 16-bit audio
- Or ~42 seconds of mono 48kHz 16-bit audio
- Or ~85 seconds of mono 22kHz 8-bit audio
