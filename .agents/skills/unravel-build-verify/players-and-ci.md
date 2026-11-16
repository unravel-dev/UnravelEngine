# Player templates, browser page tests, workflows and packages

## Player templates

The editor's deploys copy a player template from `<bin>/players/` (desktop: the
`unravel-player` beside the editor). Each template carries a stamp with the engine version;
a deploy refuses a player whose version differs from the editor's (`editor/core/deploy/files.h`),
so rebuild the templates after a commit.

Both sub-builds below:

- configure again after every host configure (a stamp file the host configure touches), so
  files added since reach them;
- wait for the `editor` target while `UNRAVEL_PLAYERS_AFTER_EDITOR` is on (the default);
  `-DUNRAVEL_PLAYERS_AFTER_EDITOR=OFF` builds one without the editor, as the release
  workflow's shared players do;
- keep their FetchContent dependencies in their own `_deps` folder;
- take `AUTO` (build when the tools are found, else say why not), `ON` (fail the configure
  when a tool is missing) or `OFF`.

### Web (`cmake/web_player.cmake`)

- `UNRAVEL_WEB_PLAYER`. An Emscripten sub-build in `<tree>/web`, always Release, compiles the
  game with the Emscripten of a .NET SDK that has the wasm-tools workload.
- `UNRAVEL_WEB_DOTNET` names that SDK's `dotnet`; empty: the one `install_web_tools.ps1` or
  `install_web_tools.sh` put in `.dotnet-web` in the home folder, else the build's. The .NET
  major is `UNRAVEL_WEB_DOTNET_MAJOR`.
- `UNRAVEL_WEB_PYTHON` (3.10 or newer) and `UNRAVEL_WEB_NODE` name the Python and Node.js
  Emscripten runs on; empty: the workload's, else the `PATH`'s. The configure says when the
  Python is too old.
- The `web_player` target compiles and weaves the engine scripts, relinks the .NET browser
  runtime with the objects the sub-build lists as the game's link inputs
  (`<tree>/web/web_link_inputs.txt`), and writes `<bin>/players/web` (page, runtime, stamp).
  It needs the script bridge target (`clrpp_managed`).

### Android (`cmake/android_player.cmake`)

- `UNRAVEL_ANDROID_PLAYER`. The NDK of `UNRAVEL_ANDROID_NDK_VERSION` from `ANDROID_NDK_HOME`,
  `ANDROID_NDK_ROOT` or the SDK (`install_android_studio.ps1` / `.sh`).
- The sub-build in `<tree>/android-arm64` uses the host tree's `CMAKE_BUILD_TYPE`; a Debug
  arm64 player is large and slow, which is the user's trade to make.
- The `android_player` target writes `<bin>/players/android-arm64`: the stripped library, the
  Gradle project, SDL's Java sources, the .NET runtime and the stamp.

## Browser page tests

The page's own scripts (`game/web/wwwroot/`) have Node tests that need no build:

```bash
node --test game/web/tests/*.mjs
```

Run them after changing `data.js`, `graphics.js`, `audio.js` or what `main.js` takes from them.

## Workflows

All five in `.github/workflows/` run by hand; `samples.yml` also runs every day at 10:00 UTC,
the one schedule the user allows.

| Workflow | What a run does |
|----------|-----------------|
| `windows.yml` | MSVC 2022 and clang GNU-driver legs, Release preset, player templates off, then every suite |
| `linux.yml` | Ubuntu 22.04 and 24.04, GCC and clang legs, then every suite |
| `macos.yml` | Apple silicon runners, then every suite |
| `release.yml` | editor packages for Windows and Ubuntu 20.04 to 26.04, uploaded to a GitHub release with `latest.json` and `samples.json` |
| `samples.yml` | `samples.json`, the start page's sample projects, attached to the latest release; daily at 10:00 UTC and by hand |

- `gh workflow run <file> --ref <branch>`. `release.yml` takes `-f version=<version>` (what
  git describe prints, a commit, a branch or a tag) and one checkbox per platform.
  `gh run view <id> --log-failed` reads a failed job.
- The CI presets write to `develop/build/<preset>`; locally, the configured trees are the
  reference.
- Release: Windows uses the preset `windows-x64-clang-msvc-gnu`; Linux builds inside an Ubuntu
  container so a package needs no newer glibc than its release. Ubuntu 20.04 builds with
  libstdc++ 10 (no `std::bit_cast`) and a standalone Python for the web player. The Android and
  web players can be built once on Windows and shared by every package.
- Samples: every public `UnravelEngine-Sample-*` repository with a `sample.json` and a
  picture (`thumbnail.jpg`) at its root is a sample; `.github/scripts/sample_catalog.py`
  documents the fields and checks them as the editor does. Publishing a new version of a
  sample means raising `version` in its `sample.json`: a copy keeps the `sample.json` it was
  made with, and the start page marks a copy of a lower version "Update available". The daily `samples.yml` run
  publishes a new or changed card; run it by hand to publish one at once. Before a release,
  refresh the copy the editor ships from
  the clones (their `sample.json` and picture committed and pushed):
  `python .github/scripts/sample_catalog.py --local-root <clones folder> --output editor_data/samples/samples.json --thumbnails-dir editor_data/samples`.

## Packages

- `cmake --build <tree> --target package` writes the CPack zip of the runtime folder (the
  test runner, `data/tests` and unshipped executables left out).
- `cmake/check_package.cmake` refuses a package that lacks what the editor or a deploy needs,
  or whose players have another engine version than its editor:
  `cmake -DPACKAGE_ARCHIVE=<zip> -P cmake/check_package.cmake`, or `-DRUNTIME_DIR=<bin>` for a
  build's runtime folder (licence and notices are not checked there).
