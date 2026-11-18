#pragma once

#include <engine/engine_export.h>

#include <hpp/string_view.hpp>
#include <hpp/uuid.hpp>

#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief Collects the asset references a load on this thread could not resolve: handles whose uid names no asset
 * the asset manager knows, which load as empty handles.
 *
 * Lives on the stack around a load. Recorders nest: the innermost one on the thread records, and the one before it
 * records again once it is gone.
 */
class ENGINE_EXPORT missing_asset_recorder
{
public:
    /// One reference that resolved to nothing.
    struct entry
    {
        hpp::uuid uid{};
        /// The asset type the field holds, as the type's name ("unravel::material").
        std::string type;
    };

    missing_asset_recorder();
    ~missing_asset_recorder();
    missing_asset_recorder(const missing_asset_recorder&) = delete;
    auto operator=(const missing_asset_recorder&) -> missing_asset_recorder& = delete;

    /// The references recorded so far, each uid once, in the order they were met.
    auto get_missing() const -> const std::vector<entry>&;

    /// Records a reference that resolved to nothing with the thread's recorder; does nothing without one.
    static void record(const hpp::uuid& uid, hpp::string_view type);

private:
    std::vector<entry> missing_;
    missing_asset_recorder* previous_{nullptr};
};

} // namespace unravel
