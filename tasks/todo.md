# Surface Cache GI (plan implementation)

- [x] Prototype: static AABB planar cards + atlas project/sample into `pbr_indirect`
- [x] Hybrid compose: surface_cache base + near-field SSIL + SH miss fallback (`u_gi_compose`)
- [x] Page pool + dirty/LRU + amortized world-space age (retain beyond spawn radius)
- [x] Production scope deferred: skinned/dynamic, card-to-card multi-bounce, HW-RT

## Enable

On a volume (or camera): enable **Surface Cache GI**. Default volumes ship with it off.
Debug viz: Scene/Game panel → Surface Cache GI.
