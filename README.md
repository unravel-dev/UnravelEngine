<div align="center">

# Unravel Engine

A cross-platform C++20 game engine and editor with C# scripting.

![windows](https://github.com/unravel-dev/UnravelEngine/actions/workflows/windows.yml/badge.svg)
![linux](https://github.com/unravel-dev/UnravelEngine/actions/workflows/linux.yml/badge.svg)
![macos](https://github.com/unravel-dev/UnravelEngine/actions/workflows/macos.yml/badge.svg)
[![Release](https://img.shields.io/github/v/release/unravel-dev/UnravelEngine)](https://github.com/unravel-dev/UnravelEngine/releases)

[Download](https://github.com/unravel-dev/UnravelEngine/releases) |
[Scripting API](https://unravel-dev.github.io/UnravelEngine/script-api/html/) |
[Engine API](https://unravel-dev.github.io/UnravelEngine/engine-api/html/) |
[Issues](https://github.com/unravel-dev/UnravelEngine/issues)

</div>

Unravel Engine is a game engine with a WYSIWYG editor, written in C++20 on top of bgfx and EnTT,
with gameplay scripted in C# on CoreCLR. It is in active development and not production-ready yet.

## Features

- **Rendering** - physically based deferred renderer with real-time global illumination,
  reflection probes, SSR, GTAO / ASSAO, SSIL, TAA, FXAA, bloom, auto exposure, tonemapping,
  render scale, physical sky and clouds, and spatial post-process volumes.
- **Graphics APIs** - Direct3D 11, Direct3D 12, Vulkan, OpenGL and Metal through bgfx.
- **Scripting** - C# on CoreCLR with hot reload; scripts open in Visual Studio Code.
- **Animation** - animation controllers, blend spaces, humanoid retargeting and IK.
- **Physics** - Box3D or Bullet backends.
- **Particles, UI and audio** - particle system, HTML / CSS game UI (RmlUi) in screen and world
  space, and 3D audio (OpenAL Soft).
- **Editor** - scene editing, prefabs with overrides, asset browser and import pipeline,
  undo / redo, play mode and a profiler.
- **Deploy** - one-click deploy for the desktop the editor runs on and for Android, with
  per-platform settings profiles.
- **AI tooling** - a local MCP server that lets AI assistants drive the editor.

Supported formats: models through Assimp (FBX, glTF / GLB, OBJ, DAE and more), textures (PNG,
JPG, TGA, DDS, KTX, HDR) and audio (WAV, MP3, OGG Vorbis, FLAC).

## Screenshots

<p align="center">
<img width="49%" alt="Editor screenshot 1" src="https://github.com/user-attachments/assets/b6f32d97-7b23-4943-a6c5-4817a83d972a" />
<img width="49%" alt="Editor screenshot 2" src="https://github.com/user-attachments/assets/f877d681-f51f-43a6-bb49-6745eb22db13" />
<img width="49%" alt="Editor screenshot 3" src="https://github.com/user-attachments/assets/5496cfdb-1c14-4525-80c7-52eb96eb6167" />
<img width="49%" alt="Editor screenshot 4" src="https://github.com/user-attachments/assets/e739f831-18c3-4639-827c-71af5ac8f80e" />
</p>

<details>
<summary>More screenshots</summary>
<p align="center">
<img width="49%" alt="Editor screenshot 5" src="https://github.com/user-attachments/assets/aa026b4b-8c2b-4368-ad2c-623d7c4af543" />
<img width="49%" alt="Editor screenshot 6" src="https://github.com/user-attachments/assets/2c5121cb-17e1-4d59-aafe-61ea9ff99ecb" />
<img width="49%" alt="Editor screenshot 7" src="https://github.com/user-attachments/assets/396dcdbe-d2fe-4df3-b58b-8f8be42a8676" />
<img width="49%" alt="Editor screenshot 8" src="https://github.com/user-attachments/assets/20106567-9242-4f13-957f-42365aa408cb" />
<img width="49%" alt="Editor screenshot 9" src="https://github.com/user-attachments/assets/9299736d-37b8-42aa-bc9a-366006b1159e" />
<img width="49%" alt="Editor screenshot 10" src="https://github.com/user-attachments/assets/dd4f6c3c-ddf6-4072-a746-2bcc86c00a9e" />
<img width="49%" alt="Editor screenshot 11" src="https://github.com/user-attachments/assets/ff311dbe-446c-4ce4-bf1a-77675a7cc3b6" />
<img width="49%" alt="Editor screenshot 12" src="https://github.com/user-attachments/assets/ff4c581a-efe7-4a63-9aee-9d31b1ae0817" />
</p>
</details>

## Getting started

1. Download the latest editor for Windows or Linux from
   [Releases](https://github.com/unravel-dev/UnravelEngine/releases) and unzip it.
2. Install the [.NET SDK](https://dotnet.microsoft.com/download) (10 recommended, 9 at least).
   Scripts compile with the `dotnet` CLI, so a .NET runtime alone is not enough.
3. Run `unravel-editor` (`unravel-editor.exe` on Windows).

Requirements: 64-bit Windows 10 / 11 or Linux (Ubuntu 20.04 or newer), and a GPU with
Direct3D 11, Vulkan or OpenGL support.

## Building from source

Prerequisites:

- Git, CMake 3.21 or newer, and Ninja
- A C++20 compiler: MSVC 2022 or Clang on Windows, GCC 12+ or Clang 15+ on Linux, Xcode 16 on macOS
- The .NET SDK (9 or newer) on `PATH`
- On Linux, the development packages:

  ```bash
  sudo apt install cmake ninja-build libglew-dev libgtk-3-dev libasound2-dev libjack-dev libudev-dev
  ```

  On Ubuntu 22.04 and newer, add `libpipewire-0.3-dev`.

Build:

```bash
git clone --recursive https://github.com/unravel-dev/UnravelEngine.git
cd UnravelEngine
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

The editor, the player and their data end up in `build/Release/bin`. On Windows, run the
commands from an x64 Developer Command Prompt for Visual Studio 2022.
[CMakePresets.json](CMakePresets.json) holds the compiler presets the CI uses (for example
`cmake --preset linux-gcc-13`).

## Android

The editor deploys games to Android 10 or newer on arm64 devices, rendering with Vulkan. It needs
the Android SDK platform tools and a JDK. A source build also needs Android NDK
`30.0.16248370` and the .NET 10 SDK to build the Android player next to the editor: configure
reports `Android player: on` when it finds them. The player builds only in single-configuration
build trees (such as Ninja) and needs Ninja installed.

**Windows**

1. Install [Android Studio](https://developer.android.com/studio). It puts the SDK in
   `%LOCALAPPDATA%\Android\Sdk` and bundles a JDK; the editor finds both there.
2. For source builds, open **SDK Manager > SDK Tools**, check **Show Package Details** and
   install **NDK (Side by side) 30.0.16248370**.
3. Configure the build again.

**Linux**

Run the setup script as your own user (not with `sudo`; it asks for `sudo` itself):

```bash
bash install_android_studio.sh
```

It installs JDK 17, the SDK command-line tools, the platform tools (with the udev rules that let
adb see a phone) and the NDK into `~/Android/Sdk`, and sets `ANDROID_HOME` and `JAVA_HOME` in
`~/.profile`. Log out and back in afterwards. Android Studio itself works too: install it with
`sudo snap install android-studio --classic` or from the tarball, then add the NDK as on Windows.

**Deploying**

Enable **Developer options > USB debugging** on the phone, connect it and accept the prompt
(`adb devices` lists it). In the editor, deploy an Android settings profile from
**Deploy > Deploy Project**, or use **Deploy and Run** in the toolbar.

## Editor MCP server

While the editor runs, it serves MCP (JSON-RPC over HTTP) at `http://127.0.0.1:27182/mcp`,
bound to localhost only. **Windows > MCP Server** shows its status, the endpoint and an activity log.

## Troubleshooting

- **`dotnet` not found or scripts do not compile** - install the .NET SDK and check that
  `dotnet --info` lists an SDK, not only a runtime.
- **Submodule errors** - run `git submodule update --init --recursive`.
- **`Android player: off`** - the configure message names the missing tool (NDK, `dotnet` or Ninja).

## Contributing

Bug reports and pull requests are welcome on [GitHub](https://github.com/unravel-dev/UnravelEngine).
Please keep pull requests focused and follow the style of the surrounding code.

## Third-party libraries

[bgfx](https://github.com/bkaradzic/bgfx),
[SDL](https://github.com/libsdl-org/SDL),
[EnTT](https://github.com/skypjack/entt),
[Dear ImGui](https://github.com/ocornut/imgui),
[RmlUi](https://github.com/mikke89/RmlUi),
[Assimp](https://github.com/assimp/assimp),
[Box3D](https://github.com/erincatto/box3d),
[Bullet](https://github.com/bulletphysics/bullet3),
[OpenAL Soft](https://github.com/kcat/openal-soft),
[FreeType](https://freetype.org),
[GLM](https://github.com/g-truc/glm),
[spdlog](https://github.com/gabime/spdlog),
[ser20](https://github.com/unravel-dev/ser20),
[yaml-cpp](https://github.com/jbeder/yaml-cpp),
[zstd](https://github.com/facebook/zstd)

## License

Unravel Engine is released under the [MIT License](LICENSE.txt).
