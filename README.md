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
<img width="2557" height="1347" alt="Screenshot 2026-10-10 143500" src="https://github.com/user-attachments/assets/b6f32d97-7b23-4943-a6c5-4817a83d972a" />
<img width="2556" height="1371" alt="Screenshot 2026-10-10 140524" src="https://github.com/user-attachments/assets/f877d681-f51f-43a6-bb49-6745eb22db13" />
<img width="2556" height="1380" alt="Screenshot 2026-10-10 140351" src="https://github.com/user-attachments/assets/5496cfdb-1c14-4525-80c7-52eb96eb6167" />
<img width="2557" height="1381" alt="Screenshot 2026-10-10 140102" src="https://github.com/user-attachments/assets/e739f831-18c3-4639-827c-71af5ac8f80e" />
<img width="2557" height="1350" alt="Screenshot 2026-10-10 135840" src="https://github.com/user-attachments/assets/aa026b4b-8c2b-4368-ad2c-623d7c4af543" />
<img width="2557" height="1366" alt="Screenshot 2026-10-10 135826" src="https://github.com/user-attachments/assets/2c5121cb-17e1-4d59-aafe-61ea9ff99ecb" />
<img width="2557" height="1386" alt="Screenshot 2026-10-10 135815" src="https://github.com/user-attachments/assets/396dcdbe-d2fe-4df3-b58b-8f8be42a8676" />
<img width="2557" height="1372" alt="Screenshot 2026-10-10 135727" src="https://github.com/user-attachments/assets/20106567-9242-4f13-957f-42365aa408cb" />
<img width="2556" height="1386" alt="Screenshot 2026-10-10 135640" src="https://github.com/user-attachments/assets/9299736d-37b8-42aa-bc9a-366006b1159e" />
<img width="2557" height="1377" alt="Screenshot 2026-10-10 135619" src="https://github.com/user-attachments/assets/dd4f6c3c-ddf6-4072-a746-2bcc86c00a9e" />
<img width="2557" height="1377" alt="Screenshot 2026-10-10 135558" src="https://github.com/user-attachments/assets/ff311dbe-446c-4ce4-bf1a-77675a7cc3b6" />
<img width="2557" height="1385" alt="Screenshot 2026-10-10 135535" src="https://github.com/user-attachments/assets/ff4c581a-efe7-4a63-9aee-9d31b1ae0817" />

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
| [Box3D](https://github.com/erincatto/box3d) | Physics simulation | [Link](https://github.com/erincatto/box3d) |
| [Bullet3](https://github.com/bulletphysics/bullet3) | Physics simulation | [Link](https://github.com/bulletphysics/bullet3) |
| [Dear ImGui](https://github.com/ocornut/imgui) | Immediate mode GUI | [Link](https://github.com/ocornut/imgui) |
| [RmlUi](https://github.com/mikke89/RmlUi) | HTML+CSS game UI | [Link](https://github.com/mikke89/RmlUi) |
| [Assimp](https://github.com/assimp/assimp) | 3D model loading | [Link](https://github.com/assimp/assimp) |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | 3D audio | [Link](https://github.com/kcat/openal-soft) |
| [GLM](https://github.com/g-truc/glm) | Mathematics library | [Link](https://github.com/g-truc/glm) |
| [spdlog](https://github.com/gabime/spdlog) | Fast logging | [Link](https://github.com/gabime/spdlog) |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | YAML parsing | [Link](https://github.com/jbeder/yaml-cpp) |
| [ser20](https://github.com/unravel-dev/ser20) | Serialization | [Link](https://github.com/unravel-dev/ser20) |
