# Assets Reference

## Common asset types

| Type | Typical source | Header |
|------|----------------|--------|
| Texture | `.png`, `.jpg`, etc. | `engine/core/graphics/texture.h` |
| Mesh | `.fbx`, `.gltf`, etc. | importers in `impl/importers/` |
| Material | `.material` | `engine/engine/rendering/material.h` |
| Shader | `.sc` | compiled to `engine_data/compiled/shaders/` |
| Animation | clips | `engine/engine/animation/animation.h` |
| Prefab | scene template | `engine/engine/ecs/prefab.h` |
| UI document | `.rml`, `.rcss` | `engine/engine/ui/` |

## Meta file rules

- Stored adjacent to source: `model.fbx` -> `model.fbx.meta`
- `asset_writer.h` appends `.meta` if missing extension
- UID assigned on first import; preserve across reimports
- Importer settings serialized in meta JSON

## Adding a new importer

1. Implement importer in `engine/engine/assets/impl/importers/`
2. Register type in asset extension tables
3. Register compiler specialization if needed
4. Add content browser create/import menu entry if user-facing
5. Add thumbnail support in `thumbnail_manager` if visual asset
6. Meta schema for importer-specific settings

## Asset database

- Groups and protocols in `asset_manager`
- `asset_storage` maps UID -> location + meta
- Scene/prefab files reference assets by UID through handles

## Git LFS

Large binary assets may use Git LFS. Do not commit huge blobs without LFS configuration.
