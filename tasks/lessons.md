# Lessons

## atomic_save_to_file + entt::handle needs non-template overloads on Clang

- Including the meta header at the call site does not fix two-phase lookup for
  unqualified `save_to_file` inside `asset_writer::atomic_save_to_file`.
- Ordinary lookup uses the template definition context; ADL uses associated
  namespaces of the arguments. `entt::handle` / `entt::const_handle` associate
  `entt`, not `unravel`, so Clang fails even when `entity.hpp` is included.
- Do not qualify as `::unravel::save_to_file` in the template — that disables ADL
  and breaks every other asset/scene overload that relies on it.
- Prefer non-template `asset_writer::atomic_save_to_file` overloads for
  `entt::handle` / `entt::const_handle` (declared next to `save_to_file` in
  `entity.hpp`) so overload resolution never enters the template for handles.

## Windows crashes need SEH, not only std::signal

- `std::signal(SIGSEGV)` does not reliably catch native access violations on Windows;
  install `SetUnhandledExceptionFilter` (and keep CRT signals as a complement).
- Crash handlers must write with OS APIs first (`WriteFile` / `write`) to `CrashLog.txt`;
  spdlog/cpptrace symbolization is best-effort after that and may deadlock.
- Prefer minidumps (`MiniDumpWriteDump`) alongside a short stack dump for RelWithDebInfo.

## Prefer project hpp helpers over std equivalents

- Use `hpp::string_view` and `hpp::span` (not `std::string_view` / `std::span`) in engine/editor code.
- Same for other established project helpers (`hpp::optional`, `hpp::variant`) when already used in the area.

## Context lifetime

- Every type added to `rtti::context` must be removed on teardown (`engine::destroy` / `editor::destroy`).
- `engine::destroy` asserts the context is empty; leftover types print via `ctx.print_types()` and fail shutdown cleanup.

## Editor launch directory

- Do not launch from wherever CMake last wrote the `.exe` without checking runtime assets.
- CMake default is `build/bin/<Config>/`; some local trees use `build/<Config>/bin/`.
- Launch cwd must contain `data/` and `clrpp/` (else scripting/shaders fail). Prefer the
  populated bin; copy the fresh exe there when needed.
- `AGENTS.md` and `unravel-build-verify` previously disagreed on paths — keep them aligned.

## Linux process restart + terminal signals

- Do not copy Godot `create_process` `setsid()` for editor self-restart — it detaches the
  controlling TTY and breaks Ctrl+C.
- Sibling spawn + parent exit also breaks Ctrl+C: the shell's `waitpid` on the original
  PID returns and the shell reclaims the foreground.
- On Linux prefer unload then `execve` (same PID / process group). Keep Windows sibling
  spawn + `--restart-from-pid` for file-lock handoff.

## ImGui modal stacking

- Do not nest `BeginPopupModal` hosts under which `ImBox` (or other modals) must open.
  ImGui binds `OpenPopup` to the current popup stack; drawing the inner modal later at
  root level collapses/replaces the outer modal.
- Prefer a non-modal overlay window (dim + centered panel) when prompts may open on top
  (same pattern as the hub project chooser).
- Never call `ImGui::CloseCurrentPopup()` outside the matching `BeginPopupModal` block.

## Multi-file mesh import races (.gltf + .bin)

- Assimp has no public API to list external glTF URIs before `ReadFile`; scrape JSON
  yourself (`buffers`/`images` uri + `byteLength`) and wait until sidecars meet size.
- Never fall back mesh *load* to raw `.gltf`/`.obj` when `.asset` is missing — that
  binary-deserializes the source and logs `Vertex data is empty`.
- Mesh compile must fail (no manifest) when `vertex_data` is empty after import.
- A size-stable but truncated `.gltf` can yield zero URIs; treat incomplete `buffers`
  parsing as not-ready and re-read, do not skip the `.bin` wait.

## Surface Cache GI: no screen temporal for set thrash

- Camera dolly “lamp on/off” pops are from card **membership** (spawn/evict + nearest-N
  upload), not missing screen TAA. Screen history only ghosts and does not fix it.
- Stabilize with upload hysteresis (inner/outer gather radius), refuse to evict pages
  still inside spawn/gather range, and keep project/sample loops on the same upload cap.
- Never re-tile every Bistro submesh every frame — retain existing cards by distance and
  only discover unspawned surfaces (amortized). Soft-fade gather by camera membership.
- Submesh `instance_bounds` are already world-space (`mul(sm->bbox, node_world)`). Do not
  re-transform. Cull/discover with AABB closest-point distance (not center), and spawn
  nearest candidates first — center+index-order looks like “GI only near mesh origin”.
- Never protect page eviction with a scene-sized radius (`max_card_distance`). On Bistro
  that freezes the pool on origin-first fills; protect only cards closer than the surface
  being spawned so walking to edges can reclaim pages.

## Surface Cache GI: production path (probes, not view-coupled gather)

- Screen-splat + camera-budgeted per-pixel card gather is view-coupled (SSGI-like pops).
  Primary final gather must be a **persistent world probe volume** sampling the atlas.
- Card lighting (world radiance) must not require the emitter on screen; keep screen
  project as optional debug/boost only (`enable_screen_project` default off).
- Compose soft-mix: `mix(SH, probes, a)` then `mix(..., SSIL, ssil_a * near_w)` — never
  binary “cache.a or black”. Keep `ssil_near_field_weight` low so SSIL is contact detail.
- HW-RT is Phase E: `gi_ray_query` software fallback until AS exists; do not block v1.
- Never free-follow probe origins every frame — world-snap to probe spacing and invalidate
  on rebase. Free follow remaps stale bright texels → white flash that “heals” as probes refill.
- Card bounce must not accumulate: lighting overwrites direct, then one bounce add + clamp.
  Material capture must not write radiance when screen project is off.
- Compose confidence must follow soft FAR-volume coverage with a quality floor — never near-
  cascade membership alone. Per-cell snaps + texture clears make circular “lamp” chunks in
  GI Confidence. Use a large deadzone before rebasing and never wipe the probe atlas.
- Card upload must be world-isotropic + sticky row order — never view-forward score. Reshuffling
  GPU card rows / dropping behind-camera cards makes look-away→look-back restart from zero.
- Age pass must soft-decay confidence only; never clear atlas RGB. Probe updates with a weak
  gather must raise history so they do not erase healthy probes.
- Emissive GI needs a sticky emissive atlas (GBuffer2 project) + local point/spot lights in
  card lighting; sun/sky alone will not match SSIL lamp bleed.
- Camera may prioritize *update budget* only. Card residency, probe anchors, and light
  selection must be **volume-AABB keyed**. Killing cards / following probes with the camera
  recreates walk-toward-brighter / walk-away-black even with isotropic upload scores.
- Mesh PBR albedo/emissive must seed card pages on alloc (no G-buffer required). Screen
  project is refine-only; otherwise emissives never exist off-screen.
- Surface-cache card origins are absolute world positions: refresh them every frame from
  live `model_component::get_world_bounds()` (owner-anchored). Stale `instance_bounds`
  proxies leave IL stuck at the old/world origin when a mesh moves; re-anchor by
  `live_center - proxy_union_center` before spawn/refresh.
- Never write atlas/probe alpha≈1 on near-black radiance, and never floor compose
  confidence on empty probes — that replaces SH with black and makes surface-cache GI
  look darker than SSIL (which still reads the real LBUFFER).
- Whole-model AABB cards only light the outer shell; Bistro interiors need per-submesh
  cards. Two-sided card emit (`abs(n·dir)`) is required for AABB face cards.
- Probe gather must use `abs(n_recv·dir)` (or include -Y/-X/-Z). One-sided `saturate`
  with only +Y/+X/+Z never sees floors below the probe — red floor bounce stays black
  in SURFACE_CACHE_GI while emissives at mid-height still show. Coplanar skip must be
  centimeters, not `thickness*2` (~0.7m). Far-cascade inside weight must not fade a
  full cell (30m+); sharpen edge fade. Atlas page UV must be integer page origin
  (no half-texel inset) so lighting writes match `fill_page`.
- On D3D, `texture2D`/`texelFetch` of a UAV/`imageStore`-written atlas often returns
  black. Surface-cache lighting, bounce, and probe gather must `imageLoad` material,
  emissive, and radiance pages. Sampler fallback albedo 0.35 made GI a flat gray with
  no red floor bounce even when mesh seed wrote the correct color.
- Lumen split: **cards = world/volume persistent**, **final-gather probes = camera-
  centered** (world-snapped). Locking probes to the GI volume center kills IL at
  scene outskirts and makes local-volume GI translate with the mesh. Card residency
  must union a camera working set; GPU card upload must prefer camera neighborhood.
- Atlas debug (screen UV→atlas) showing mostly magenta is normal — unused page
  memory. Judge lit pages by non-magenta RGB islands, not the magenta field.
- Never write screen LBUFFER radiance into the world atlas — that makes GI look
  like unstable SSGI. Material G-buffer refine is OK; lighting must stay world.
- Compose: `ssil_near_field` must not replace a healthy cache (`ssil_w *= 1-cache_a`).
  Default near-field ~0.3. Flooring `gather_intensity` at 2.5 made the slider dead;
  probe sample confidence must not multiply energy so hard that cache_a stays ~0.
- `remove_card_at` must remap `sticky_upload_indices_` on swap-from-back or GPU
  rows point at the wrong cards and lighting pops while walking.
- Mesh seed uses untextured `get_base_color()` tint. If material refine history-
  locks seed (`a≈0.85` + `project_history≈0.85`), red albedo maps never enter
  the material atlas → achromatic radiance → no red bounce even with SSIL off.
  Prefer G-buffer albedo on refine; seed with low alpha; keep PROJECT/BOUNCE
  caps at the gather upload size (512).
- Final gather must not be probe-only: probes average chroma away. Hybrid
  card gather at the G-buffer hit (directional) + probe fill. FS sampling of
  UAV atlases on D3D needs a blit to a BLIT_DST/SRV texture first.
- Never one `gfx::render_pass` per card. Lighting all ≤128 cards × (lit+bounce)
  created ~130 views and ~20ms. Batch with `dispatch(8,8,N)` + `u_card_batch`,
  amortize pages_per_frame (~16–32), and keep full-screen card gather tiny
  (≤48, 1 tap). Point lights need plane-side reject or they punch through walls.
- Walk pops: never `stable_partition` / mid-list sticky swaps (GPU row i must
  keep the same card). Never apply `gather_intensity` in card lighting AND bounce
  AND probes. Probe rebase must keep HIGH history (not 0.25). Soft-start empty
  atlas pages; soft-add bounce. Surface cache DOES use albedo (material atlas →
  lighting/bounce); textured maps need G-buffer refine — SSIL reads live LBUFFER
  so it looks better until cards hold correct albedo radiance.
- Debug views: Surface Cache GI = screen irradiance; GI Probe Volume = packed
  2D probe grid (columns), not the scene; Surface Cache Atlas = radiance pages
  (magenta empty); Surface Cache Material = albedo pages. Gray GI/probes means
  radiance is achromatic — usually white `base_color` tint without sampling the
  color map. Seed pages with `base_color * color_map`, not tint alone.
- Card radiance must be **direct only** (sun/points/emissive × albedo). Stamping
  skySH×albedo onto every page makes dark rooms look like flat ambient/albedo
  fill. Bounce needs lit neighbors; empty pages stay black (confidence 0) so
  compose falls back to skylight SH.
- Analytical sun on cards **must** sample CSM. Unshadowed N·L lights every
  upward floor card under arches → bounce/probes fill the volume → Gather
  Intensity feels like ambient. Outside all cascades treat as occluded (0), not
  fully lit. Without a valid shadow map, skip sun entirely. Also avoid probe
  confidence floors (`+0.2` / `+0.25`) that keep dark areas opaque to SH.
- Empty/dark probes must NOT use confidence=0. That falls back to skylight SH
  (outdoor ambient in enclosed corridors). Updated probes with no lit cards are
  **known-dark**: rgb=0, high occupancy alpha so compose replaces SH with black.
  Gather Intensity must scale RGB only — never confidence (low intensity → SH
  takeover feels like ambient floor). Cap gather distance (~48–64); long form-
  factor gathers punch sunlit cards through walls. Prefer normalized average
  over unnormalized energy sums.
- Lumen-lite: form-factor alone = free-space coupling (curtain leaks). Stamp cards
  into an opacity clipmap and march short traces before sampling the atlas.
  Outside CSM cascades keep previous card radiance (unknown), do not force black.
  Priority-light by |ΔN·L| on sun rotate; soft-reset probe history. One-sided
  emit only. Inset AABB cards toward mesh interior.
- Floor→wall red: never stamp +Y floor cards into opacity (blocks all floor taps).
  Still stamp -Y ceilings to plug roof leaks. Hemisphere vis must use
  `dot(mid - sample, card_n)` — the flipped `sample - mid` rejects every +Y
  emitter. Unknown-CSM keep-prev only when N·L is clearly sun-facing; force-decay
  backfaces/overhangs or roof undersides leak bright history.
- Cyan GI tint: generic chroma form boost + energy mix + high emis_cap lets neon
  emissives win over sunlit red floors. Boost chroma only on +Y warm emitters;
  damp cool non-floor; cap emissive GI (~0.45); average-only gather. Never wipe
  shadowed wall history (that deleted floor bounce each light pass) — hard-kill
  leak history on ceilings only. Soft opacity (exp), not binary reject. No gray
  0.35 albedo fallback.
- Knife-cut cyan rectangles / bright spots in Surface Cache GI debug: **page
  recycle without clearing radiance**. `free_page`/`allocate_page` must zero
  atlas+material+emissive or old neon pages stick as hard rects. Also raise
  form-factor distance floor (1/r^2 fireflies) and gate G-buffer emissive project.
