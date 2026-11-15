# Add an asset type, an importer or an import setting

Each asset type is listed in a fixed set of places. To find every one, search for an existing type
of the same kind (`style_sheet` for a text asset, `physics_material` for an engine-authored one,
`audio_clip` for an imported one with settings) across `engine/engine/assets/`,
`engine/engine/meta/assets/`, `editor/core/` and `editor/editor/`.

## A new asset type

1. Formats (`engine/engine/assets/impl/asset_extensions.h`): a `get_suported_formats<T>`
   specialization, its entry in `get_all_formats`, the type name in `get_type<T>` and
   `get_type(ext)`, and the text or binary answer in `is_binary` (text files are fingerprinted with
   line endings normalized). A compiled format also gets a `get_format_version<T>` specialization and
   a branch in the extension overload.
2. Storage: `add_storage<T>()` with its load function in `asset_manager::init`
   (`engine/engine/assets/asset_manager.cpp`).
3. Loading: `load_from_file<T>` in `engine/engine/assets/impl/asset_reader.cpp`.
4. Compiling (`editor/core/asset_compiler/asset_compiler.cpp`): a `compile<T>` specialization, a
   `create_default_importer` branch when the type has settings, and `read_importer<T>` through
   `read_or_create_importer`. A type that includes other files gets `resolve_dependencies<T>`
   (`editor/core/asset_compiler/asset_dependencies.cpp`) and a dependency watcher in
   `asset_watcher::setup_meta_syncer`.
5. Watching: a `watch_synced<T>` call in `asset_watcher::setup_cache_syncer`
   (`editor/editor/assets/asset_watcher.cpp`).
6. Editor: icon (`editor/editor/assets/asset_icons.cpp`), Content Browser create and open
   (`editor/editor/hub/panels/content_browser_panel/content_browser_panel.cpp`), thumbnail
   (`editor/editor/editing/thumbnail_manager.cpp`), and the lists in `editor/editor/assets/asset_actions.cpp`.
7. Deploy: the reference walk in `editor/core/deploy/content.cpp` when the type names other assets.
8. Generated files: a text type that can name another file joins the formats
   `generated_asset::remove_unreferenced_files` scans (`editor/core/asset_compiler/generated_asset.cpp`).
9. MCP type names and readiness (`editor/editor/system/mcp/mcp_tools_assets.cpp`,
   `editor/editor/system/mcp/mcp_asset_wait.cpp`): skill `unravel-editor-mcp`.
10. Script members that hold the asset: skill `unravel-scripting` (serializer, inspector, MCP).

## A new import setting

- Field with its default in the `*_importer_meta` (`engine/engine/assets/asset_storage.h`).
- SAVE and LOAD in `engine/engine/meta/assets/<type>_importer_meta.cpp`, appended last in both
  (binary archives read by position), plus its reflection entry for the inspector.
- A `.meta` that sets the field is fingerprinted, so editing it recompiles. A new or changed
  default that alters output is not: `.meta` files that never wrote the field do not change. Bump
  `ex::get_format_version<T>()` for that.
- A setting that falls back to a project default (texture compression, audio codec) goes into the
  compile-parameters string the manifest records, so a change of the project default recompiles
  (`needs_recompilation` in `asset_watcher.cpp` compares it).

## A new importer setting type or importer

- A new `*_importer_meta` or `*_header_info` derives `crtp_meta_type<leaf, base>` and registers with
  `SERIALIZE_REGISTER_TYPE_WITH_NAME` (`engine/engine/meta/assets/asset_importer_meta.hpp`,
  `engine/engine/meta/assets/asset_header_info.hpp`).
- Code reading it casts with `safe_pointer_cast<leaf>` (`engine/core/reflection/registration.h`).
- A new source extension for an existing type joins that type's `get_suported_formats<T>` list; a
  mesh format goes through assimp (`editor/core/asset_compiler/importers/mesh_importer.cpp`).

## Verify

- `unravel-tests --suite "asset writer"` and the suite of the type (`generated assets`,
  `mesh import heuristics`, `audio clip encoding`, `settings profiles`, ...).
- In an editor: create or import one asset of the type, edit its `.meta` in the inspector and see it
  recompile once, rename it in the Content Browser and see references follow, reopen the project and
  see no recompile.
