#include "editing_manager.h"
#include "engine/profiler/profiler.h"
#include "imgui/imgui.h"
#include "logging/logging.h"
#include <chrono>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>

#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/defaults/defaults.h>
#include <engine/meta/ecs/entity.hpp>

#include <engine/assets/impl/asset_writer.h>
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <imgui_widgets/gizmo.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/imgui/integration/imgui_messagebox.h>

#include <filedialog/filedialog.h>

namespace unravel
{

namespace
{
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
    ev.on_play_after_end.connect(sentinel_, -1000, this, &editing_manager::on_play_after_end);
    ev.on_frame_update.connect(sentinel_, 1000, this, &editing_manager::on_frame_update);
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


    exit_prefab_mode(ctx, save_option::no);

    save_selection(ctx);

    const auto& scenes = scene::get_all_scenes();
    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "save_checkpoints");
        caches_.clear();
        for(auto scn : scenes)
        {
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            save_checkpoint(ctx, cache);
        }
    }

    
    auto& scripting = ctx.get_cached<script_system>();
    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "unload_app_domain");
        scripting.unload_app_domain();
    }
    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "wait_for_jobs_to_finish");
        scripting.wait_for_jobs_to_finish(ctx);
    }
    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_app_domain");
        scripting.load_app_domain(ctx, true);
    }

    {
        // APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "load_checkpoints");

        for(auto scn : scenes)
        {
            auto& cache = caches_[scn->tag];
            cache.scn = scn;
            load_checkpoint(ctx, cache, true);
        }
    }

    waiting_for_compilation_before_play_ = false;

}

void editing_manager::on_play_after_end(rtti::context& ctx)
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    unselect();

    const auto& scenes = scene::get_all_scenes();
    for(auto scn : scenes)
    {
        if(scn->tag == "game")
        {
            continue;
        }

        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        save_checkpoint(ctx, cache);
    }

    auto& scripting = ctx.get_cached<script_system>();
    scripting.unload_app_domain();
    scripting.load_app_domain(ctx, false);
        
    for(auto scn : scenes)
    {
        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        load_checkpoint(ctx, cache, true);

        sync_prefab_instances(ctx, scn);

    }

    caches_.clear();

    undo_stack.clear();
    pending_actions.clear();
}

void editing_manager::on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version)
{
    if(waiting_for_compilation_before_play_)
    {
        return;
    }

    save_selection(ctx);

    const auto& scenes = scene::get_all_scenes();
    caches_.clear();
    for(auto scn : scenes)
    {
        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        save_checkpoint(ctx, cache);
    }


    auto& scripting = ctx.get_cached<script_system>();
    scripting.unload_app_domain();
    scripting.load_app_domain(ctx, false);

    for(auto scn : scenes)
    {
        auto& cache = caches_[scn->tag];
        cache.scn = scn;
        load_checkpoint(ctx, cache, true);
    }

    caches_.clear();
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
                entity.remove<id_component>();
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

    if(ev.is_playing)
    {
        return;
    }

    auto& scn = ec.get_scene();

    std::vector<entt::handle> affected_entities;
    scn.registry->view<prefab_component>().each(
        [&](auto e, auto&& prefab_comp)
        {
            auto entity = scn.create_handle(e);
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

void editing_manager::sync_prefab_entity(rtti::context& ctx, entt::handle entity, const asset_handle<prefab>& pfb)
{
    queue_action("Sync Prefab Entity",
        [&ctx, entity, pfb]() mutable
    {
        auto& ec = ctx.get_cached<ecs>();
        auto& ev = ctx.get_cached<events>();
    
        if(ev.is_playing)
        {
            return;
        }

        if(!entity.valid())
        {
            return;
        }

        if(!pfb.is_valid())
        {
            return;
        }

        auto& scn = ec.get_scene();
        if(auto trans_comp = entity.template try_get<transform_component>())
        {
            auto parent = trans_comp->get_parent();
            auto pos = trans_comp->get_position_local();
            auto rot = trans_comp->get_rotation_local();

            auto& prefab_comp = entity.get<prefab_component>();
            // Enable path recording for prefab loading
            serialization::path_context path_ctx;
            path_ctx.should_serialize_property_callback = [&](const std::string& property_path) -> bool
            {
                return !prefab_comp.has_serialization_override(property_path);
            };
            path_ctx.enable_recording();
            serialization::path_context* old_ctx = serialization::get_path_context();
            serialization::set_path_context(&path_ctx);

            
            if(scn.instantiate_out(pfb, entity))
            {
                auto& new_trans = entity.get<transform_component>();
                new_trans.set_position_local(pos);
                new_trans.set_rotation_local(rot);

                new_trans.set_parent(parent, false);
            }

            
            // Restore previous path context
            serialization::set_path_context(old_ctx);
        }

    });
}

void editing_manager::sync_prefab_instances(rtti::context& ctx, scene* scn)
{
    scn->registry->view<prefab_component>().each(
    [&](auto e, auto&& comp)
    {
        sync_prefab_entity(ctx, comp.get_owner(), comp.source);
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

void editing_manager::on_frame_update(rtti::context& ctx, delta_t)
{
    session.tick();

    execute_actions();

    if(focused_data.frames > 0)
    {
        focused_data.frames--;
    }

    if(focused_data.frames == 0)
    {
        unfocus();
    }
}
void editing_manager::focus(entt::meta_any object)
{
    focused_data.object = object;
    focused_data.frames = 20;
}

void editing_manager::focus_path(const fs::path& object)
{
    focused_data.focus_path = object;
}

void editing_manager::unselect(bool clear_selection_tools)
{
    selection_data = {};

    if(clear_selection_tools)
    {
        ImGuizmo::Enable(false);
        ImGuizmo::Enable(true);
    }
}

void editing_manager::unfocus()
{
    focused_data = {};
}

void editing_manager::enter_prefab_mode(rtti::context& ctx, const asset_handle<prefab>& prefab, bool auto_save)
{
    auto& ev = ctx.get_cached<events>();

    if(ev.is_playing)
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

void editing_manager::clear()
{
    clear_unsaved_changes();
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
    return add_action(name, action, true);
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
    return add_action(name, action, false);
}


void editing_manager::add_action(const std::string& name, std::shared_ptr<editing_action_t> action, bool immediate)
{
    if (!action)
    {
        return;
    }
    
    has_unsaved_changes_ = true;


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
    
    // Queue the action for execution (don't execute immediately)
    pending_actions.push_back(std::move(action));

    if(immediate)
    {
        execute_actions();
    }
}


void editing_manager::push_undo_stack_enabled(bool enabled)
{
    undo_stack_enabled.push(enabled);
}
void editing_manager::pop_undo_stack_enabled()
{
    undo_stack_enabled.pop();
}

void editing_manager::execute_actions()
{
    // Process all pending actions
    for (auto& action : pending_actions)
    {
        if (action)
        {
            // Execute the action
            action->execution_count++;
            action->do_action();
            // Add to undo stack if the action is undoable
            // Note: We need to handle merging here since the action is now executed
            if (action->is_undoable())
            {
                // Move the action to the undo stack
                undo_stack.push_if_undoable(std::move(action));
            }
        }
    }
    
    // Clear the pending actions queue
    pending_actions.clear();
}

void editing_manager::undo()
{
    if (undo_stack.can_undo())
    {
        has_unsaved_changes_ = true;
        undo_stack.undo();
    }
}

void editing_manager::redo()
{
    if (undo_stack.can_redo())
    {
        has_unsaved_changes_ = true;
        undo_stack.redo();
    }
}

} // namespace unravel
