# Project Acorn

An IBM PC 5150 (Intel 8086) emulator written in C.

## Overview

Project Acorn is an effort to create a self-contained emulator for the IBM PC 5150, the first personal computer released by IBM in 1981. The emulator aims to accurately reproduce the behavior of the Intel 8086 processor and associated hardware.

## Current Status

This project is in early development. Currently implemented features:

- Basic Intel 8086 CPU structure
- Limited instruction set (MOV, JMP, conditional jumps, flag operations)
- BIOS ROM loading capability
- Memory management (1MB address space)
- SDL2 window initialization (display output not yet implemented)

## Building

### Prerequisites

- A C11-compatible compiler (GCC or Clang recommended)
- Meson build system (>= 0.55.0)
- Ninja build tool
- SDL2 development libraries

On Ubuntu/Debian:
```bash
sudo apt install meson ninja-build libsdl2-dev gcc
```

On Fedora/RHEL:
```bash
sudo dnf install meson ninja-build SDL2-devel gcc
```

On macOS:
```bash
brew install meson ninja sdl2
```

### Compilation

```bash
# Set up build directory
meson setup builddir

# Compile
meson compile -C builddir

# Install (optional)
meson install -C builddir
```

The compiled binary will be located at `builddir/acorn`.

## Running

To run the emulator, you need a BIOS ROM file. The default expected file is `0239462.BIN` in the current directory.

```bash
./builddir/acorn
```

**Note:** The emulator is currently in a very early state and will only execute a limited number of instructions before halting.

## Project Structure

```
.
├── 5150emu.c        - Main emulator entry point and BIOS loading
├── intel8086.c      - CPU emulation and instruction execution
├── intel8086.h      - CPU structure definitions
├── opcode.h         - Instruction implementations
├── meson.build      - Build configuration
├── LICENSE          - NCSA license file
├── docs/            - Documentation and development plans
└── README.md        - This file
```

## Documentation

See the [docs/](docs/) directory for:
- Development roadmap
- Architecture documentation
- Contribution guidelines

## License

This project is licensed under the University of Illinois/NCSA Open Source License. See the [LICENSE](LICENSE) file for details.

## Contributing

This project is in early development and welcomes contributions! See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the development roadmap and contribution guidelines.

## References

- [Intel 8086 Family User's Manual](https://edge.edx.org/c4x/BITSPilani/EEE231/asset/8086_family_Users_Manual_1_.pdf)
- [IBM PC 5150 Technical Reference](http://www.minuszerodegrees.net/manuals/IBM_5150_Technical_Reference_6025005_APR84.pdf)
- [x86 Instruction Set Reference](https://www.felixcloutier.com/x86/)
