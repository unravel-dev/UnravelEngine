# Texture and mesh import traps

Format-specific contracts of the texture and mesh compiles. General compile rules are in
[SKILL.md](SKILL.md).

## Textures

- High quality BC textures compile to BC1 (RGB) and BC3 (RGBA), never BC7 (`select_bc_format` in
  `editor/core/asset_compiler/asset_compiler.cpp`): BC7 encodes too slowly for an import default.
  A cost documented in code is a decision; changing it is the user's call.
- texturec rounds a compressed texture's size up to whole blocks, before and after the `--max` cap,
  and resamples the image to that size (no padding). A variant must keep the size of the texture it
  stands in for, since UI sprite rectangles are measured in texels: `select_astc_ldr_format` picks the
  most compressed ASTC block whose rounded size equals the unblocked size (`get_texturec_output_size`,
  `editor/core/asset_compiler/texture_target.cpp`), 4x4 otherwise. A new block format or a texturec
  option that changes the size goes through the same check. BC's 4x4 blocks resample sizes that are
  not multiples of 4.
- A texture tagged sRGB compiles only to a format every renderer samples through an sRGB view
  (`has_srgb_view`, `select_srgb_format`): BC4/BC5 become BC1, EAC becomes ETC2, R8/RG8 become RGBA8.
  The engine loader drops the tag of a format without a view and samples it raw, which reads too light,
  and never fails the texture.
- The shaders read a normal map with green pointing up the image (`NORMAL_MAP_Y_UP`,
  `engine_data/data/shaders/shaderlib.sh`): the importer's left-handed conversion leaves the bitangent
  pointing down the image, so the sampled Y is negated before Z is rebuilt, the same for BC5, ASTC and
  RGBA8. A green-down (DirectX) source compiles with its Y turned over. The source's format does not
  decide which a file is: glTF files with green-down maps exist, and packs mix both.
- `texture_importer_meta::normal_y` is Auto, Up or Down. The texture compile resolves Auto every time
  it compiles a normal map (`load_normal_map_to_invert`, `asset_compiler.cpp`): a convention word in
  the file's name first (`_nor_gl`, `NormalDX`), else a test on the texels that answers unknown
  rather than guess (`editor/core/asset_compiler/normal_map_green.h`). So a map is read whichever way
  it became a normal map, for every platform variant, and again when its source changes. A `.meta`
  saved with the `invert_normal_y` checkbox loads true as Down and false as Auto.
- Only a normal map on Auto has `reads_normal_y` in its compile parameters, which end in the
  reading's version (`NORMAL_Y_READING_VERSION`, `texture_target.cpp`). A change to the test or its
  thresholds raises that version, which compiles those maps again and no other texture. Check the
  change on maps whose convention is known (`UNRAVEL_NORMAL_MAP_CORPUS`, suite `normal map green`):
  maps baked for one object reach a score of about 0.1 with either sign, which is why the bar is 0.20.
- The texel test reads only surface, and the compile has no mesh. A baked map is often more than half
  padding (what the baker paints around the UV islands), which outvotes the islands. The mesh
  importer has the UVs: it marks a `normal_map_coverage` (the cells of a 128 x 128 grid in which the
  first-UV vertices of the meshes that bind the map lie) and, where the drawn texels read differently
  from the whole map, writes Up or Down into the `.meta` (`find_normal_y_to_record`), which the
  compile follows. Every other map stays on Auto and is not decoded at import. A map with vertices
  outside the UV square repeats and has no padding; one whose vertices mark under a tenth of the grid
  tells too little. A Normal Y someone chose is never changed by an import.
- The project audit's `texture.normal_green_down` rule reads the compiled file, without a mesh, and
  reports a normal map that still reads green down: an Up or Down that states the wrong direction,
  or a map Auto could not tell.
- bimg parses a KTX2 ETC2 RGB container as ETC1, which Vulkan and Direct3D cannot sample and which has
  no sRGB view. The engine loader maps ETC1 to ETC2 after every parse (`readEtc1AsEtc2`,
  `engine/core/graphics/utils/bgfx_utils.cpp`); a new load path through bimg does the same.
- `bimg::imageWritePng` with R8 data writes an RGBA header, a corrupt grayscale PNG: tests write their
  grayscale PNGs by hand.
- Tests read compiled formats through the engine's parse functions (the ones in `bgfx_utils.cpp`), so
  they see what the engine loads, not what bimg alone reports.

## Meshes

- assimp's logger is one process-wide object whose mutexes are compiled out
  (`ASSIMP_BUILD_SINGLETHREADED` is forced in assimp's `defs.h`), while mesh compiles run in parallel.
  Never create, set or kill `Assimp::DefaultLogger` around an import. Per-import logs need one custom
  `Assimp::Logger` installed once at startup (`set`, then `set(nullptr)` at shutdown, never `kill`) that
  routes to a `thread_local` per-import sink set by a scope around the import; pool workers such as
  the texture jobs do not inherit the `thread_local`.
- The importer drops faces and bone weights that index past `mNumVertices` (with a warning) and skips
  node mesh indices past the scene's meshes; keep both checks in any new geometry path
  (`editor/core/asset_compiler/importers/mesh/mesh_import_geometry.cpp`).
- A file without meshes (animation-only, or Import Meshes off) compiles its clips and materials and an
  empty model; loading accepts that model.
- One draw takes at most `gfx::get_max_blend_transforms()` bones. `mesh::apply_skin_to_load_data`
  (`engine/engine/rendering/mesh.cpp`) builds a palette per submesh, splits a submesh whose faces reach
  more bones into pieces that fit, and keeps each vertex's four strongest weights scaled to sum to one.
  Skin and palette changes go through it, and it runs before LODs and fields are generated. A limit is
  checked against the palette index, never the global bone index.
- A model's default materials are generated by-products named `<model stem>_<material>.mat` beside
  the model (`get_default_material_path`, `editor/core/asset_compiler/asset_compiler.cpp`); their
  rules are the generated-file gotchas in [SKILL.md](SKILL.md).
- A change to the mesh compile output (geometry, SDF bake, cards, skin) bumps
  `ex::get_format_version<mesh>()`; the comment there lists what each version changed.

## Verify

- `unravel-tests --suite "mesh import heuristics"`, `--suite "mesh skinning"`,
  `--suite "mesh shapes"` (`--bench` compiles every built-in shape and compares each field with the
  run-time bake), `--suite "settings profiles"` for texture formats per target,
  `--suite "normal map green"` for the green direction test and the import that applies it.
- An importer refactor keeps the output byte for byte: run `--suite "mesh import corpus digest"` with
  `UNRAVEL_MESH_IMPORT_CORPUS=<list>` on the build before and after the change and diff the two
  digests (unset, the suite is skipped).
