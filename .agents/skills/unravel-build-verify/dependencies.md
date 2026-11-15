# First-party library suites and bgfx

The rules (what may be edited, where a library's tests go, who commits a submodule) are in
`deps/AGENTS.md`. This file holds how.

## A first-party library's suite

The engine tree turns every library's tests off (`deps/threadpp/CMakeLists.txt` and its
siblings force `BUILD_<LIB>_TESTS` to `OFF`). A library's suite builds from that library's
own folder, where it is the main project and its tests default on. That build is the one
tree outside the configured trees: put it in the session scratchpad, not in the repository.

```bash
cmake -S deps/threadpp/threadpp -B <scratch>/threadpp -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build <scratch>/threadpp
<scratch>/threadpp/bin/threadpp_test
```

- Test executables: `threadpp_test`, `audiopp_test`, `ospp_test`, `hpp_test`, and
  `monopp_test` for dotnetpp.
- Tests use suitepp (`TEST_CASE`, `EXPECT`, header `suitepp/suite.hpp`). A library without it
  gets it the way audiopp has it: the submodule `3rdparty/suitepp`, `add_subdirectory(suitepp)`
  in its `3rdparty/CMakeLists.txt` inside the `BUILD_<LIB>_TESTS` block, and the test target
  links `suitepp`.
- dotnetpp: configure once per backend (`-DDOTNETPP_BACKEND=coreclr` and `mono`) and build
  `copy_resources monopp_test`. `copy_resources` copies `tests/managed/*.cs` into
  `bin/managed`, which `monopp_test` compiles when it runs, so a stale copy fails new tests.
  Some checks run on CoreCLR only.
- A MinGW build runs with the MinGW `bin` folder on `PATH`: its runtime DLLs are not copied.

## bgfx

How it is wired:

- `deps/3rdparty/bgfx/bgfx` is the bgfx.cmake fork (a submodule). Its `CMakeLists.txt` pins
  bx, bimg and bgfx, one `FetchContent_Declare(<name> GIT_REPOSITORY ... GIT_TAG <sha>)` line
  each.
- `deps/3rdparty/bgfx/CMakeLists.txt` reads the bgfx pin from the fork with a regex and
  declares bgfx first, with a patch step; the fork's own declare then does nothing. bgfx build
  options (`BGFX_CONFIG_*`) are `add_definitions` there, never patches.
- The checkout is `<FETCHCONTENT_BASE_DIR>/bgfx-src`; the tree's `CMakeCache.txt` names the
  base folder (default `<tree>/_deps`; the player sub-builds have their own).
- `deps/3rdparty/bgfx/apply_patches.cmake` keeps "HEAD and patch-set hash" in the checkout's
  git folder (`unravel-applied-patches`). An unchanged state touches no file; otherwise it
  resets the checkout and applies each `deps/3rdparty/bgfx/patches/*.patch` in name order,
  failing the configure on the first patch that does not apply. Patched files rebuild bgfx and
  everything that links it.
- `deps/3rdparty/bgfx/reset_moved_checkout.cmake` resets a checkout whose HEAD is not the pin
  before FetchContent updates it; FetchContent's own update stashes the patches and fails to
  pop them on the new commit. It skips disconnected builds and `FETCHCONTENT_SOURCE_DIR_BGFX`;
  a bgfx from `BGFX_DIR` or an own checkout is built unpatched.

Moving the pin:

1. Move the fork first: fast-forward it to upstream bgfx.cmake. When a needed fix is newer
   than its pins, move bgfx, bx and bimg together to matching heads; moving only the inner
   pins can break the fork's CMake files. The fork's commit and push and the engine's
   submodule pointer are the user's.
2. Re-configure each tree. A patch that does not apply to the new source stops the configure: regenerate it
   by applying the set in order to the new source in a scratch git repository and taking
   `git diff` per patch; drop a patch upstream took.
3. When `BGFX_SHADER_BIN_VERSION` changed, bump `ex::get_format_version<gfx::shader>()` in
   `engine/engine/assets/impl/asset_extensions.h` so every shader compiles again.
4. Build everything and run `eshader files` and `render feature shaders`; rendering checks
   are in `unravel-rendering`.

Writing a patch:

- Edit the checkout, then write a plain-text description, a blank line and `git diff -- <files>`
  (`diff --git` hunks without `index` lines). Line endings LF (`.gitattributes`:
  `*.patch text eol=lf`).
- A new patch reaches a tree at its next configure, or at once:
  `cmake -DSOURCE_DIR=<checkout> -DPATCH_DIR=deps/3rdparty/bgfx/patches -P deps/3rdparty/bgfx/apply_patches.cmake`.
