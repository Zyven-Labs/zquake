# zquake - Quake1 Source Port

A modern Quake1 source port built with SDL2, Vulkan, and C++20.

## Features

- **Vulkan renderer** with modern graphics pipeline
- **SDL2** for windowing, input, and audio
- **Fully deterministic** fixed-point math (24.8 format)
- **QuakeC VM** for game logic and total conversions
- **Bit-exact replication** across platforms (x86, ARM)
- **Multi-threaded renderer** (physics stays single-threaded for determinism)
- **Enhanced audio** (16-bit stereo, 256 channels, streaming)

## Building

### Prerequisites

- CMake 3.27+
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2019+)
- Vulkan SDK
- Internet connection (for FetchContent dependencies)

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Windows (MSVC)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Run

```bash
./zquake +map map01
```

## Architecture

```
zquake/
├── src/
│   ├── core/          # Fixed-point math, memory, containers, logging
│   ├── filesystem/    # Virtual FS, PAK archives
│   ├── subsystems/    # Network protocol, reliable/unreliable
│   ├── vulkan/        # Vulkan renderer pipeline
│   ├── engine/        # Console, CVar, entity, world, game
│   ├── vm/            # QuakeC VM + builtins
│   └── app/           # Entry point, main loop
├── include/           # Public interface headers
├── tests/             # Catch2 test suite
├── shaders/           # GLSL → SPIR-V build-time compilation
└── tools/             # Dev tools (QCC, etc.)
```

## Determinism

zquake uses fixed-point arithmetic throughout the core math systems, ensuring bit-exact determinism across all supported platforms. This enables:

- Perfect demo playback
- Synchronized multiplayer
- Reproducible debugging

The QuakeC VM also operates entirely on 32-bit integers (fixed-point), with no IEEE 754 dependency.

## License

Based on the Quake source code, released under the GPL v2+.
See gnu.txt for full license text.
