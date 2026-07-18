---
name: unravel-assets
description: >-
  Manages UnravelEngine asset pipeline: importers, .meta sidecars, UID stability,
  asset compilation, async loading, and reimport. Use for asset types, import
  errors, content browser issues, or compiled .asset output.
disable-model-invocation: true
---

# Unravel Assets

## Start here

| Purpose | Path |
|---------|------|
| Asset manager | `engine/engine/assets/asset_manager.h` |
| Asset handles | `engine/engine/assets/asset_handle.h` |
| Asset storage / UID | `engine/engine/assets/asset_storage.h` |
| Compiler API | `engine/engine/assets/impl/asset_compiler.h` |
| Importers | `engine/engine/assets/impl/importers/` |
| Extensions / .meta | `engine/engine/assets/impl/asset_extensions.h` |
| Writer (meta IO) | `engine/engine/assets/impl/asset_writer.h` |
| Dependencies | `engine/engine/assets/impl/asset_dependencies.h` |
| Editor watcher | `editor/editor/assets/asset_watcher` |
| Content browser | `editor/editor/hub/panels/content_browser_panel/` |
| Thumbnails | `editor/editor/editing/thumbnail_manager.h` |

## Asset model

Every source asset has a **`.meta` sidecar** (JSON):

- `uid` — stable unique ID (never change casually)
- `type` — asset type name
- `importer` settings per type

Meta extension constant: `.meta` in `asset_extensions.h`.

Compiled output: `.asset` blobs (plus platform-specific shader variants).

## Compilation flow

```cpp
asset_compiler::compile<T>(source_path, ...);
```

1. Read source + `.meta`
2. Run type-specific importer
3. Write compiled `.asset` to cache
4. Update dependency graph

Importers live in `engine/engine/assets/impl/importers/`.

## Loading modes

`asset_manager` supports sync and async/deferred loading. Hot paths (frame render) must not block on uncached loads.

`asset_handle<T>` — typed reference resolved through asset database.

## Editor integration

- **Content browser** — browse, import, reimport, rename, delete
- **Asset watcher** — file changes trigger reimport (`editor/editor/assets/`)
- **Thumbnails** — `thumbnail_manager` generates previews

Context menu actions in `content_browser_panel.cpp`.

## Protocols and paths

Asset manager uses virtual filesystem protocols for engine vs project vs editor data. Respect protocol boundaries — do not hardcode absolute paths.

Runtime data copied by CMake:

- `engine_data/` → `build/bin/data/engine`
- `editor_data/` → `build/bin/data/editor`

## Verification checklist

- [ ] `.meta` created/updated with valid `uid` and `type`
- [ ] Import produces expected `.asset` output
- [ ] Reimport from content browser succeeds
- [ ] `asset_handle<T>` resolves at runtime
- [ ] Async load does not stall main thread
- [ ] Dependency tracking updated for nested assets
- [ ] Content browser shows correct icon/thumbnail

## Common mistakes

- Regenerating UIDs (breaks scene references)
- Editing `.asset` blobs instead of source + reimport
- Missing importer for new file extension
- Sync load in render loop
- Forgetting CMake data copy for new `engine_data` files
- `.meta` path mismatch (must be `filename.ext.meta`)

## Deep reference

See [reference.md](reference.md) for importer patterns.
