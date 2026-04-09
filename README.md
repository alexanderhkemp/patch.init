This repository contains firmware projects for the Electrosmith PATCH.INIT() module (Daisy Patch Submodule). They're intended for fun and experimentation, and everyone is welcome to use, modify, or enjoy them. No guarantees of any kind are implied.

## Projects

- `filter-into-delay/` — `filter-into-delay` firmware
- `stereo-parametric-eq/patch_init_app/` — stereo parametric EQ firmware

## Dependencies

This repo includes Daisy dependencies in two ways:

- Top-level `libDaisy/` and `DaisySP/` for projects at the repo root (like `filter-into-delay/`)
- `stereo-parametric-eq/libDaisy/` and `stereo-parametric-eq/DaisySP/` for the nested EQ project

These folders are expected by each project's Makefile and are normal for this layout.

## Quick Build

From each project folder:

```sh
make clean
make -j
```
