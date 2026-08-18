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
};


} // namespace unravel
