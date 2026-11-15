# deps/

First-party libraries (`ospp`, `threadpp`, `audiopp`, `dotnetpp`, `hpp`, `filedialog`) and
third-party code in `3rdparty/`. Most libraries are git submodules with their own
repositories.

- `3rdparty/` is not edited. A fix to bgfx goes into `3rdparty/bgfx/patches/*.patch` (LF line
  endings, applied in file name order by `3rdparty/bgfx/apply_patches.cmake`); other
  libraries are worked around in the engine. Ask the user before changing anything else here.
- A first-party library's wrong contract is fixed inside that library, not around it in
  the engine.
- A change to a first-party library is tested in that library's own `tests/` folder
  (suitepp: `TEST_CASE` / `EXPECT`), not in `tests/` of the engine.
- A change inside a submodule is committed in that submodule's repository; the engine
  commit then moves the submodule pointer. Both happen only when the user asks.
- Before calling a platform capability missing, check `ospp/` (windows, input, paths,
  entry points). `ospp` is the only place that calls SDL.
- New libraries: only when the user names one (root `AGENTS.md`).
