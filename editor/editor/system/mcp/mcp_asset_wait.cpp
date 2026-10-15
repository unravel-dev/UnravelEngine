#include "mcp_asset_wait.h"
#include "mcp_asset_lookup.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/audio/audio_clip.h>
#include <engine/ecs/prefab.h>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/ui/ui_tree.h>
#include <graphics/texture.h>

#include <filesystem/filesystem.h>
#include <threadpp/thread.h>

#include <algorithm>

namespace unravel::mcp
{
namespace
{

/// Longest a key is polled before the others get their turn.
constexpr auto k_poll_slice = std::chrono::milliseconds(100);
constexpr auto k_poll_interval = std::chrono::milliseconds(32);

auto source_exists(const std::string& key) -> bool
{
    fs::error_code ec;
    return fs::exists(fs::absolute(fs::resolve_protocol(key)), ec);
}

/// Polls with try_get(), the handle's own readiness test: unlike is_ready() it submits a deferred
/// load, which nothing else would start while we wait. The handle is fetched again each round
/// because right after an import the key may not be registered yet. While the handle returns
/// @p previous, the asset the key held before an overwrite, the reload is still on its way.
///
/// The handle is not touched before the compiler has written the asset: a load started earlier finds no compiled file
/// and leaves the asset empty. An empty asset is not the answer either while time remains: the compiled-asset watcher
/// reloads it once the compiler writes it, so only an asset still empty at the deadline failed.
template<typename T>
auto poll_handle_state(asset_manager& am,
                       const std::string& key,
                       std::chrono::milliseconds timeout,
                       const std::shared_ptr<void>& previous) -> asset_readiness
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto compiled_path = asset_reader::resolve_compiled_path<T>(key);
    bool is_empty = false;
    while(true)
    {
        fs::error_code ec;
        if(fs::exists(compiled_path, ec))
        {
            if(auto handle = am.get_asset<T>(key))
            {
                auto asset = handle.try_get();
                is_empty = asset_handle<T>::is_empty(asset);
                if(asset && asset != previous && !is_empty)
                {
                    return asset_readiness::ready;
                }
            }
        }
        if(std::chrono::steady_clock::now() >= deadline)
        {
            if(is_empty)
            {
                return asset_readiness::failed;
            }
            return source_exists(key) ? asset_readiness::loading : asset_readiness::missing;
        }
        tpp::this_thread::sleep_for(k_poll_interval);
    }
}

template<typename... Types>
struct asset_type_list
{
};

/// The asset types with a loadable handle, in the order extensions are matched.
using loadable_asset_types = asset_type_list<material,
                                             mesh,
                                             prefab,
                                             scene_prefab,
                                             gfx::texture,
                                             audio_clip,
                                             animation_clip,
                                             physics_material,
                                             ui_tree>;

/// Calls @p visit with the asset type that owns extension @p ext; false when no type does.
template<typename Visitor, typename T, typename... Rest>
auto visit_asset_type(const std::string& ext, asset_type_list<T, Rest...>, Visitor&& visit) -> bool
{
    if(matches_asset_extension<T>(ext))
    {
        visit.template operator()<T>();
        return true;
    }
    if constexpr(sizeof...(Rest) > 0)
    {
        return visit_asset_type(ext, asset_type_list<Rest...>{}, std::forward<Visitor>(visit));
    }
    return false;
}

auto find_snapshot_asset(const asset_snapshot& snapshot, const std::string& key) -> std::shared_ptr<void>
{
    auto it = snapshot.find(key);
    return it != snapshot.end() ? it->second : nullptr;
}

/// Waits until @p key is loaded; with a @p previous asset (see asset_snapshot), until it is
/// loaded as something else.
auto wait_asset_state(rtti::context& ctx,
                      const std::string& key,
                      std::chrono::milliseconds timeout,
                      const std::shared_ptr<void>& previous) -> asset_readiness
{
    auto& am = ctx.get_cached<asset_manager>();
    const auto ext = fs::path(key).extension().generic_string();
    asset_readiness state{};
    if(visit_asset_type(ext,
                        loadable_asset_types{},
                        [&]<typename T>()
                        {
                            state = poll_handle_state<T>(am, key, timeout, previous);
                        }))
    {
        return state;
    }
    // Types with no loadable handle here: registered metadata, or the file itself, is the signal.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while(true)
    {
        auto meta = am.get_metadata_for_key(key);
        if(!meta.location.empty() || !meta.meta.type.empty())
        {
            return asset_readiness::ready;
        }
        if(std::chrono::steady_clock::now() >= deadline)
        {
            return source_exists(key) ? asset_readiness::loading : asset_readiness::missing;
        }
        tpp::this_thread::sleep_for(k_poll_interval);
    }
}

} // namespace

auto readiness_to_string(asset_readiness state) -> const char*
{
    switch(state)
    {
        case asset_readiness::ready:
            return "ready";
        case asset_readiness::failed:
            return "failed";
        case asset_readiness::missing:
            return "missing";
        case asset_readiness::loading:
            break;
    }
    return "loading";
}

auto take_asset_snapshot(rtti::context& ctx, const std::vector<std::string>& keys) -> asset_snapshot
{
    tpp::this_thread::register_this_thread();
    auto& am = ctx.get_cached<asset_manager>();
    asset_snapshot snapshot;
    for(const auto& key : keys)
    {
        const auto ext = fs::path(key).extension().generic_string();
        visit_asset_type(ext,
                         loadable_asset_types{},
                         [&]<typename T>()
                         {
                             auto handle = am.find_asset<T>(key);
                             if(!handle)
                             {
                                 return;
                             }
                             if(auto asset = handle.get(); asset && !asset_handle<T>::is_empty(asset))
                             {
                                 snapshot.emplace(key, std::move(asset));
                             }
                         });
    }
    return snapshot;
}

auto wait_assets_ready(rtti::context& ctx,
                       const std::vector<std::string>& keys,
                       std::chrono::milliseconds timeout,
                       const asset_snapshot& previous) -> std::vector<asset_readiness>
{
    tpp::this_thread::register_this_thread();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<asset_readiness> states(keys.size(), asset_readiness::loading);
    const auto is_pending = [](asset_readiness state)
    {
        // Waiting longer cannot change a failed load or a file that is not there.
        return state == asset_readiness::loading;
    };
    do
    {
        for(size_t i = 0; i < keys.size(); ++i)
        {
            if(!is_pending(states[i]))
            {
                continue;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            const auto slice = std::clamp(remaining, std::chrono::milliseconds(0), k_poll_slice);
            states[i] = wait_asset_state(ctx, keys[i], slice, find_snapshot_asset(previous, keys[i]));
        }
    } while(std::chrono::steady_clock::now() < deadline && std::any_of(states.begin(), states.end(), is_pending));
    return states;
}

auto wait_for_assets(rtti::context& ctx,
                     const std::vector<std::string>& keys,
                     const std::vector<bool>& is_waited,
                     std::chrono::milliseconds timeout,
                     const asset_snapshot& previous) -> asset_wait_report
{
    asset_wait_report report;
    report.state_fields.resize(keys.size());
    report.waited = timeout.count() > 0;
    if(!report.waited)
    {
        return report;
    }
    std::vector<std::string> waited_keys;
    for(size_t i = 0; i < keys.size(); ++i)
    {
        if(is_waited[i])
        {
            waited_keys.push_back(keys[i]);
        }
    }
    const auto states = wait_assets_ready(ctx, waited_keys, timeout, previous);
    size_t state_index = 0;
    for(size_t i = 0; i < keys.size(); ++i)
    {
        if(!is_waited[i])
        {
            continue;
        }
        const auto state = states[state_index++];
        report.ready_count += state == asset_readiness::ready ? 1 : 0;
        report.state_fields[i] = fmt::format(R"(,"state":{})", make_json_string(readiness_to_string(state)));
    }
    return report;
}

} // namespace unravel::mcp
