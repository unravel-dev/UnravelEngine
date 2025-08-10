## [Unravel Engine](https://github.com/unravel-dev/UnravelEngine) - Cross-platform C++ Game Engine

![windows](https://github.com/unravel-dev/UnravelEngine/actions/workflows/windows.yml/badge.svg)
![linux](https://github.com/unravel-dev/UnravelEngine/actions/workflows/linux.yml/badge.svg)
![macos](https://github.com/unravel-dev/UnravelEngine/actions/workflows/macos.yml/badge.svg)


**Unravel Engine** is a cutting-edge, cross-platform game engine and WYSIWYG (What You See Is What You Get) editor, crafted in modern C++20. It empowers developers to create high-performance, immersive games with ease.

## Features

- **Cross-Platform Compatibility**: Seamlessly supports multiple operating systems to maximize your game's reach.
- **Modern C++20**: Built on the latest C++ standards for exceptional performance and maintainability.
- **WYSIWYG Editor**: Intuitive editor enabling real-time editing and visualization of game scenes.
- **Comprehensive Feature Set**:
  - **Animation System**: Robust support for skeletal and keyframe animations.
  - **Physics Integration**: Realistic physics simulations powered by [Bullet Physics](https://github.com/bulletphysics/bullet3).
  - **Audio Support**: 3D audio capabilities using [OpenAL Soft](https://github.com/kcat/openal-soft).
  - **C# Scripting**: Extend and customize gameplay logic using a C# scripting interface.
  - **Action-Based Input System**: Flexible and modular input mapping for various devices.
  - **PBR Deferred Rendering**: High-quality physically-based rendering with advanced lighting and material support.
  - **Dynamic Shadows**: Realistic shadow casting for immersive visual fidelity.
  - **Reflection Probes**: Support for realistic lighting and environment reflections.
  - **Async Asset Loading**: Asynchronous loading for smooth gameplay and faster scene transitions.
  - **Broad Format Support**:
    - Most 3D mesh formats (e.g., OBJ, FBX, GLTF).
    - Common audio formats (e.g., WAV, MP3, OGG).
  - **Graphics API Support**: Fully supports DirectX 11, DirectX 12, Vulkan, and OpenGL for maximum flexibility and performance.

## Documentation
Engine C++ documentation can be found here - [Engine Api](https://unravel-dev.github.io/unravel-engine-api/html/index.html)

Scripting C# documentation can be found here - [Script Api](https://unravel-dev.github.io/unravel-script-api/html/index.html)



## Current Status

Unravel Engine is currently under active development and is not yet production-ready. Contributions, feature requests, and feedback are highly encouraged to shape its evolution.

![Screenshot 2024-10-12 153527](https://github.com/user-attachments/assets/dacb054b-13c8-49e8-a757-dd43bdd8401a)
![Screenshot 2024-10-12 151734](https://github.com/user-attachments/assets/7eed707b-35fb-41f8-8831-4a235bd9934f)


## Getting Started

## Building
Don't forget to update submodules
```
git clone https://github.com/unravel-dev/UnravelEngine.git
cd UnravelEngine
git submodule update --init --recursive

mkdir build
cd build
cmake ..

```
## LIBRARIES
bgfx - https://github.com/bkaradzic/bgfx

ser20 - https://github.com/unravel-dev/ser20

rttr - https://github.com/rttrorg/rttr

spdlog - https://github.com/gabime/spdlog

imgui - https://github.com/ocornut/imgui

assimp - https://github.com/assimp/assimp

glm - https://github.com/g-truc/glm

openal-soft - https://github.com/kcat/openal-soft

yaml-cpp - https://github.com/jbeder/yaml-cpp

bullet3 - https://github.com/bulletphysics/bullet3

entt - https://github.com/skypjack/entt
