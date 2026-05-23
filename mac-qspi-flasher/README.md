# Mac QSPI Flasher for Daisy Seed

A Mac-compatible tool for flashing binary files to the Daisy Seed's external QSPI flash memory (8MB).

## What It Does

The Daisy Seed has two separate flash memories:
- **Internal 128KB FLASH** — for firmware (web-flashable)
- **External 8MB QSPI FLASH** — for samples, images, data

This tool lets you flash files to the external QSPI on macOS, enabling you to:
- Store audio samples for playback
- Save configuration data
- Cache large datasets
- Anything that doesn't fit in the 128KB internal flash

## How It Works

The system uses a client/server architecture:

1. **FlasherClient** — runs on Daisy Seed, receives data via USB CDC
2. **qspi_sender.py** — Python script running on Mac, sends files to Daisy

## Prerequisites

- Daisy Seed board with USB cable
- Python 3 with pyserial: `pip3 install pyserial`
- Daisy toolchain (gcc-arm-none-eabi, make, etc.)
- libDaisy and DaisySP libraries

## Installation

```bash
# Clone or copy this folder to your project
cd mac-qspi-flasher

# Build the FlasherClient (Daisy-side receiver)
cd flasher-client
make
```

## Quick Start

### 1. Build the Daisy Client

```bash
cd flasher-client
make
# Creates build/FlasherClient.bin
```

### 2. Flash the Client to Daisy

Use Daisy Web Programmer or dfu-util to flash `flasher-client/build/FlasherClient.bin`. 

The LED will turn on — the client is waiting for data.

### 3. Prepare Your Files

Convert your data to raw binary format:

```bash
# If you have WAV files, convert them
cd ../converter
python3 wav_to_qspi.py input.wav output.bin

# Or use your own binary files directly
```

### 4. Flash to QSPI

```bash
cd ..  # Back to mac-qspi-flasher root

# Flash single file
python3 qspi_sender.py mydata.bin

# Flash multiple files
python3 qspi_sender.py sample1.bin sample2.bin sample3.bin

# Specify port manually if needed
python3 qspi_sender.py --port /dev/tty.usbmodem123456 mydata.bin
```

The script auto-detects the Daisy port. Run with `--list` to see available ports.

### 5. Flash Your Actual Firmware

After QSPI flashing, reflash your normal firmware. The QSPI data persists!

```bash
# Your firmware can now access the flashed data
make program  # or use web programmer
```

## Using In Your Firmware

Access the QSPI data in your C++ code:

```cpp
#include "libDaisy.h"

// QSPI is memory-mapped starting at address 0x90000000
typedef uint8_t QSPI_Page[4096];  // 4KB pages

// Direct access by page number
uint8_t* my_data = (uint8_t*)0x90000000;  // Page 0

// Or calculate offset
uint32_t page_num = 0;  // Which 4KB page
uint32_t offset = 0;    // Byte offset within page
uint8_t* ptr = (uint8_t*)(0x90000000 + (page_num * 4096) + offset);

// Read your data
float sample = *(float*)ptr;
```

## Protocol Details

Communication happens over USB CDC (virtual serial port) at 115200 baud:

1. Daisy sends request: `"BLOC"` + block_number (6 bytes)
2. Mac responds with data block:
   - `"BLOC"` marker (4 bytes)
   - Block number (2 bytes)
   - CRC checksum (1 byte)
   - End flag (1 byte)
   - Data payload (1024 bytes)
   - `"END"` marker (3 bytes)
3. Daisy writes to QSPI every 4 blocks (4096 bytes = 1 page)
4. Repeat until all data sent

## File Layout

Files are stored sequentially in QSPI:

| File | Start Block | Start Address |
|------|-------------|---------------|
| File 1 | 0 | 0x90000000 |
| File 2 | N | 0x90000000 + (N × 1024) |
| ... | ... | ... |

You'll need to track file offsets in your firmware or use a directory structure.

## Troubleshooting

**"Could not find Daisy port"**
```bash
python3 qspi_sender.py --list  # See available ports
python3 qspi_sender.py --port /dev/tty.usbmodemXXXX myfile.bin
```

**"No response from Daisy"**
- Verify FlasherClient is running (LED should be on)
- Reset the Daisy and try again
- Check USB connection

**"Flash verification failed"**
- The QSPI flash may be worn out (limited to ~100,000 writes)
- Try erasing first with `make program-dfu` and `--erase-all`

**Port permission errors**
```bash
# Add your user to dialout group (Linux/Mac sometimes needed)
sudo usermod -a -G dialout $USER
# Or run with sudo (not recommended long-term)
sudo python3 qspi_sender.py myfile.bin
```

## Advanced Usage

### Custom Directory Structure

Create a JSON file tracking your QSPI layout:

```json
{
  "files": [
    {"name": "kick.wav", "start_block": 0, "size": 48000},
    {"name": "snare.wav", "start_block": 47, "size": 48000}
  ]
}
```

Load this in your firmware to locate files dynamically.

### Large Files

The 8MB QSPI can hold:
- ~21 seconds of stereo 48kHz 16-bit audio
- ~4 minutes of mono 8kHz 8-bit audio
- Or any mix of data up to 8MB total

### Multiple Flash Cycles

QSPI flash has limited write cycles (~100,000). For development:
- Cache files in SDRAM when possible
- Only re-flash QSPI when audio content changes
- Firmware reflashing doesn't affect QSPI data

## License

The FlasherClient is based on DADDesign's work. The Python sender and documentation are provided for Mac compatibility.

## Credits

- Original Windows QSPI Flasher: [DADDesign-Projects](https://github.com/DADDesign-Projects/Daisy_QSPI_Flasher)
- Mac port: Python sender with pyserial
