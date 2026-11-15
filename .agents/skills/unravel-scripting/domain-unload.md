# Domain unload, leaks and the bridge files

Script libraries load into collectible `AssemblyLoadContext`s. A context goes away only when nothing
references it, so one static field in a surviving context (an engine manager, a cache keyed by script
`Type`s, an event subscription) keeps the whole app context and its types alive after every reload.

## What an unload does

`DomainUnload` in `deps/dotnetpp/dotnetpp/clrpp/managed/Bridge.Unload.cs`, in order:

1. `[AutoStaticsCleanup]` over every clrpp context. The attribute is matched by name
   (`AutoStaticsCleanupAttribute`), so any assembly can declare its own; the engine's is
   `engine_data/data/scripts/system/AutoStaticsCleanup.cs`. A marked type with
   `static void OnStaticsCleanup()` has it called (re-create managers such as `SystemManager` and
   `UIEventManager` there); otherwise its non-readonly static fields are reset, and a readonly one logs
   "cannot clear readonly field".
2. `ClearFrameworkCaches`: every framework assembly's `MetadataUpdateHandler` gets `ClearCache(null)`,
   because caches built over reflection (System.Text.Json's) keep every type passed to them. CoreLib's
   handler is skipped: it walks every loaded type, and on .NET 10 it makes reflection check each cached
   member for a deletion attribute for the rest of the process.
3. Interned reflection handles owned by the dying context are purged. Ownership checks a member's
   `DeclaringType` and its `ReflectedType`: a property declared on an engine base class but reflected
   through an app type pins the app context.
4. Unload, then verify through a `WeakReference` after GC rounds. Returns 0 (clean), 1 (leaked) or
   -1 (error).
5. On a leak: "domain '<name>' LEAKED", then one "static root: <Type>.<field> ..." line per static
   field of the live contexts that reaches the dead one (values, delegate targets, collection contents
   and `Dictionary<Type, ...>` keys). With the ClrMD libraries beside the bridge,
   `Bridge.LeakAnalysis.cs` adds GC handles that still target leaked objects (handles native code did
   not free), a census of surviving instances per type, and full root paths. Nothing of this runs
   without a leak.

## Rules

- A class whose statics hold script instances, script `Type`s or delegates is marked
  `[AutoStaticsCleanup]`.
- Before adding a cleanup for a leak, read the roots the report names: they say which field or handle
  holds the context. A guard written from a guessed mechanism changes nothing when the guess is wrong.
- A native handle to a managed object (`dotnet::make_object_pinned`, GC handles in components) is freed
  before its domain unloads: the editor unloads every scene's scripting before the domains for this
  reason (`editing_manager::on_script_recompile`).

## Bridge files

The bridge payload is `Clrpp.Managed.dll` with `Clrpp.Managed.runtimeconfig.json` and optional NuGet
libraries: `Mono.Cecil*` (icall weaving, used by the script build) and `Microsoft.Diagnostics.*` /
`Microsoft.Extensions.*` (leak analysis). CMake deploys it into the `clrpp/` folder beside the
executables (`CLRPP_MANAGED_DIR`). `locate_bridge` (`deps/dotnetpp/dotnetpp/clrpp/clrpp/clr_bridge.cpp`)
probes the explicit assembly dir and its `clrpp/` child, then `<exe dir>/clrpp`, `<exe dir>`,
`<cwd>/clrpp`, `<cwd>`. The bridge resolves its dependencies from its own folder, so the folder ships
as is. A shipped game may leave out Cecil (its libraries are woven at build) and ClrMD (leak reports
lose the GC analysis).

The native side and `Clrpp.Managed.dll` check each other's export table at start; a new export needs
both rebuilt and deployed together, or init fails with "bridge export count mismatch".

## Testing dotnetpp

- The suite is the target `monopp_test` (`deps/dotnetpp/dotnetpp/tests/`), with managed fixtures in
  `deps/dotnetpp/dotnetpp/tests/managed/`. The engine tree builds without it.
- Configure dotnetpp standalone with `-DBUILD_DOTNETPP_TESTS=ON` and `-DDOTNETPP_BACKEND=coreclr` (and a
  second tree with `mono` when the change touches shared code), then build `copy_resources` together
  with `monopp_test`: only `copy_resources` copies the fixtures, and a stale copy fails new tests.
- An unload change adds a fixture domain that must unload cleanly, checked through the bridge's
  "unloaded cleanly" / "LEAKED" log lines, as the test "domain using System.Text.Json unloads" does
  with `json_unload.cs` (`deps/dotnetpp/dotnetpp/tests/dotnetpp_suite.cpp`).
