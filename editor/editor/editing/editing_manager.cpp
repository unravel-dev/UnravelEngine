#include "editing_manager.h"
#include "base/basetypes.hpp"
#include "engine/profiler/profiler.h"
#include "imgui/imgui.h"
#include "logging/logging.h"
#include "simulation/simulation.h"
#include <chrono>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/scripting/ecs/components/script_component.h>

#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/settings/settings.h>
#include <engine/defaults/defaults.h>
#include <engine/meta/ecs/entity.hpp>

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_writer.h>
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/project_manager.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <imgui_widgets/gizmo.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/imgui/integration/imgui_messagebox.h>

#include <filedialog/filedialog.h>

namespace unravel
{

namespace
{
    /// Reload script domains after scenes have been unloaded via unload_scenes_scripting.
    /// App domain always reloads; engine domain follows editor scripting settings.
    void reload_script_domains(rtti::context& ctx, script_system& scripting, bool recompile)
    {
        const bool reload_engine =
            ctx.has<project_manager>() &&
            ctx.get_cached<project_manager>().get_editor_settings().scripting.reload_engine_domain;

        scripting.unload_app_domain();
        if(reload_engine)
        {
            scripting.unload_engine_domain();
            scripting.load_engine_domain(ctx, recompile);
        }
        scripting.load_app_domain(ctx, recompile);
    }

    struct merge_session
    {
        uint64_t epoch = 1;       // increments on boundaries (press/release/focus loss)
        bool     down_prev = false;

        auto is_active() const -> bool
        {
            return ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive();
        }
    
        void tick()
        {
            const bool down = is_active();
    

            if(!ImGui::IsAnyItemActive())
            {
                // Bump the epoch on any boundary so new actions won't merge with the previous batch.
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || 
                    ImGui::GetIO().AppFocusLost)
                {
                    ++epoch;
                }

                if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    ++epoch;

                }
            }
           
    
            down_prev = down;
        }
    
        // Current merge key to stamp onto actions created this frame.
        // 0 means "not mergeable".
        auto current_merge_key() const -> uint64_t
        {
            return epoch;
        }
    };

    static merge_session session;
}

auto editing_manager::init(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();

    ev.on_play_before_begin.connect(sentinel_, 1000, this, &editing_manager::on_play_before_begin);
    ev.on_play_begin.connect(sentinel_, 1000, this, &editing_manager::on_play_begin);
    ev.on_play_after_end.connect(sentinel_, -1000, this, &editing_manager::on_play_after_end);
    ev.on_frame_update.connect(sentinel_, frame_update_priority::editing, this, &editing_manager::on_frame_update);
    ev.on_script_recompile.connect(sentinel_, 1000, this, &editing_manager::on_script_recompile);

    return true;
}

auto editing_manager::deinit(rtti::context& ctx) -> bool
{
    unselect();
    unfocus();
    return true;
}

void editing_manager::on_play_before_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    waiting_for_compilation_before_play_ = true;

    push_undo_stack_enabled(false);  
    auto& scripting = ctx.get_cached<script_system>();

    {
        scripting.wait_for_jobs_to_finish(ctx);
        on_frame_update(ctx, delta_t(0.016667f));
    }


    exit_prefab_mode(ctx, save_option::no);

    undo_stack.clear();
    pending_actions.clear();

    save_selection(ctx);

    clear(false);

    const auto& scenes = scene::get_all_scenes();
    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "save_checkpoints");
        caches_.clear();
        for(auto scn : scenes)
        {
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            save_checkpoint(ctx, cache);
            cache.scn = nullptr;
        }
    }


    // Unload scenes BEFORE unloading domains to prevent script_component destructors
    // from trying to free GC handles from the old domain
    unload_scenes_scripting(scenes);

    {
        scripting.wait_for_jobs_to_finish(ctx);
        on_frame_update(ctx, delta_t(0.016667f));
    }

    reload_script_domains(ctx, scripting, true);

    const bool defer_game_scene = ctx.has<settings>() && ctx.get<settings>().splash.enabled;

    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_checkpoints");

        for(auto scn : scenes)
        {
            if(defer_game_scene && scn->tag == "game")
            {
                continue;
            }
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            load_checkpoint(ctx, cache, true);
            cache.scn = nullptr;
        }
    }

    pop_undo_stack_enabled();

    waiting_for_compilation_before_play_ = false;

}

void editing_manager::on_play_begin(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(!ctx.has<settings>() || !ctx.get<settings>().splash.enabled)
    {
        return;
    }

    auto cache_it = caches_.find("game");
    if(cache_it == caches_.end())
    {
        return;
    }

    for(auto scn : scene::get_all_scenes())
    {
        if(scn->tag != "game")
        {
            continue;
        }
        auto& cache = cache_it->second;
        cache.scn = scn;
        load_checkpoint(ctx, cache, true);
        cache.scn = nullptr;
        break;
    }
}

void editing_manager::on_play_after_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    push_undo_stack_enabled(false);  

    unselect();

    auto& scripting = ctx.get_cached<script_system>();
    {
        scripting.wait_for_jobs_to_finish(ctx);
        on_frame_update(ctx, delta_t(0.016667f));
    }
    
    undo_stack.clear();
    pending_actions.clear();

    
    clear(false);


    const auto& scenes = scene::get_all_scenes();
    for(auto scn : scenes)
    {
        // GAME scene is not saved. Any changes to it during play will be lost.
        if(scn->tag == "game")
        {
            continue;
        }

        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        save_checkpoint(ctx, cache);
        cache.scn = nullptr;
    }

    // Unload scenes BEFORE unloading domains to prevent script_component destructors
    // from trying to free GC handles from the old domain
    unload_scenes_scripting(scenes);

    {
        scripting.wait_for_jobs_to_finish(ctx);
        on_frame_update(ctx, delta_t(0.016667f));
    }

    reload_script_domains(ctx, scripting, false);

    for(auto scn : scenes)
    {
        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        load_checkpoint(ctx, cache, true);
        cache.scn = nullptr;

        sync_prefab_instances(ctx, scn);

    }

    pop_undo_stack_enabled();

    caches_.clear();

    ctx.get_cached<simulation>().set_time_scale(1.0f);
}

void editing_manager::on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version)
{
    queue_action("Script Recompile", [&]() {
        if(waiting_for_compilation_before_play_)
        {
            return;
        }
        undo_stack.clear();

        save_selection(ctx);

        
        push_undo_stack_enabled(false);  

        clear(false);

        const auto& scenes = scene::get_all_scenes();
        caches_.clear();
        for(auto scn : scenes)
        {
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            save_checkpoint(ctx, cache);
            cache.scn = nullptr;
        }

        // Unload scenes BEFORE unloading domains to prevent script_component destructors
        // from trying to free GC handles from the old domain
        unload_scenes_scripting(scenes);

        auto& scripting = ctx.get_cached<script_system>();
        reload_script_domains(ctx, scripting, false);

        for(auto scn : scenes)
        {
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            load_checkpoint(ctx, cache, true);
            cache.scn = nullptr;
        }

        caches_.clear();
        pop_undo_stack_enabled();
    });
}

void editing_manager::save_selection(rtti::context& ctx)
{
    selection_cache_ = {};
    for(auto sel : try_get_selections_as<entt::handle>())
    {
        if(sel)
        {
            if(sel->valid())
            {
                auto& id_comp = sel->get_or_emplace<id_component>();
                id_comp.generate_if_nil();
                selection_cache_.uids.emplace_back(id_comp.id);
            }
            unselect(*sel);
        }
    }
}

void editing_manager::save_checkpoint(rtti::context& ctx, scene_cache& cache)
{
    if(!cache.scn)
    {
        return;
    }
    // APPLOG_TRACE("save_checkpoint {}", cache.scn->tag);

    cache.cache = {};
    cache.cache_source = cache.scn->source;
    // first save scene
    // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "save_to_stream");

    // An in-memory snapshot that load_checkpoint reads back and then drops. Nobody sees
    // it, and it is written on play start, play stop and every script recompile.
    serialization::scoped_output_format compact(serialization::output_format::compact);
    save_to_stream(cache.cache, *cache.scn);
}

void editing_manager::load_checkpoint(rtti::context& ctx, scene_cache& cache, bool recover_selection)
{
    if(!cache.scn)
    {
        return;
    }

    // APPLOG_TRACE("load_checkpoint {}", cache.scn->tag);
    // clear scene
    cache.scn->unload();

    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_from_stream");
        load_from_stream(cache.cache, *cache.scn);
    }

    cache.scn->source = cache.cache_source;

    std::vector<entt::handle> entities;

    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_checkpoint_selection");

        cache.scn->registry->view<id_component>().each(
            [&](auto e, auto&& comp)
            {
                auto uid = comp.id;
                if(std::find(selection_cache_.uids.begin(), selection_cache_.uids.end(), uid) != selection_cache_.uids.end())
                {
                    entities.emplace_back(cache.scn->create_handle(e));
                }
            });
    
        for(auto entity : entities)
        {
            if(recover_selection)
            {
                select(entity, select_mode::shift);
            }
        }
    }
 

    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_checkpoint_update");
        delta_t dt(0.016667f);

        auto& rpath = ctx.get_cached<rendering_system>();
        rpath.on_frame_update(*cache.scn, dt);
        rpath.on_frame_before_render(*cache.scn, dt);
    }


    cache.scn = nullptr;
}

void editing_manager::on_prefab_updated(const asset_handle<prefab>& pfb)
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& ev = ctx.get_cached<events>();

    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return;
    }


    const auto& scenes = scene::get_all_scenes();
    for(auto scn : scenes)
    {

        std::vector<entt::handle> affected_entities;
        scn->registry->view<prefab_component>().each(
            [&](auto e, auto&& prefab_comp)
            {
                auto entity = scn->create_handle(e);
                if(prefab_comp.source == pfb)
                {
                    affected_entities.emplace_back(entity);
                }
            });

        for(auto& entity : affected_entities)
        {
            sync_prefab_entity(ctx, entity, pfb);    
        }
    }
}

void editing_manager::sync_prefab_entity(rtti::context& ctx, entt::handle entity, const asset_handle<prefab>& pfb)
{
    queue_action("Sync Prefab Entity",
        [&ctx, entity, pfb]() mutable
    {
        auto& play = ctx.get_cached<play_mode>();
        if(play.is_active())
        {
            return;
        }

        if(!entity.valid() || !pfb.is_valid())
        {
            return;
        }

        // The sync itself lives in the engine. It is the version that knows about nesting:
        // it snapshots each nested instance's locally-made overrides and removals before the
        // replay and puts them back after, filters the document's snapshot of a nested
        // instance through that instance's override set, and copies the prefab_component
        // rather than holding a reference across a load that adds and removes them (the pool
        // swap-and-pops). The editor used to keep its own pre-nesting copy of this dance,
        // which replayed stale snapshots over live nested instances.
        if(auto* prefab_comp = entity.try_get<prefab_component>())
        {
            prefab_comp->source = pfb;
        }

        sync_prefab_instance(entity);
    });
}

void editing_manager::sync_prefab_instances(rtti::context& ctx, scene* scn)
{
    // Top-level instances only. An instance nested inside another is refreshed by its
    // container's sync - load_from_prefab_out cascades into everything nested - so syncing
    // it from here as well repeated the whole replay once per nesting level.
    scn->registry->view<prefab_component>().each(
    [&](auto e, auto&& comp)
    {
        auto owner = comp.get_owner();
        const auto* trans_comp = owner.template try_get<transform_component>();
        auto parent = trans_comp != nullptr ? trans_comp->get_parent() : entt::handle{};
        while(parent)
        {
            if(parent.all_of<prefab_component>())
            {
                return;
            }
            const auto* parent_trans = parent.try_get<transform_component>();
            parent = parent_trans != nullptr ? parent_trans->get_parent() : entt::handle{};
        }
        sync_prefab_entity(ctx, owner, comp.source);
    });
}

auto editing_manager::get_select_mode() const -> select_mode
{
    select_mode mode = select_mode::normal;

    if(ImGui::IsKeyDown(ImGuiKey_LeftShift))
    {
        mode = select_mode::shift;
    }
    if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        mode = select_mode::ctrl;
    }

    return mode;
}

void editing_manager::on_frame_update(rtti::context& ctx, delta_t dt)
{
    session.tick();

    execute_actions();

    if(focused_data.remaining_time > delta_t::zero())
    {
        focused_data.remaining_time -= dt;
    }

    undo_stack.last_action_elapsed_time += dt;

    if(focused_data.remaining_time <= delta_t::zero())
    {
        unfocus();
    }

    auto& play = ctx.get_cached<play_mode>();

    // Only evict assets if not playing
    if(!play.is_active())
    {
        using namespace std::chrono;
        static auto last_eviction = steady_clock::now();
        auto now = steady_clock::now();
        if(now - last_eviction > seconds(30))
        {
            auto& am = ctx.get_cached<asset_manager>();
            am.evict_unused_assets("app:/", seconds(60));
            last_eviction = now;
        }
    }
}
void editing_manager::focus(entt::meta_any object)
{
    focused_data.object = object;
    focused_data.remaining_time = delta_t(0.5f);
}

void editing_manager::focus_path(const fs::path& object)
{
    focused_data.focus_path = object;
}

void editing_manager::unselect(bool clear_selection_tools)
{
    // Capture the old selection state before clearing
    std::vector<entt::meta_any> old_selection = selection_data.objects;

    // Perform the unselect operation
    selection_data = {};

    if(clear_selection_tools)
    {
        ImGuizmo::Enable(false);
        ImGuizmo::Enable(true);
    }

    // Capture the new selection state and create action
    std::vector<entt::meta_any> new_selection = selection_data.objects;
    
    // Only create action if selection actually changed
    bool selection_changed = old_selection.size() != new_selection.size();
    if (!selection_changed && !old_selection.empty())
    {
        // Check if contents are different
        for (size_t i = 0; i < old_selection.size() && !selection_changed; ++i)
        {
            if (i >= new_selection.size() || old_selection[i] != new_selection[i])
            {
                selection_changed = true;
                break;
            }
        }
    }

    if (selection_changed)
    {
        push_undo_stack_enabled(true);
        queue_action("Unselect", std::make_shared<selection_action_t>(this, old_selection, new_selection, false));
        pop_undo_stack_enabled();
    }
}

void editing_manager::unfocus()
{
    focused_data = {};
}

void editing_manager::enter_prefab_mode(rtti::context& ctx, const asset_handle<prefab>& prefab, bool auto_save)
{
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return;
    }

    
    auto on_continue = [this,&ctx, prefab]()
    {
        // Store the prefab we're editing
        edited_prefab = prefab;
        current_mode = editing_mode::prefab;
        
        // Clear selection
        unselect();
        
        // Create a new scene for prefab editing if it doesn't exist
        prefab_scene.unload();

        // Set up a default 3D scene with lighting
        defaults::create_default_3d_scene_for_editing(ctx, prefab_scene);
        
        // Instantiate the prefab in our editing scene
        prefab_entity = prefab_scene.instantiate(prefab);
        
        // Select the prefab entity
        if (prefab_entity)
        {
            select(prefab_entity);
        }
        
        APPLOG_INFO("Entered prefab editing mode for: {}", prefab.id());
    };

    if (is_prefab_mode())
    {
        // Already in prefab mode, check if we need to save changes
        if (edited_prefab != prefab)
        {
            auto on_save = [this,&ctx]()
            {
                save_prefab_changes(ctx);
            };

            if(auto_save)
            {
                on_save();
            }
            else
            {
                prompt_save_changes(ctx, on_save, on_continue);
                return;
            }
        }
        else
        {
            select(prefab_entity);

            // Already editing this prefab, nothing to do
            return;
        }
    }

    on_continue();


}

auto editing_manager::prompt_save_changes(rtti::context& ctx, const std::function<void()>& on_save, const std::function<void()>& on_continue) -> bool
{
    ImBox::ShowSaveConfirmation("Save prefab?",
        "Do you want to save the changes you made?",
        [&ctx, on_save, on_continue](ImBox::ModalResult result)
    {
        if(result == ImBox::ModalResult::Save)
        {
            on_save();
        }

        if(result != ImBox::ModalResult::Cancel)
        {
            on_continue();
        }
    });

    return true;
}

void editing_manager::exit_prefab_mode(rtti::context& ctx, save_option save_changes)
{
    if (!is_prefab_mode())
    {
        return;
    }
    
    auto on_save = [this,&ctx]()
    {
        save_prefab_changes(ctx);
    };

    auto on_continue = [this, &ctx]()
    {
            // Reset state
        current_mode = editing_mode::scene;
        edited_prefab = {};
        prefab_entity = {};
        prefab_scene.unload();
        
        // Clear selection
        unselect();
        
        APPLOG_INFO("Exited prefab editing mode");
    };
    
    switch (save_changes)
    {
        case save_option::yes:
            on_save();
            on_continue();
            break;
            
        case save_option::no:
            on_continue();
            break;
            
        case save_option::prompt:
            prompt_save_changes(ctx, on_save, on_continue);
            break;
    }
    
    // if (should_save)
    // {
    //     save_prefab_changes(ctx);
    // }
    
    // // Reset state
    // current_mode = editing_mode::scene;
    // edited_prefab = {};
    // prefab_entity = {};
    // prefab_scene.unload();
    
    // // Clear selection
    // unselect();
    
    // APPLOG_INFO("Exited prefab editing mode");
}

void editing_manager::save_prefab_changes(rtti::context& ctx)
{
    if (!is_prefab_mode() || !edited_prefab || !prefab_entity)
    {
        return;
    }
    
    // Make sure the entity is valid
    if (!prefab_entity.valid())
    {
        APPLOG_ERROR("Failed to save prefab: Invalid entity");
        ImGui::PushNotification(ImGuiToast(ImGuiToastType_Error, 1000,"Failed to save prefab."));

        return;
    }
    
    auto prefab_path = fs::resolve_protocol(edited_prefab.id());
    asset_writer::atomic_save_to_file(prefab_path.string(), prefab_entity);

    APPLOG_INFO("Saved changes to prefab: {}", edited_prefab.id());
    ImGui::PushNotification(ImGuiToast(ImGuiToastType_Success, 1000,"Prefab saved."));

}


auto editing_manager::get_active_scene(rtti::context& ctx) -> scene*
{
    if (is_prefab_mode())
    {
        return &prefab_scene;
    }
 
    auto& ec = ctx.get_cached<ecs>();
    return &ec.get_scene();

}

void editing_manager::unload_scenes_scripting(const std::vector<scene*>& scenes)
{
    // Only clear script_components to free GC handles before domain unload
    // Don't unload entire scenes as that destroys rendering resources that
    // might still be referenced by the graphics system
    for(auto scn : scenes)
    {
        // Clear only script_components to free GC handles
        // The scenes will be properly unloaded in load_checkpoint
        scn->registry->clear<script_component>();
    }
}

void editing_manager::clear(bool clear_unsaved)
{
    if(clear_unsaved)
    {
        clear_unsaved_changes();
    }
    unselect();
    unfocus();

    // Clear pending actions and undo/redo stack
    pending_actions.clear();
    undo_stack.clear();

    // If in prefab mode, exit it
    if (is_prefab_mode())
    {
        auto& ctx = engine::context();
        exit_prefab_mode(ctx, save_option::no);
    }

    // Reset prefab editing mode and clean up all references
    current_mode = editing_mode::scene;
    edited_prefab = {};
    prefab_entity = {};
    
}


void editing_manager::do_action(const std::string& name, const std::function<void()>& action)
{
    editing_manager::do_action<untracked_action_t>(name, action);
}

void editing_manager::do_action(const std::string& name, const std::function<void()>& do_action, const std::function<void()>& undo_action)
{
    editing_manager::do_action<tracked_lambda_action_t>(name, do_action, undo_action);
}

void editing_manager::do_action(const std::string& name, std::shared_ptr<editing_action_t> action)
{
    add_action(name, action, true);
}

void editing_manager::queue_action(const std::string& name, const std::function<void()>& action)
{
    editing_manager::queue_action<untracked_action_t>(name, action);
}

void editing_manager::queue_action(const std::string& name, const std::function<void()>& do_action, const std::function<void()>& undo_action)
{
    editing_manager::queue_action<tracked_lambda_action_t>(name, do_action, undo_action);
}

void editing_manager::queue_action(const std::string& name, std::shared_ptr<editing_action_t> action)
{
    add_action(name, action, false);
}


void editing_manager::add_action(const std::string& name, std::shared_ptr<editing_action_t> action, bool immediate)
{
    if (!action)
    {
        return;
    }

    action->merge_key = session.current_merge_key();

    if(!name.empty())
    {
        action->name = name;
    }

    if(undo_stack_enabled.empty())
    {
        action->undoable = false;
    }
    else
    {
        action->undoable = undo_stack_enabled.top();
    }

    if(!immediate)
    {
        action->detach();
    }
    
    // Queue the action for execution (don't execute immediately)
    pending_actions.push_back(std::move(action));

    if(immediate)
    {
        execute_actions();
    }
}


void editing_manager::push_undo_stack_enabled(bool enabled)
{
    bool last_enabled = true;
    if(!undo_stack_enabled.empty())
    {
        last_enabled = undo_stack_enabled.top();
    }

    undo_stack_enabled.push(enabled && last_enabled);
}
void editing_manager::pop_undo_stack_enabled()
{
    undo_stack_enabled.pop();
}

void editing_manager::execute_actions()
{
    while(!pending_actions.empty())
    {
        auto actions = std::move(pending_actions);
        // Process all pending actions
        for (auto& action : actions)
        {
            if (action)
            {
                // Execute the action
                action->execution_count++;
                action->do_action();
                
                on_action_executed(action);
                // Add to undo stack if the action is undoable
                // Note: We need to handle merging here since the action is now executed
                if (action->is_undoable())
                {
                    // Move the action to the undo stack
                    undo_stack.push_if_undoable(std::move(action));
                }

                
            }
        }

    }

}

auto editing_manager::undo() -> std::shared_ptr<editing_action_t>
{
    if (undo_stack.can_undo())
    {
        has_unsaved_changes_ = true;
        return undo_stack.undo();
    }
    return nullptr;
}

auto editing_manager::redo() -> std::shared_ptr<editing_action_t>
{
    if (undo_stack.can_redo())
    {
        has_unsaved_changes_ = true;
        return undo_stack.redo();
    }
    return nullptr;
}

void editing_manager::on_action_executed(std::shared_ptr<editing_action_t> action)
{
    // Auto-rebuild reflection probes on any scene-mutating action while editing. This mirrors the
    // experience of Unity/Unreal where moving environment geometry refreshes the bakes in the background.
    // We intentionally do nothing in play mode - runtime behavior is governed by probe_update_mode.
    if(action->modifies_scene_content())
    {
        has_unsaved_changes_ = true;

        if(auto_rebuild_reflection_probes)
        {
            auto& ctx = engine::context();
            auto& play = ctx.get_cached<play_mode>();
            if(!play.is_active())
            {
                // Time-sliced rebuild so repeated gizmo drags don't stall the editor.
                editor_actions::rebuild_reflection_probes(ctx, false);
            }
        }
    }
}

} // namespace unravel
