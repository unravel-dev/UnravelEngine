<div align="center">

# 🎮 Unravel Engine
### Modern Cross-Platform C++20 Game Engine with WYSIWYG Editor

![windows](https://github.com/unravel-dev/UnravelEngine/actions/workflows/windows.yml/badge.svg)
![linux](https://github.com/unravel-dev/UnravelEngine/actions/workflows/linux.yml/badge.svg)
![macos](https://github.com/unravel-dev/UnravelEngine/actions/workflows/macos.yml/badge.svg)
[![Release](https://img.shields.io/github/v/release/unravel-dev/UnravelEngine)](https://github.com/unravel-dev/UnravelEngine/releases)

[📥 Download](https://github.com/unravel-dev/UnravelEngine/releases) • [📖 Documentation](https://unravel-dev.github.io/UnravelEngine/script-api/html/) • [💬 Community](#community--support)

</div>

---

**Unravel Engine** is a cutting-edge, cross-platform game engine and WYSIWYG (What You See Is What You Get) editor, crafted in modern C++20. It empowers developers to create high-performance, immersive games with ease.

## 🚀 Quick Start

### Try the Demo
1. Download the latest release from [Releases](https://github.com/unravel-dev/UnravelEngine/releases)
2. Extract and run `еditor.exe` (Windows) or `еditor` (Linux)
3. Open the included DemoProject to explore features

### System Requirements
| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Windows 10, Ubuntu 20.04, macOS 14 | Windows 11, Ubuntu 24.04, macOS 15+ |
| **CPU** | Intel i5-4590 / AMD FX 8350 | Intel i7-8700K / AMD Ryzen 5 3600 |
| **Memory** | 8 GB RAM | 16 GB RAM |
| **Graphics** | DirectX 11 compatible | GTX 1060 / RX 580 or better |
| **Storage** | | |

## ✨ Key Features

### 🎨 **Visual Development**
- **WYSIWYG Editor** - Real-time scene editing and visualization
- **Material Editor** - Advanced PBR material authoring
- **Asset Browser** - Intuitive asset management and preview

### 🔧 **Development Experience**
- **C# Scripting** - Full-featured scripting with hot-reload
- **Cross-Platform** - Windows, Linux, macOS support
- **Modern C++20** - Latest standards for performance and maintainability
- **Action-Based Input** - Flexible input mapping for various devices
- **Undo/Redo System** - Complete editor history management
- **Asset Compilation** - Automatic compilation of source assets with import metadata
- **Deploy Pipeline** - One-click project deployment

### 🎮 **Engine Capabilities**
- **PBR Deferred Rendering** - Physically-based rendering pipeline
- **Dynamic Shadows** - Realistic shadow casting with multiple techniques
- **Reflection Probes** - Environment reflections and lighting
- **Post-Processing Volumes** - Spatial volumes with Bloom, Tonemapping, FXAA, SSAO, and SSR; local/global modes with priority and blend transitions
- **Skylight** - Atmospheric lighting with Perez sky model
- **Physics Integration** - Powered by Bullet Physics
- **3D Audio** - Spatial audio with OpenAL Soft
- **Animation System** - Skeletal and keyframe animations
- **Robust Particle System** - Advanced particle effects and simulations
- **Game UI** - HTML+CSS based UI system powered by RmlUi with full World space support.
- **Prefab System** - Reusable entity templates with overrides and updates
- **ECS Architecture** - Entity-Component-System powered by EnTT
- **Async Asset Loading** - Non-blocking resource management

### 📁 **Format Support**
- **3D Models**: OBJ, FBX, GLTF, DAE, and more
- **Audio**: WAV, MP3, OGG formats
- **Textures**: PNG, JPG, DDS, HDR formats

### 🎯 **Graphics APIs**
DirectX 11 • DirectX 12 • Vulkan • OpenGL

## 📸 Screenshots
<img width="2560" height="1380" alt="Screenshot 2025-08-10 234456" src="https://github.com/user-attachments/assets/1fc3ccc8-1ad1-4a8a-b335-7e478eb8f479" />
<img width="2560" height="1380" alt="Screenshot 2025-08-10 234618" src="https://github.com/user-attachments/assets/98475418-1f0f-41be-9dac-4e2268c9feda" />
<img width="2560" height="1380" alt="Screenshot 2026-03-01 125054" src="https://github.com/user-attachments/assets/1656c856-805e-431f-b8be-471987bac1bd" />

## 📖 Documentation
Engine C++ documentation can be found here - [Engine Api](https://unravel-dev.github.io/UnravelEngine/engine-api/html/)

Scripting C# documentation can be found here - [Scripting Api](https://unravel-dev.github.io/UnravelEngine/script-api/html/)

## 🚧 Current Status

Unravel Engine is in **active development** and not yet production-ready. We welcome:
- 🤝 **Contributions** from developers
- 💡 **Feature requests** from the community  
- 📝 **Feedback** to guide development priorities



## 🏁 Getting Started

**Prerequisites**: Download and install [Mono](https://www.mono-project.com/) before using the engine.

> **Note for Linux users**: Install the `mono-complete` package after follwing the install instructions from the url for the full IDE experience.

## 🎯 Editor

Download pre-built binaries for Windows and Linux from [Releases](https://github.com/unravel-dev/UnravelEngine/releases)

The release includes a **DemoProject** to help you get started quickly and explore the engine's capabilities.

### Code Editing Integration
The Editor integrates seamlessly with **Visual Studio Code** and its variants (Cursor, VSCodium, etc) for script editing:

- Double-click any script in the editor to open it in your detected VS Code installation
- Install the recommended extensions when prompted for the best development experience
- Enjoy features like syntax highlighting, IntelliSense, and debugging support


## 🛠 Building from Source

### Prerequisites
- **CMake** 3.20 or higher
- **C++20** compatible compiler (MSVC 2022, GCC 12+, Clang 12+)
- **Git** with LFS support
- **Mono** 6.12 ([Download](https://www.mono-project.com/))

### Build Steps
```bash
# Clone with submodules
git clone --recursive https://github.com/unravel-dev/UnravelEngine.git
cd UnravelEngine

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --parallel

# Run editor
./build/bin/editor
```

### Platform-Specific Notes
- **Windows**: MSVC or Clang recommended
- **Linux**: Install mono-complete package for full IDE experience ([workflow dependencies](https://github.com/unravel-dev/UnravelEngine/blob/main/.github/workflows/linux.yml))
- **macOS**: Xcode command line tools required

### Troubleshooting
- **Issue**: Mono not found → Ensure mono is in PATH
- **Issue**: Submodule errors → Run `git submodule update --init --recursive`
- **Issue**: CMake configuration fails → Check CMake version and compiler support

## 🤝 Community & Support

- 🐛 **Issues**: [Report bugs](https://github.com/unravel-dev/UnravelEngine/issues)
- 💡 **Discussions**: [Feature requests & questions](https://github.com/unravel-dev/UnravelEngine/discussions)
- 📚 **Documentation**: [Engine API](https://unravel-dev.github.io/unravel-engine-api/) • [Script API](https://unravel-dev.github.io/unravel-script-api/)

## 🤝 Contributing

We welcome contributions! Here's how you can help:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 🙏 Third-Party Libraries

Unravel Engine is built upon these excellent open-source libraries:

| Library | Purpose | Repository |
|---------|---------|------------|
| [bgfx](https://github.com/bkaradzic/bgfx) | Cross-platform rendering | [Link](https://github.com/bkaradzic/bgfx) |
| [EnTT](https://github.com/skypjack/entt) | Entity-Component-System | [Link](https://github.com/skypjack/entt) |
| [Bullet3](https://github.com/bulletphysics/bullet3) | Physics simulation | [Link](https://github.com/bulletphysics/bullet3) |
| [Dear ImGui](https://github.com/ocornut/imgui) | Immediate mode GUI | [Link](https://github.com/ocornut/imgui) |
| [RmlUi](https://github.com/mikke89/RmlUi) | HTML+CSS game UI | [Link](https://github.com/mikke89/RmlUi) |
| [Assimp](https://github.com/assimp/assimp) | 3D model loading | [Link](https://github.com/assimp/assimp) |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 3D audio | [Link](https://github.com/kcat/openal-soft) |
| [GLM](https://github.com/g-truc/glm) | Mathematics library | [Link](https://github.com/g-truc/glm) |
| [spdlog](https://github.com/gabime/spdlog) | Fast logging | [Link](https://github.com/gabime/spdlog) |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | YAML parsing | [Link](https://github.com/jbeder/yaml-cpp) |
| [ser20](https://github.com/unravel-dev/ser20) | Serialization | [Link](https://github.com/unravel-dev/ser20) |
