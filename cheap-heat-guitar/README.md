# Cheap Heat Guitar - Hothouse Edition

Lo-fi tape warble effect for Cleveland Music Co Hothouse DIY DSP Platform.

## Hardware
- **Platform:** Cleveland Music Co Hothouse (Daisy Seed-based)
- **RAM:** 64MB SDRAM (stores 3x 5-second stereo noise loops internally)
- **I/O:** Mono guitar input, stereo TRS output

Uses the official [Hothouse HAL](https://github.com/clevelandmusicco/HothouseExamples) for hardware abstraction.

## Controls

### Knobs (6)
| Knob | Function | Range |
|------|----------|-------|
| 1 | **Haze** (saturation/distortion) | Clean → Heavy lo-fi crumble |
| 2 | **Wow Depth** (tape pitch warble) | 0 → ~17ms max modulation |
| 3 | **Flutter Depth** (fast wow/flutter amount) | 0 → Maximum |
| 4 | **Noise Blend** (tape/vinyl selector) | CCW=Tape, Noon=Off, CW=Vinyl |
| 5 | **Global LFO Speed** | 0.5x → 2x all modulation rates |
| 6 | **Wet/Dry Mix** (effect blend) | 0=Clean, 1=Full warble |

### Toggle 1 (3-Position) - Noise Selection
| Position | Noise Pair |
|----------|------------|
| **UP** | Pair 1: Gentle tape hiss + sparse vinyl crackle |
| **MIDDLE** | Pair 2: Heavier cassette + dusty vinyl |
| **DOWN** | Pair 3: Reel-to-reel hum + warped record |

### Toggle 2 (3-Position) - Output HPF
| Position | HPF Setting |
|----------|-------------|
| **UP** | Aggressive HPF ~240Hz (tight guitar focus) |
| **MIDDLE** | Standard HPF ~120Hz |
| **DOWN** | HPF bypassed (full range) |

> Toggle 3 is unused (future expansion)

### Footswitches
| Switch | Function |
|--------|----------|
| **Right (FSW 2)** | Effect bypass toggle |
| **Left (FSW 1)** | +4dB output boost toggle |
| **Hold Left 2s** | Enter DFU/bootloader mode |

## Build & Flash

```bash
cd cheap-heat-guitar
make
# Flash via USB using Daisy Web Programmer or dfu-util
make program
```

## Noise Loops
Three 5-second stereo loops stored in **SDRAM** (11.5MB total):
- **Pair 1:** Tape hiss + Vinyl crackle (classic)
- **Pair 2:** Cassette noise + Dusty vinyl (lo-fi)
- **Pair 3:** Reel-to-reel hum + Warped record (extreme)

**Memory Layout:**
- Buffers placed at `0xc0000000` using `.sdram_bss` linker section
- Currently using generated placeholder noise (replace with actual WAV data)
- 52MB+ SDRAM remaining for extended delays, longer loops, or additional features

Loops are compiled into firmware binary (no SD card needed).

## Signal Chain
```
Guitar In → Haze Pre-gain (12x) → Saturation/Filter → 
Wow/Flutter Modulation → Dropouts → Noise Injection → 
Wet Pad (-15dB) → Wet/Dry Mix → Stereo Out
```

**Notes:**
- Pre-gain compensates for guitar vs Eurorack level differences
- Wet pad balances levels between dry and saturated signal
- Wow frequency fixed at 0.35Hz (only speed scales with Knob 5)
- Modulation centered at 4ms with max ~17ms deviation (subtle pitch warble)

## Status
Functional port from Patch.Init firmware. Features:
- ✅ Web flasher compatible (no QSPIFLASH)
- ✅ Low latency (4-sample audio block)
- ✅ Wet/dry mix for chorus effects
- ✅ 3 noise pairs via Toggle 1
- ✅ 3-state output HPF via Toggle 2
- ✅ Guitar-level optimized saturation
