# UniVex_Engine

Rebuilding the modern 3D and 2D Game Engine architecture with modular runtime components in C++23.

## Overview

UniVex Engine is designed with a decoupled runtime architecture featuring:
- **Core Runtime**: Memory, Threading, Scheduling, Math, and Logging
- **Rendering & RHI**: Multi-backend rendering support (Vulkan, Desktop OpenGL, Null backend)
- **Scene & Entity**: Modern scene node tree and component architecture
- **Physics & Audio**: Modular physics and spatial audio integration
- **Cross-Platform**: Windows and multi-platform platform abstractions

## Build Requirements

- **C++ Standard**: C++23 required
- **Build System**: CMake 3.24+
- **Compiler**: Modern MSVC, Clang, or GCC with C++23 support

## License

Copyright (c) 2026 UniVex Studios. All Rights Reserved.