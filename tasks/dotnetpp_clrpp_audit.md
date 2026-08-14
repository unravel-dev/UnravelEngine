# dotnetpp / clrpp / monopp audit (2026-08-13)

> **Status update (same day): fixes applied.** Everything in §8's safety list
> except the monopp architectural items is fixed in the working tree, plus the
> engine per-frame invoker caching. Verified: dotnetpp suite 428/428 (coreclr,
> JIT and `DOTNET_InterpMode=1` on the .NET 10 runtime), 425/425 (mono,
> vendored 6.12), perf harness within noise of the pre-fix baseline on every
> row, and the perf domain now passes its unload leak check (it leaked a
> pinned `Perf.Vec2[]` before — live proof of the S2 bug). Additional findings
> made during the fix pass:
> - `get_args_signature` had a second, masking bug on both backends: the
>   lambda took the `for_each_tuple_type` index constant's `::type` (the
>   constant itself), so no argument type was ever "known" and the
>   by-signature lookup path was dead code. Fixed together with the OR/AND
>   accumulation; `make_method_invoker(type, name)` now tries the exact
>   signature and falls back to name+arity (enums declared as integers on the
>   C++ side have no signature-name mapping).
> - `ConcurrentDictionary.IsEmpty` acquires every bucket lock when the
>   dictionary IS empty - using it as a FreeHandle fast-path guard cost +630ns
>   per released object (found via A/B against the perf baseline; §5's
>   "guard with IsEmpty" suggestion is retracted).
> - monopp additionally got: mono_string ToString exception checks (was
>   `exc=nullptr` = process abort), field invoker size guards + managed-
>   representation reads (converted POD fields read straight into the smaller
>   native type = stack overflow), meta-cache mutexes, static regex.
> - Still open (monopp, architectural): raw `MonoObject*` heap storage
>   without GC handles, hand-forged `_MonoGenericInst`, `mono_array`
>   raw-bytes fallback + char stride, `mono_raise_exception` longjmp,
>   `mono_object` ctor `mono_runtime_object_init` unchecked, global
>   `current_domain`. The mono backend remains dormant; treat as
>   do-not-enable-without-fixing.

In-depth review of the scripting-host libraries under `deps/dotnetpp/dotnetpp/`:
safety, performance, API-neutral optimization opportunities, and the JIT vs
interpreter story. Line references are against the current working tree.

Scope note: `deps/dotnetpp` is first-party (not `deps/3rdparty`), so fixes here
are actionable.

---

## 1. Architecture recap

| Layer | Role |
|---|---|
| `monopp/` (~6.5k loc) | Mono embedding wrapper (legacy API: `mono_jit_init_version`, appdomains, `mono_runtime_invoke`). Dormant: the repo forces the coreclr backend. |
| `clrpp/clrpp/` (~6.9k loc) | CoreCLR host via hostfxr. Bootstraps a ~75-entry positional table of `UnmanagedCallersOnly` function pointers from `Clrpp.Managed.dll` (`clr_bridge.cpp:466-489`). Objects cross as GCHandles (`managed_ptr` = `shared_ptr<void>` RAII), values as blittable `clr_variant` blobs. |
| `clrpp/managed/` (~6.1k loc C#) | The bridge assembly: reflection exports, three-tier invoke fast path, collectible-ALC "domains", unload verification + ClrMD leak analysis, Mono.Cecil icall weaver. |
| `dotnetpp/dotnetpp/` (~0.9k loc) | Pure `using`-alias facade (`namespace dotnet`) over `mono::` / `clr::`, selected by `DOTNETPP_BACKEND` (forced to `coreclr` in `deps/dotnetpp/CMakeLists.txt:3`). |

Engine integration: only `engine` links `dotnetpp` (`engine/engine/CMakeLists.txt:53`);
all use goes through `dotnet::` (scripting system, script components, 28 binding
TUs, asset compiler, profiler). Frame updates are one managed call each
(`SystemManager.internal_n2m_update(UpdateInfo)`), per-entity fan-out happens
managed-side with override-flag caching. Internal calls are name-registered
natively and woven into `calli` bodies at compile time (no runtime codegen).

Overall verdict: **clrpp + managed bridge are well-engineered** (collectible-ALC
discipline, ephemeron caches, interpreter/AOT fallbacks are deliberate).
**monopp is significantly less safe** and carries several crash-grade latent
bugs; fine while dormant, but do not re-enable the mono backend without fixing
§3. A handful of shared copy-paste bugs affect both.

---

## 2. Shared (copy-paste) bugs — both backends

### 2.1 `get_args_signature` OR-accumulates `all_types_known`
`clr_type_traits.h:226-266`, `mono_type_traits.h:230-270`. `get_name(bool& found)`
does `found |= true/false`, so `all_types_known` is true if *any* arg type is
known, not all. A signature mixing a known primitive with a custom struct
builds a wrong signature string (`"Foo(int,unknown)"` or drops leading unknown
args) and the by-signature lookup **throws** instead of falling back to the
name+arity lookup in `make_method_invoker` (`clr_method_invoker.h:253-271`).
Fix: per-arg `bool this_found = false;` and AND-accumulate.

### 2.2 Return-type "ignore" exemption is inverted
`clr_method_invoker.h:43-51`, `mono_method_invoker.h:43-51`. Comment says
"allow cpp return type to be void i.e ignoring it", but the code exempts the
case where the **managed method** returns void:
- C++ `void`, method non-void (the intended ignore case) → **rejected**.
- C++ `int()`, method `void` → **accepted**; monopp then does
  `mono_object_unbox(nullptr)` → crash. clrpp silently returns `T{}`
  (`invoke_result::extract` handles `kind_empty`).
Fix: `if (!std::is_void<return_type>::value) return false;` in the exemption.

### 2.3 `check_type_layout` alignment test direction
`clr_type_conversion_base.h:22` / monopp equivalent accept
`managed_align <= cpp_align`. For monopp, which reinterpret-casts the unbox
pointer directly, a lower-aligned managed payload read as a higher-aligned C++
type is a misaligned-read UB (matters on ARM). On clrpp it is harmless (managed
side copies with `WriteUnaligned`), but the check documents the wrong intent.
Also: both `check_type_layout` calls live in `assert(...)` only — release
builds skip layout validation entirely.

### 2.4 `is_*_valuetype = std::is_standard_layout`
`clr_type_traits.h:25-26`, `mono_type_traits.h:24-25`. Standard layout does not
imply trivially copyable, and types containing native pointers pass. Tighten to
`std::is_standard_layout && std::is_trivially_copyable` (the array pin path
already requires trivially-copyable — `clr_array.h:121-127`).

### 2.5 Unsynchronized global meta caches
Type/method/property/field meta caches are unguarded static `unordered_map`s
(`clr_type.cpp:34-39`, `clr_method.cpp:15-19`, `clr_property.cpp:15-19`,
`clr_field.cpp` same; monopp identical), while the icall registry *is*
mutex-guarded (`clr_internal_call.cpp:45-57`). Wrapper construction from two
threads is a data race. The engine does call the bridge off-main-thread
(weaver from compile jobs — `asset_compiler.cpp:2103`; deploy queries), so this
is not purely theoretical. Cheap fix: a `std::shared_mutex` per cache (or one
global) inside `get_or_create_meta`; no API change.

Also shared: `extract_relevant_stack_frame` compiles a `std::regex` per call
(`clr_exception.cpp:79`, `mono_exception.cpp:87`) — make it `static`.
`domain::get_version()` returns the raw handle/pointer value; slot reuse makes
it a weak identity token (and monopp never validates its copy at all).

---

## 3. monopp-specific findings (crash-grade first)

The mono backend is dormant (`DOTNETPP_BACKEND` forced to coreclr), so these are
latent — but they gate any future re-enable (e.g. mono-AOT console targets).

1. **Hand-forged Mono internals for `List<T>` inflation** — `mono_list.h:367-403`
   declares private `_MonoGenericInst` (with bitfields) / `_MonoGenericContext`
   mimicking runtime-internal structs, fills a stack local and casts it into
   `mono_class_inflate_generic_type`. Implementation-defined bitfield layout +
   version-fragile + the runtime canonicalizes/retains generic-inst pointers, so
   a stack pointer can be captured inside runtime metadata. Replace with
   reflection `MakeGenericType` or `mono_class_bind_generic_parameters`.
2. **Raw `MonoObject*` held in heap storage without GC handles** —
   `mono_object.h:72` and every `std::vector<mono_object>` return
   (`mono_type.cpp:630-666`, `mono_array.h:351-365`, field/property attribute
   getters). SGen scans only the native stack conservatively; heap-held object
   pointers are invisible → collected/moved under the wrapper. The pinning
   helpers (`mono_gc_handle.h`) exist but nothing uses them internally.
3. **`mono_array<T>` raw-bytes fallback is dead-on-arrival** — `create_array`
   sets `use_raw_bytes_ = true` while running as the *base-class ctor argument*
   (`mono_array.h:82,90`), then the member's default initializer resets it to
   `false` (`mono_array.h:231`). The fallback allocates `byte[count*sizeof(T)]`
   but element access then uses `T` strides on it. Also `char` maps to
   `System.Char` (2 bytes) with 1-byte strides (`mono_array.h:220-223`), and the
   primitive-class if/else chain is broken (independent `if`s) — duplicated in
   `mono_list.h:237-297`.
4. **Field invoker copies with no size check** — `mono_field_invoker.h:50-67,
   145-171`: `mono_field_get_value` writes `mono_class_value_size` bytes into a
   stack `T` — managed struct bigger than `T` = stack buffer overflow.
5. **`exc=nullptr` invokes abort the process on managed exceptions** —
   `mono_runtime_object_init` (`mono_object.cpp:52`), `mono_object_to_string`
   in `as_utf8/16/32` (`mono_string.cpp:33,59,71`), enum-values invokes
   (`mono_type.cpp:432,448`). A throwing C# constructor kills the process.
6. **`mono_raise_exception` longjmps across C++ frames** (`mono_exception.cpp:68-77`)
   — destructors on the icall stack are skipped; undocumented.
7. Thread safety: global non-atomic `current_domain` (`mono_domain.cpp:45`),
   single `mono_thread_attach` for the creating thread only, logger `std::map`
   mutated on read (`mono_logger.cpp:7-17`).
8. Perf: `mono_runtime_invoke` exclusively (no unmanaged thunks — the
   `mono_thunk_exception` name is a vestige); `mono_list` scans all methods by
   name per `size()/clear()` call and builds fresh invokers per element
   (`mono_list.h:71-96,186-204`); `to_vector` calls `size()` per iteration;
   `mono_list<T>::set` does a full `to_vector()` and discards it
   (`mono_list.h:169-170`); element-wise array copies where one
   `mono_array_addr`+memcpy would do; `mono_string::as_utf8` routes through
   managed `Object.ToString()` even for actual strings; unchecked UTF-16→UTF-8
   transcode emits invalid UTF-8 for unpaired surrogates (`mono_string.cpp:48-50`).
9. Misc: `FamORAssem` visibility asserts (`mono_method.cpp:184-203`); invalid
   instance silently degrades to static invoke (`mono_method_invoker.h:90-98`);
   non-shared assembly load leaves `mono_domain_set` switched
   (`mono_assembly.cpp:173`).

---

## 4. clrpp native findings

Safety:
- **Positional export-table contract** is validated only by count
  (`clr_bridge.cpp:476-487`); a same-count reorder between native struct and
  `Bridge.Bootstrap.cs` is silent ABI corruption. Cheap hardening: have
  `Bootstrap` also return a protocol hash (e.g. FNV of export names) checked at
  init. Internal protocol, no public API change.
- `clr_scoped_gc_handle::get_handle()` truncates the 64-bit GCHandle IntPtr to
  `uint32_t` (`clr_gc_handle.h:64-67`) — collision-prone if used as identity.
- Meta caches unguarded (see §2.5); `clr_domain::get_assembly` mutates a
  mutable map under `const` with no lock (`clr_domain.cpp:117-126`).
- Error paths in `bridge_detail::initialize` leak the hostfxr context on
  mid-init failure (`clr_bridge.cpp:437-489`) — benign one-shot, worth a
  `hostfxr_close` on the failure exits.
- The `assert`-only layout validation of §2.3 applies to every POD converter
  (`clr_managed.h:56`).

Perf (API-neutral):
- `take_string` = managed CoTaskMem alloc → native copy → **extra bridge
  crossing to free** (`clr_bridge.cpp:188-201`). Meta generation does 3-4 per
  type/method; acceptable because meta is cached, but a caller-buffer variant
  of the name getters would halve boundary chatter during type scans.
- `clr_object(managed_ptr)` eagerly does `object_get_type` (one bridge call +
  meta creation) per wrapped object (`clr_object.cpp:10-17`).
  `clr_array<clr_object>::get` pays it per element. Making `type_` lazy
  (fetch on first `get_type()`) halves bridge calls for object-returning
  invokes without any signature change.
- String arguments to invokers go `string_create` (bridge call + GCHandle) +
  later `free_handle` (`clr_type_conversion.h:73-97`) — three crossings per
  string arg. The variant protocol already has `kind_string_utf8`
  (`clr_bridge.h:35`) and managed `VariantToObject` accepts it; routing
  `std::string` args through a UTF-8 variant instead of a handle removes all
  three crossings. Internal converter change only.
- `clr_list` builds its invoker set per wrapper instance
  (`clr_list.h:112-118`); each construction = 3-8 by-name method lookups
  through the bridge. Cache invoker sets per element-type handle in a static
  (guarded) map.
- `has_compatible_signature` compares by `get_fullname()` string
  (`clr_method_invoker.h:19-27`), allocating copies per check. Interned type
  handles make handle-equality comparison possible: cache the corlib primitive
  type handles once, compare handles. Fully internal; also fixes the cost of
  the engine's per-frame checks (§6.1) at the root.
- Meta getters (`get_name/get_fullname/get_namespace`) return `std::string` by
  value per call (`clr_type.cpp:206-219`). Returning `const&` into the shared
  meta is source-compatible for callers but changes the declared signature —
  borderline vs the "no public API change" constraint; the handle-comparison
  fix above removes most of the motivation.
- `fetch_handles` two-call sizing (`clr_bridge_utils.h:12-22`) re-runs the full
  reflection query managed-side both times — see §5 for the managed-side memo
  fix; the native pattern itself is fine.
- Good already: `variant_pack` is zero-heap for blittable args
  (`clr_method_invoker.h:121-142`); array pin-once cache with `memcpy` element
  access (`clr_array.h:316-344`); bool/char icall ABI widening is a correct and
  well-documented fix (`clr_internal_call.h:120-160`).

---

## 5. Managed bridge (Clrpp.Managed) findings

Ranked by risk (verified S1/S2 mechanics by direct read):

- **S1 — `GetOrAdd` factory races leak permanent GCHandles.**
  `Bridge.Core.cs:84-90` (`Intern`) and `Bridge.FastPath.cs:190-199`
  (long-lived pins) allocate GCHandles inside `ConcurrentDictionary.GetOrAdd`
  factories. Factories can run concurrently; the losing handle is unreachable
  from the forward map, is never purged by `PurgeInternedHandles`, and
  `FreeHandle` refuses it — a permanent strong root that can pin a dying ALC
  forever (defeats the flagship unload guarantee). Fix: allocate outside +
  `TryAdd`/free-on-lose.
- **S2 — thread-local array-pin cache aliases freed handles.**
  `Bridge.FastPath.cs:41-102`: TLS keeps a `GCHandle` *copy* after the call; a
  release on another thread frees the real pin, the stale TLS copy still passes
  `IsAllocated`, and a recycled handle slot means `AddrOfPinnedObject` +
  `Buffer.MemoryCopy` read/write **another object's memory**. Also
  `ReleaseAllLongLived` only clears the unloading thread's TLS — worker-thread
  pins survive unload and pin arrays forever. Fix shape: TLS stores only the
  IntPtr key and re-resolves through `LongLivedPins`; never cache `GCHandle`
  structs across ownership boundaries.
- **S3 — blittable fast path skips blob size validation** the slow path
  enforces (`Bridge.BlittableInvoke.cs:237-255` vs `Bridge.Core.cs:192-197`):
  undersized native blob = OOB read. One comparison per arg to fix.
- **S4 — portable field path uses `Marshal.OffsetOf` (interop layout) against
  a CLR-layout pinned box** (`Bridge.FieldAccess.Portable.cs:146`); structs
  with `bool`/`char` fields can mis-offset. Gate harder or compute managed
  offsets.
- **S5 — a few exports can still throw out of `UnmanagedCallersOnly`**
  (process fail-fast): `TypeGetEnumValues` `Convert.ToInt64` overflow on
  ulong enums (`Bridge.Reflection.cs:186`), `PropertyGetFlags` instantiates
  `DefaultValueAttribute` (user code, `Bridge.Reflection.cs:932`),
  `GetParameters()/GetTypes()` type-load exceptions on partial deploys.
- **S6 — `SharedAssemblies` re-registration hole**: first-loaded-wins +
  removal-on-unload means hot reload can serve the old assembly to other
  contexts, then a *third* private copy after unload — type-identity split
  (`Bridge.Runtime.cs:22-107`). Re-register survivors on removal.
- S7/S8 (lower): statics cleanup sweeps *all* contexts on any unload
  (`Bridge.Unload.cs:110`); `DomainUnload` frees the domain handle while native
  still holds the IntPtr (`Bridge.Unload.cs:99`).

Perf (native API unchanged):
- Sizing calls re-run reflection queries twice, and attribute enumeration
  *instantiates attributes on both passes* (`Bridge.Reflection.cs:664-674`,
  `Bridge.Runtime.cs:372-409`). A one-entry (handle,query)→array memo halves
  every enumeration.
- `FindType` negative lookups scan all types per miss; add a per-assembly name
  cache (`Bridge.Runtime.cs:315-335`).
- `TypeGetMethodBySignature` rebuilds mono-style names per candidate parameter
  (`Bridge.Reflection.cs:446-482`) — hot during script bind; memoize per Type.
- `Log` allocates two CoTaskMem blocks even for dropped trace messages
  (`Bridge.Core.cs:373-391`) — add a level gate.
- Widen the portable (interpreter-safe) blittable tier beyond public-static
  arity≤8 (`Bridge.BlittableInvoke.cs:98-100`) — matters under interpreter/no-JIT.
- Already excellent: three-tier invoke plan cache (`ConditionalWeakTable`),
  DynamicMethod thunks gated on `RuntimeFeature.IsDynamicCodeCompiled`,
  pooled exact-arity `object[]`s with reentrancy guards, compile-time weaving.

---

## 6. Engine integration findings

1. **Per-frame invoker construction with signature checks** —
   `script_system.cpp:787,837,870` call
   `dotnet::make_method_invoker<...>(cache_.update_method)` with the default
   `check_signature=true` every frame: `get_return_type()` is an uncached
   bridge round-trip plus fullname string fetches, ×3 sites (fixed update can
   run multiple times per frame). `script_component.cpp` already passes
   `false` everywhere. Either pass `false` here, or better: cache the
   constructed `clr_method_invoker` in `engine_script_cache` (it is copyable
   and just wraps the method handle).
2. `process_pending_deletions()` runs per script component per frame/tick
   (`script_system.cpp:747-756,818-822`) — native-only cost, fine, but it is
   the only per-entity loop in the hot path.
3. Threading: weaver + version queries run on worker threads while the meta
   caches are unguarded (§2.5) — the mutex fix covers this.
4. Deploy bundles a pruned .NET **9.0** root (`editor_actions.cpp:1578-1662`);
   dev machines roll forward to latest major (`rollForward: LatestMajor`), so
   dev and deploy can run different runtime majors. Consider pinning dev to the
   deploy version when validating.

---

## 7. JIT vs interpreted paths

### Current state
- Backend is CoreCLR through hostfxr; **tiered JIT with TieredPGO (runtime
  defaults)** — no `configProperties` exist anywhere (generated
  `Clrpp.Managed.runtimeconfig.json` and the native fallback writer
  `clr_bridge.cpp:152-165` both set only tfm/rollForward/framework). Bundled
  target: net9.0 (`CLRPP_DOTNET_VERSION`, `clrpp/CMakeLists.txt:6`).
- **Interpreter is already plumbed, off by default.**
  - clrpp: `interpreter_config::mode::forced` sets `DOTNET_InterpMode=1` before
    hostfxr loads, never overriding a pre-existing env value
    (`clr_jit.cpp:315-335`); runtimes without the interpreter ignore it and JIT.
    This is the .NET 10 CoreCLR interpreter switch (preview; name/semantics may
    still evolve while it stabilizes).
  - Engine: `--interpreter forced` command-line option or the
    `UNRAVEL_FORCE_DOTNET_INTERPRETER` compile define
    (`script_system.cpp:215-243`) — note the define is not set anywhere in the
    build system, so the CLI flag is the only live path.
  - monopp: forced mode passes `--interpreter` to `mono_jit_parse_options`
    before `mono_jit_init_version` (`mono_jit.cpp:252-259`) — works only if the
    linked Mono build compiled mint in; not verified at runtime.
- The bridge is **interpreter-clean by design**: every `Reflection.Emit` /
  `DynamicMethod` use is gated on `RuntimeFeature.IsDynamicCodeCompiled` with
  feature-complete portable fallbacks ("Emit never owns a feature",
  `Bridge.FieldAccess.cs:9-22`), and the icall weaver runs at compile time
  producing static `calli` IL (`clr_internal_call.h:57-71`, `Weaver.cs:60-63`).

### Options for no-JIT platforms (iOS, consoles)
1. **.NET 10+ CoreCLR interpreter** (the path the code anticipates): keep the
   whole clrpp architecture unchanged; `DOTNET_InterpMode` (or the runtime
   self-enabling on no-JIT packs). Costs today: interpreter is preview-grade in
   .NET 10; expect ~10-30x slower managed execution than tier-1 JIT for compute
   — fine for gameplay glue, painful for script-heavy math. Mitigations that
   are already in place: blittable no-boxing invoke tiers, pinned bulk array
   copies, one-managed-call-per-frame fan-out design. Mitigation to add: widen
   the portable blittable tier (§5) since DynamicMethod thunks vanish there.
2. **Mono full AOT (+ interpreter fallback)** via the monopp backend — viable
   but §3 must be fixed first, and monopp has no AOT wiring today
   (`mono_jit_set_aot_mode` absent; only a commented-out AOT command in
   `asset_compiler.cpp:2092`).
3. **NativeAOT — not compatible** with this design: the bridge depends on
   `AssemblyLoadContext.LoadFromStream` of freshly compiled assemblies,
   runtime reflection over arbitrary script types, and `MakeGenericType`
   instantiations over unseen structs (`ClrLayout.cs:22-28` documents this).
   An AOT story would precompile scripts into the image, keep the (already
   AOT-clean) woven icalls, and replace the dynamic bridge tiers — a different
   product, not a config switch.

### Recommendations
- Add a CI/dev smoke that runs the existing `tests/dotnetpp_perf.cpp` harness
  with `DOTNET_InterpMode=1` on a .NET 10 nightly — it exercises exactly the
  n2m/m2n/array paths that regress under the interpreter, and validates the
  portable tiers actually engage (no silent Emit dependence).
- Consider explicit `configProperties` in the bridge csproj for deploys:
  `TieredCompilation`/`TieredPGO` left on (right default), but pin
  `System.Runtime.TieredCompilation.BackgroundWorkerTimeoutMs` only if startup
  jitter shows up; more usefully, pin the deploy runtime version instead of
  rollForward LatestMajor for reproducibility (dev already differs from deploy).
- If script startup latency matters (first-call JIT of woven thunks +
  script methods), the cheap lever is invoking hot lifecycle methods once
  during load (warm-up), not R2R — crossgen of user script DLLs would
  complicate the compile pipeline for little gain at these sizes.

---

## 8. Priority list

Safety (do first):
1. S1 GetOrAdd handle leaks (`Bridge.Core.cs:84`, `Bridge.FastPath.cs:190`) — breaks unload.
2. S2 TLS pin aliasing (`Bridge.FastPath.cs:41-102`) — memory-corruption class.
3. §2.2 inverted return-type exemption (both invokers) + §2.1 `found |=` signature bug.
4. §2.5 mutex the native meta caches (weaver runs off-thread today).
5. S3 fast-path blob size check; S5 the two concrete throwers (one-liners).
6. S6 SharedAssemblies re-registration on unload.
7. monopp §3.1-3.7 — only if/when the mono backend is revived; otherwise mark
   the backend as unsupported.

Performance (API-neutral, rough impact order):
1. Engine: stop re-validating signatures per frame (`script_system.cpp:787,837,870`)
   — cache invokers in `engine_script_cache`.
2. Managed: memoize sizing/fill double-enumeration + stop double-instantiating
   attributes; per-assembly type-name cache.
3. Native: lazy `clr_object` type fetch; `kind_string_utf8` for string args;
   per-type `clr_list` invoker cache.
4. Native: handle-based `is_compatible_type` (kills fullname string churn).
5. Managed: widen portable blittable tier (pays off under interpreter).
6. Small: static regex in `extract_relevant_stack_frame`; `Log` level gate;
   `FreeHandle` pin-probe guard.
