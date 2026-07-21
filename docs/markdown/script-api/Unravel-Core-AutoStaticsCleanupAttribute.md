<a id="autostaticscleanupattribute"></a>

# AutoStaticsCleanupAttribute

> **Extends:** `Attribute`

Marks a class whose static state must be reset when a script domain is unloaded (CoreCLR backend).

On Mono, unloading a domain destroys its statics wholesale. On CoreCLR a collectible AssemblyLoadContext only unloads once nothing references it anymore, so static caches in a surviving assembly (e.g. managers holding script instances or script Type objects) would silently keep the unloaded domain alive. The runtime processes this attribute right before any domain is unloaded:

* If the class defines `static void OnStaticsCleanup()` (any visibility), that method is invoked and nothing else happens - use it to re-create managers or clear collections selectively. 

* Otherwise every non-readonly static field is reset to its default value. Readonly statics cannot be cleared by reflection and produce a warning - define `OnStaticsCleanup` for those. 

The attribute is matched by name, so any assembly may use it without referencing the engine. On Mono it is ignored.

