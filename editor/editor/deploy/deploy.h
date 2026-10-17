#pragma once

#include <base/basetypes.hpp>

#include <filesystem/filesystem.h>

namespace unravel
{
struct deploy_settings
{
    fs::path deploy_location{};
    bool deploy_dependencies{true};
    bool deploy_and_run{};

    /**
     * @brief Resolve nested prefab instances into the deployed assets.
     *
     * A prefab that instances another stores a snapshot of it, taken when that prefab was
     * last saved, so it is stale as soon as the inner asset changes. The runtime normally
     * hides that by refreshing every nested instance on load - one extra asset load per
     * instance, every time one is created.
     *
     * On: the deploy resolves them once, up front, and marks the assets so the runtime can
     * skip that work. The marker is only trustworthy because the deploy rewrites it on
     * every run - it says "resolved as of this build", and this build is what produced it.
     *
     * Off: the marker is cleared instead, and the runtime refreshes on load as the editor
     * does. Slower to run, but it cannot serve stale content, and it is the safe choice if
     * a bake ever looks wrong.
     */
    /// Cook assets while deploying: resolve prefab nesting into the shipped copies and mark
    /// them, so the deployed runtime instantiates flat with no sync logic. The cooked files
    /// exist only in the deploy destination - the editor's compiled cache is never touched.
    bool deploy_cooked_assets{true};

    /// Ship the script libraries' debug companions (.pdb symbols, .xml docs). The game runs
    /// without them; with them, managed exceptions name the file and line they came from.
    bool deploy_debug_symbols{false};

    /// Write each protocol's compiled assets and asset index into one compressed pack
    /// (data/<protocol>/content.pak) instead of loose files. Off ships them loose, which is easier
    /// to inspect; the game reads either.
    bool deploy_packed_assets{true};

    /// The kept release to patch (a folder under the project's releases/). Set: the release's packs
    /// ship unchanged and content_p1.pak holds every change since it, so players who have the release
    /// download only the patch. Empty: full packs.
    std::string deploy_patch_release;

    /// Ship only the project assets the game uses: what the startup scene and deploy_scenes reference,
    /// directly or through other assets, with deploy_included added and deploy_excluded taken out.
    /// Off: every project asset ships. Engine assets always ship whole, since engine code loads
    /// them by path.
    bool deploy_only_used_assets{false};
    /// Scenes the game loads besides the startup scene, by asset key ("app:/data/levels/one.spfb").
    std::vector<std::string> deploy_scenes;
    /// Folders and assets that ship whatever the scenes use, by key: what scripts load by a path or
    /// key built at run time. The most specific rule for a path wins over its parents' rules.
    std::vector<std::string> deploy_included;
    /// Folders and assets that never ship, by key, even when the scenes use them.
    std::vector<std::string> deploy_excluded;
};


} // namespace unravel
