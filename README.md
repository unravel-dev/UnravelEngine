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

### Try it
1. Download the latest release from [Releases](https://github.com/unravel-dev/UnravelEngine/releases)
2. Install the [.NET 9 SDK](https://dotnet.microsoft.com/download) (required for C# scripting)


### System Requirements
| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **OS** | Windows 10, Ubuntu 22.04, macOS 14 | Windows 11, Ubuntu 24.04, macOS 15+ |
| **CPU** | Intel i5-4590 / AMD FX 8350 | Intel i7-8700K / AMD Ryzen 5 3600 |
| **Memory** | 8 GB RAM | 16 GB RAM |
| **Graphics** | DirectX 11 compatible | GTX 1060 / RX 580 or better |
| **.NET** | [.NET 9 SDK](https://dotnet.microsoft.com/download) | Latest .NET 9+ SDK |

## ✨ Key Features

### 🎨 **Visual Development**
- **WYSIWYG Editor** - Real-time scene editing and visualization
- **Material Editor** - Advanced PBR material authoring with alpha cutoff and shadow support
- **Asset Browser** - Intuitive asset management, thumbnails, and preview
- **Wireframe Selection** - Clear mesh selection overlays in the viewport
- **Surface Placement** - Drop meshes and prefabs into the scene with raycast placement and surface snap

### 🔧 **Development Experience**
- **C# Scripting on CoreCLR** - Modern .NET scripting with hot-reload, powered by CoreCLR (`hostfxr`) via [dotnetpp](https://github.com/unravel-dev/monopp)
- **IL Weaving** - Mono-style internal calls compiled and woven at build time for CoreCLR (no Mono runtime required)
- **Cross-Platform** - Windows, Linux, macOS support
- **Modern C++20** - Latest standards for performance and maintainability
- **Action-Based Input** - Flexible input mapping for various devices
- **Undo/Redo System** - Complete editor history management
- **Asset Compilation** - Automatic compilation of source assets with import metadata
- **Deploy Pipeline** - One-click project deployment
- **Play Mode Lifecycle** - Splash → running phases with optional logo splash screen

### 🎮 **Engine Capabilities**
- **PBR Deferred Rendering** - Physically-based rendering pipeline
- **Dynamic Shadows** - Realistic shadow casting with multiple techniques
- **Reflection Probes** - Environment reflections with configurable capture / bake settings
- **Post-Processing Volumes** - Spatial volumes with Bloom, Tonemapping, FXAA, ASSAO, SSR, and SSIL; local/global modes with priority and blend transitions
- **Skylight** - Atmospheric lighting with Perez sky model
- **Per-Submesh LODs** - Animation-aware world bounds, culling, and LOD selection per submesh
- **GPU Resource Eviction** - Automatic GPU memory pressure handling on supported backends
- **Physics Integration** - Powered by Bullet Physics
- **3D Audio** - Spatial audio with OpenAL Soft
- **Animation System** - Skeletal and keyframe animations
- **Particle System** - GPU-friendly particle effects and simulations
- **Game UI** - HTML+CSS based UI system powered by RmlUi with full world-space support
- **Prefab System** - Reusable entity templates with overrides and updates
- **ECS Architecture** - Entity-Component-System powered by EnTT
- **Async Asset Loading** - Non-blocking resource management

### 📁 **Format Support**
- **3D Models**: OBJ, FBX, GLTF, GLB, DAE, and more
- **Audio**: WAV, MP3, OGG formats
- **Textures**: PNG, JPG, TGA, DDS, KTX, HDR formats

### 🎯 **Graphics APIs**
DirectX 11 • DirectX 12 • Vulkan • OpenGL

## 📸 Screenshots
<img width="2560" height="1380" alt="Screenshot 2026-06-09 235910" src="https://github.com/user-attachments/assets/7bdbe702-ad80-418b-a2be-81d6c46fa305" />
<img width="2560" height="1380" alt="Screenshot 2025-08-10 234456" src="https://github.com/user-attachments/assets/1fc3ccc8-1ad1-4a8a-b335-7e478eb8f479" />
<img width="2560" height="1380" alt="Screenshot 2025-08-10 234618" src="https://github.com/user-attachments/assets/98475418-1f0f-41be-9dac-4e2268c9feda" />
<img width="2560" height="1380" alt="Screenshot 2026-03-10 082240" src="https://github.com/user-attachments/assets/9e2e9955-13cf-4547-ba6f-0c92d1572d57" />

## 📖 Documentation
Engine C++ documentation can be found here - [Engine Api](https://unravel-dev.github.io/UnravelEngine/engine-api/html/)

Scripting C# documentation can be found here - [Scripting Api](https://unravel-dev.github.io/UnravelEngine/script-api/html/)

## 🚧 Current Status

Unravel Engine is in **active development** and not yet production-ready. We welcome:
- 🤝 **Contributions** from developers
- 💡 **Feature requests** from the community  
- 📝 **Feedback** to guide development priorities

## 🏁 Getting Started

**Prerequisites**: Install the [.NET 9 SDK](https://dotnet.microsoft.com/download) before using the engine. Script compilation and hot-reload use the `dotnet` CLI and CoreCLR.

> **Note**: A .NET runtime alone is not enough for editing — you need the **SDK** so scripts can compile. Newer major SDKs are accepted via roll-forward when available.

## 🎯 Editor

Download pre-built binaries for Windows and Linux from [Releases](https://github.com/unravel-dev/UnravelEngine/releases)

### Code Editing Integration
The Editor integrates seamlessly with **Visual Studio Code** and its variants (Cursor, VSCodium, etc) for script editing:

- Double-click any script in the editor to open it in your detected VS Code installation
- Install the recommended extensions when prompted for the best development experience
- Enjoy features like syntax highlighting, IntelliSense, and debugging support

### Editor MCP Server
While the editor is running, a localhost MCP (JSON-RPC over HTTP) server is available for AI tooling:

- URL: `http://127.0.0.1:27182/mcp`
- Bind: `127.0.0.1` only (default port `27182`)
- Open **Windows → MCP Server** in the editor for status, endpoint copy, start/stop, and an activity log

## 🛠 Building from Source

### Prerequisites
- **CMake** 3.20 or higher
- **C++20** compatible compiler (MSVC 2022, GCC 12+, Clang 12+)
- **Git** with LFS support
- **.NET 9 SDK** ([Download](https://dotnet.microsoft.com/download)) — `dotnet` must be on `PATH`

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
cd build/bin

./UnravelEditor.exe

or

./unravel-editor
```

### Platform-Specific Notes
- **Windows**: MSVC or Clang recommended
- **Linux**: Install a recent .NET 9 SDK package from package manager or [Microsoft’s install docs](https://learn.microsoft.com/dotnet/core/install/linux); see also [workflow dependencies](https://github.com/unravel-dev/UnravelEngine/blob/main/.github/workflows/linux.yml)
- **macOS**: Xcode command line tools required, plus the .NET 9 SDK

### Troubleshooting
- **Issue**: `dotnet` not found → Install the .NET 9 SDK and ensure it is on `PATH`
- **Issue**: Script compile fails → Confirm `dotnet --info` reports an SDK (not only a runtime)
- **Issue**: Submodule errors → Run `git submodule update --init --recursive`
- **Issue**: CMake configuration fails → Check CMake version and compiler support

## 🤝 Community & Support

- 🐛 **Issues**: [Report bugs](https://github.com/unravel-dev/UnravelEngine/issues)
- 💡 **Discussions**: [Feature requests & questions](https://github.com/unravel-dev/UnravelEngine/discussions)
- 📚 **Documentation**: [Engine API](https://unravel-dev.github.io/UnravelEngine/engine-api/html/) • [Script API](https://unravel-dev.github.io/UnravelEngine/script-api/html/)

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
