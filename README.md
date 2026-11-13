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
- **Graphics APIs** - Direct3D 11, Direct3D 12, Vulkan, OpenGL and Metal through bgfx, and WebGPU in
  the browser, with WebGL 2 where WebGPU is unavailable.
- **Scripting** - C# on CoreCLR with hot reload; scripts open in Visual Studio Code.
- **Animation** - animation controllers, blend spaces, humanoid retargeting and IK.
- **Physics** - Box3D or Bullet backends.
- **Particles, UI and audio** - particle system, HTML / CSS game UI (RmlUi) in screen and world
  space, and 3D audio (miniaudio).
- **Editor** - scene editing, prefabs with overrides, asset browser and import pipeline,
  undo / redo, play mode and a profiler.
- **Deploy** - one-click deploy for the desktop the editor runs on, Android and the browser, uploads
  to Steam, and per-platform settings profiles.
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

At start, at most every 6 hours, the editor fetches `latest.json` from the latest release on GitHub to see
whether a newer build for its platform is published; nothing about you or your computer is sent. Editor
Settings > Updates turns that off. With a project open, Help > Check for Updates... asks at any time.

## Platforms

| Editor runs on | Deploys to |
|---|---|
| Windows 10 / 11 (x64) | Windows, Android, Web |
| Linux x64 (Ubuntu 20.04 or newer) | Linux, Android, Web |

A desktop deploy builds for the system the editor runs on: Windows builds come from the Windows
editor and Linux builds from the Linux editor. Android and web deploys work from both. The editor
packages carry the Android and browser players, so deploying needs neither the Android NDK nor
Emscripten. The editor also builds from source on macOS, but there is no macOS package.

| Deploy | Output | Needs on this computer |
|---|---|---|
| Desktop | The player with the game's data beside it | Nothing more |
| Android | An `.apk` for arm64 phones with Android 10 or newer (Vulkan) | The Android SDK platform tools and a JDK 17 or newer, see [Android](#android) |
| Web | A folder for any static web host (WebGPU, optionally WebGL 2) | Nothing more; `cloudflared` for a public test link, see [Browser](#browser) |
| Steam | A desktop build uploaded to a Steamworks depot | SteamCMD, see [Steam](#steam) |

## Building from source

The editor and the desktop player need the tools in the first row. The Android and browser players
are optional: configure builds each one when it finds its tools and prints `Android player: on` or
`Web player: on`, or names what is missing. A source-built editor deploys to Android or the web only
when it built that player.

| To build | Windows | Linux |
|---|---|---|
| Editor and desktop player | Git, CMake 3.21 or newer, Ninja, Visual Studio 2022 (17.5 or newer) or 2026 with **Desktop development with C++**, and the .NET SDK 10 (`install_dotnet.ps1`) | Git, CMake 3.21 or newer, Ninja, GCC 12+ or Clang 15+, the packages below, and the .NET SDK 10 (`install_dotnet.sh`) |
| Android player | Android Studio with NDK 30.0.16248370, or `install_android_studio.ps1`, see [Android](#android) | `install_android_studio.sh`, see [Android](#android) |
| Browser player | `install_web_tools.ps1`, see [Browser](#browser) | `install_web_tools.sh` and Python 3.10 or newer, see [Browser](#browser) |

The setup scripts are in the repository root. Run the `.sh` ones as your own user with
`bash <script>.sh` (they ask for `sudo` themselves), and the `.ps1` ones with
`powershell -ExecutionPolicy Bypass -File <script>.ps1`, which allows the script for that run only.
The editor also builds with the .NET SDK 9; the Android player needs 10. Ubuntu 20.04's CMake is
3.16: take a newer one from [CMake's releases](https://github.com/Kitware/CMake/releases).

**Windows**

Run the commands from an **x64 Native Tools Command Prompt** of Visual Studio, which puts the MSVC
headers and libraries on the path:

```bat
git clone --recursive https://github.com/unravel-dev/UnravelEngine.git
cd UnravelEngine
powershell -ExecutionPolicy Bypass -File install_dotnet.ps1
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

When `install_dotnet.ps1` installs .NET, open a new prompt before `cmake`, so it finds `dotnet`.
That builds with MSVC. The release packages build with clang against the same MSVC libraries: install
[LLVM](https://github.com/llvm/llvm-project/releases) and configure with
`cmake --preset windows-x64-clang-msvc-gnu -B build/Release -DCMAKE_BUILD_TYPE=Release` instead.

**Linux**

Install a compiler (for example `sudo apt install g++-12` on Ubuntu 22.04), the development
packages and the .NET SDK, then build:

```bash
sudo apt install git cmake ninja-build pkg-config curl unzip python3 \
  libglew-dev libgtk-3-dev libasound2-dev libjack-dev libudev-dev libdbus-1-dev zlib1g-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrandr-dev libxcursor-dev \
  libxinerama-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev libglu1-mesa-dev libpipewire-0.3-dev
git clone --recursive https://github.com/unravel-dev/UnravelEngine.git
cd UnravelEngine
bash install_dotnet.sh
source ~/.profile
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

On Ubuntu 20.04, install `libpipewire-0.2-dev` instead of `libpipewire-0.3-dev`. Run
`install_dotnet.sh` as your own user, not with `sudo`: it installs .NET 10 into `~/.dotnet` and asks
for `sudo` itself.

**macOS**

CI builds the editor with Xcode 16 and the `macos-arm64` preset; there is no macOS package.

**Output and tests**

The editor, the player and their data end up in `build/Release/bin`. `build/Release/bin/unravel-tests`
runs every test suite, and `--suite <text>` only the suites whose names contain the text.
`cmake -DRUNTIME_DIR=build/Release/bin -P cmake/check_package.cmake` checks that the folder holds what
the editor and its deploys need; the release workflow runs the same check on every package.

[CMakePresets.json](CMakePresets.json) holds the compiler presets the CI uses. A preset is a Release
build (`cmake --preset linux-gcc-13`, then `cmake --build --preset linux-gcc-13-release`) and its
`-debug` twin a Debug build (`cmake --preset linux-gcc-13-debug`, then
`cmake --build --preset linux-gcc-13-debug`); each builds into its own `develop/build/<preset>`. The
windows, linux and macos workflows build the Release presets and run `unravel-tests`; they run only
by hand (Actions > Run workflow).

## Android

The editor deploys games to Android 10 or newer on arm64 devices, rendering with Vulkan. It needs
the Android SDK platform tools and a JDK 17 or newer (the Deploy window names an older Java it
finds instead). The first Android deploy on a computer downloads Gradle and the Android build tools,
which takes a few minutes and an internet connection. A source build also needs Android NDK
`30.0.16248370` and the .NET 10 SDK to build the Android player next to the editor: configure
reports `Android player: on` when it finds them. The player builds only in single-configuration
build trees (such as Ninja) and needs Ninja installed.

**Windows**

1. Install [Android Studio](https://developer.android.com/studio). It puts the SDK in
   `%LOCALAPPDATA%\Android\Sdk` and bundles a JDK; the editor finds both there.
2. For source builds, open **SDK Manager > SDK Tools**, check **Show Package Details** and
   install **NDK (Side by side) 30.0.16248370**.
3. Configure the build again.

Without Android Studio, or to skip the SDK Manager, run the setup script from the repository instead:

```bat
powershell -ExecutionPolicy Bypass -File install_android_studio.ps1
```

It installs the SDK command-line tools, the platform tools and the NDK into
`%LOCALAPPDATA%\Android\Sdk` (or `ANDROID_HOME`), the folder Android Studio uses. When it finds no
JDK, neither `JAVA_HOME` nor Android Studio's, it installs JDK 17 with winget and sets `JAVA_HOME`.

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

## Browser

A web settings profile builds a folder for any static web host: the browser player, which
renders with WebGPU, with the game's data beside it. **Deploy and Run** serves that folder on
this computer and opens it in the default browser.

**WebGL 2 where WebGPU is unavailable**

A profile's **Graphics** setting can be **WebGPU, Then WebGL 2**: the deploy then also cooks the
shaders for WebGL 2, and the page starts WebGL 2 in a browser without WebGPU (an older browser,
a blocked graphics driver, a page served without HTTPS). WebGL 2 has no compute shaders, so
global illumination, screen space effects, ambient occlusion, auto exposure and volumetric fog
are off there. **When WebGL 2 Stands In** says what the page tells the player: a short notice by
default, a question before the download, or nothing. **Run With** starts Deploy and Run and the
public link on WebGL 2, to see that version on a computer whose browser has WebGPU; scripts read
`Graphics.backend` and `Graphics.fallbackReason`.

**Public link for testing**

To play the build on a phone, or send it to someone, the Deploy window's web profile has
**Share Public Link**: the editor serves the profile's last deploy and opens a Cloudflare quick
tunnel to it, a `https://<name>.trycloudflare.com` address that works until you stop sharing or
close the editor. Browsers allow WebGPU only over HTTPS, which the tunnel provides. It needs no
Cloudflare account, only `cloudflared`, installed once:

```powershell
winget install --id Cloudflare.cloudflared
```

On macOS run `brew install cloudflared`; elsewhere download it from its
[releases](https://github.com/cloudflare/cloudflared/releases). The editor looks for it on
`PATH` and, on Windows, where its installer puts it; **Editor Settings > External Tools** can name
another one. Quick tunnels are for testing; for players, upload the folder to a web host.

**Building the browser player from source**

The browser player compiles with the Emscripten of the .NET 11 SDK's `wasm-tools` workload and links
the .NET 11 browser runtime. That SDK goes into `.dotnet-web` in your home folder, beside the .NET the
rest of the build uses, in the version [release.yml](.github/workflows/release.yml) pins as
`WEB_DOTNET_SDK_VERSION`; configure takes it from there. The setup script installs both, on Linux:

```bash
bash install_web_tools.sh
```

and on Windows:

```bat
powershell -ExecutionPolicy Bypass -File install_web_tools.ps1
```

On Linux it also installs Ninja and Python from apt. `--cloudflared` (`-Cloudflared` on Windows)
also installs `cloudflared` for the public link, or updates it. Then configure again, and it reports
`Web player: on`. An SDK with the workload in another folder works too: name its `dotnet` with
`-DUNRAVEL_WEB_DOTNET`. The workload brings Node.js, and on Windows and macOS Python as well. On
Linux, Emscripten runs on the `python3` on the `PATH`, which must be 3.10 or newer; Ubuntu 20.04 has
3.8, so name a newer one with `-DUNRAVEL_WEB_PYTHON`. Like the Android player, the browser player
builds only in single-configuration build trees (such as Ninja).

## Steam

A desktop profile can upload its build to Steam with Valve's
[SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD). Set the profile's Steam app id, depot,
account and branch in the Deploy window, deploy the profile, then use **Upload to Steam**. The editor
runs the `steamcmd` on `PATH`, or the one **Editor Settings > External Tools** names, and logs in with
the login SteamCMD keeps for that account, so no password is stored: log the account in once in a
terminal with `steamcmd +login <account>`. An upload sets the build live only on the profile's branch;
Steam allows setting the default branch live only on the Steamworks website.

## Editor MCP server

While the editor runs, it serves MCP (JSON-RPC over HTTP) at `http://127.0.0.1:27182/mcp`,
bound to localhost only. **Windows > MCP Server** shows its status, the endpoint and an activity log.

## Troubleshooting

- **`dotnet` not found or scripts do not compile** - install the .NET SDK and check that
  `dotnet --info` lists an SDK, not only a runtime.
- **Submodule errors** - run `git submodule update --init --recursive`.
- **`Android player: off`** - the configure message names the missing tool (NDK, `dotnet` or Ninja).
- **`Web player: off`** - the configure message names what is missing: the .NET 11 SDK, its
  `wasm-tools` workload, Python 3.10 or Ninja.

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
[miniaudio](https://github.com/mackron/miniaudio),
[FreeType](https://freetype.org),
[GLM](https://github.com/g-truc/glm),
[spdlog](https://github.com/gabime/spdlog),
[ser20](https://github.com/unravel-dev/ser20),
[yaml-cpp](https://github.com/jbeder/yaml-cpp),
[zstd](https://github.com/facebook/zstd)

[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) holds the licenses of everything a game ships with. The engine
compiles it in and leaves out the libraries a build does not link. Scripts read it from
`Application.thirdPartyNotices`, and Deploy writes no file of it, so a game shows it on a credits or licenses screen.
Add new dependencies to it. The editor packages carry it beside [LICENSE.txt](LICENSE.txt).

The engine links no library under the LGPL: audiopp builds without OpenAL Soft (`deps/audiopp/CMakeLists.txt`) and
cpptrace without libdwarf (`deps/3rdparty/cpptrace/CMakeLists.txt`), so the notices carry no LGPL text. A build that
links either names its source and the engine version, so players can relink the game's player from the public engine;
ship such a build from a commit that is public. A closed fork must give its players what they need to relink it some
other way.

## License

Unravel Engine is released under the [MIT License](LICENSE.txt).
