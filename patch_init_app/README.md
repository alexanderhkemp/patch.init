# patch_init_app

## Build
From this folder:

```sh
make clean
make -j
```

Artifacts will be in `build/`:
- `patch_init_app.elf`
- `patch_init_app.bin`

## Flash (DFU)
Put the Daisy Patch.init into DFU mode, then:

```sh
make program-dfu
```

## Notes
- This project pins `GCC_PATH` to the Daisy toolchain at `/Library/DaisyToolchain/0.2.0/arm/bin` to avoid Homebrew’s `arm-none-eabi-gcc` build without headers.
- Libraries are referenced from the git submodules at `../libDaisy` and `../DaisySP`.
