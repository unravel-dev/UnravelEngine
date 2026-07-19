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

## MCP tools

| Tool | Purpose |
|------|---------|
| `assets_list_batch` | List by protocol (`app`/`engine`/`editor`); optional `type` |
| `assets_find_batch` | Search **any** asset type: `protocol` (incl. `all`), `type`, `prefix`, `name_contains`, `limit` |
| `assets_get_batch` | Metadata for many (`items`: `key` / `uid`) |
| `assets_list_types` | Known extensions (`.mat`, `.pfb`, `.emesh`, …) |
| `assets_list_embedded_primitives` | Names for `scene_create_primitives_batch` |
| `assets_create_folder` | Create folder under protocol path |
| `assets_import_files` | Copy external files/folders into `app:/...` (content-browser Import); waits for copy + ready |
| `assets_reimport_batch` | Reimport many keys (+ optional `wait_ms`) |
| `assets_wait_ready_batch` | Poll until material/mesh/prefab (or meta/file) keys are ready |
| `assets_get_mesh_info` | Mesh local AABB |
| `prefabs_create_from_entities_batch` | Save entity hierarchies as `.pfb` |
| `window_request_focus` | Focus/raise editor OS window (watcher gated when unfocused) |

If create/reimport/import appears stuck while agent app has focus, call `window_request_focus` then `assets_wait_ready_batch`.

### Import workflow (required)

**Never download or write fetched assets directly into the project (`app:/`).**

1. Download/stage to an OS temp (or other non-project) directory.
2. Call `assets_import_files` with those absolute paths and a destination `folder` under `app:/`.
3. Use returned `key`s; optionally `assets_wait_ready_batch` if needed.

`assets_import_files` rejects source paths under the project root. It uses `editor_actions::import_files` / `wait_import_jobs` (content-browser Import parity), focuses the editor window, waits for async copies, then polls until ready (`wait_ms`, default 15000).

`type` filters accept `mat`, `.mat`, `pfb`, `emesh`, `etex`, `cs`, `spfb`, etc. (aliases normalized).

Code: `editor/editor/system/mcp/mcp_tools_assets.cpp`.

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
