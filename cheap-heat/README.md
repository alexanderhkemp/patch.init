# cheap-heat

Lo-fi tape warble firmware project for PATCH.INIT.

## Current Status

This is the initial project scaffold with a clean pass-through audio callback so build/flash flow is ready before effect implementation.

## Build

From this folder:

```sh
make clean
make -j
```

Artifacts will be in `build/`:
- `cheap-heat.elf`
- `cheap-heat.bin`

## Flash (DFU)

Put the Daisy Patch.init into DFU mode, then:

```sh
make program-dfu
```

## Notes
- This project pins `GCC_PATH` to the Daisy toolchain at `/Library/DaisyToolchain/0.2.0/arm/bin`.
- Libraries are referenced from top-level submodules at `../libDaisy` and `../DaisySP`.

## Control Map
- `CV_1` (knob 1): `HAZE` amount
- `CV_2` (knob 2): `WOW` depth
- `CV_3` (knob 3): `FLUTTER` / dropout amount
- `CV_4` (knob 4): tape/vinyl noise blend (`noon = none`, CCW = tape, CW = vinyl)
- `B8`: output HPF toggle
- `CV_5`: haze modulation (paired with `CV_1`)
- `CV_6`: wow rate modulation (paired with `CV_2`)
- `CV_7`: flutter modulation
- `CV_8`: dropout depth/rate modulation
- `CV_OUT_1`: dusty noise CV
- `CV_OUT_2`: left input RMS envelope

## SD Card Files
Place audio files in `cheapheataudio/` at SD root.

Noise pair files (`B7` cycles 1 -> 2 -> 3):
- `cheapheat_tape.wav`
- `cheapheat_vinyl.wav`
- `cheapheat_tape2.wav`
- `cheapheat_vinyl2.wav`
- `cheapheat_tape3.wav`
- `cheapheat_vinyl3.wav`

Modulation noise files:
- Preferred stereo file: `stereonoise.wav`
- Fallback mono pair (if stereo file is missing):
  - `mononoiseL.wav`
  - `mononoiseR.wav`
