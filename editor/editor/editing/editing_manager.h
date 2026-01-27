#pragma once
#include "editor_actions.h"
#include "uuid/uuid.h"

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <math/math.h>
#include <uuid/uuid.h>

#include <editor/imgui/integration/imgui.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include "actions/undo_redo_stack.h"
#include "actions/actions.h"

namespace unravel
{

struct editing_manager
{
    undo_redo_stack undo_stack;
    std::vector<std::shared_ptr<editing_action_t>> pending_actions; // Actions waiting to be executed
    std::stack<bool> undo_stack_enabled;

    struct selection
    {
        std::vector<entt::meta_any> objects{entt::meta_any{}};
    };

    struct focused
    {
        entt::meta_any object;
        delta_t remaining_time{};

        fs::path focus_path{};
    };

    struct snap
    {
        ///
        math::vec3 translation_snap = {1.0f, 1.0f, 1.0f};
        ///
        float rotation_degree_snap = 15.0f;
        ///
        float scale_snap = 0.1f;
    };

    struct grid
    {
        float opacity = 1.0f;
        bool depth_aware {true};
    };

    struct billboard_gizmos
    {
        float opacity = 0.75f;
        float size = 0.5f;
        bool depth_aware {false};
        bool show_camera {true};
        bool show_light {true};
        bool show_reflection_probe {true};
        bool show_audio_source {true};
        bool show_particle_emitter {true};
    };

    struct gizmos_data
    {
        bool show_selection_outline {true};
        bool show_camera {true};
        bool show_light {true};
        bool show_reflection_probe {true};
        bool show_model {true};
        bool show_model_bounds {false};
        bool show_model_local_bounds {false};
        bool show_model_submesh_local_bounds {false};
        bool show_model_lod {false};
        bool show_text {true};
        bool show_particle_emitter {true};
        bool show_component_gizmos {true};
        bool show_particle_emitter_bounds {true};
        bool show_particle_emitter_shape {true};
        bool show_particle_emitter_direction {true};
    };

    struct inverse_kinematics
    {
        int num_nodes = 2;
    };

    enum select_mode
    {
        normal,
        ctrl,
        shift,
    };

    enum class editing_mode
    {
        scene,
        prefab
    };
    
    enum class save_option
    {
        yes,    // Save changes
        no,     // Don't save changes
        prompt  // Prompt the user whether to save changes
    };

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    void on_play_before_begin(rtti::context& ctx);
    void on_play_after_end(rtti::context& ctx);
    void on_frame_update(rtti::context& ctx, delta_t);
    void on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version);
    void on_prefab_updated(const asset_handle<prefab>& pfb);

    void sync_prefab_entity(rtti::context& ctx, entt::handle entity, const asset_handle<prefab>& pfb);
    void sync_prefab_instances(rtti::context& ctx, scene* scn);
    auto get_select_mode() const -> select_mode;

    //-----------------------------------------------------------------------------
    //  Name : select ()
    /// <summary>
    /// Selects an object. Can be anything.
    /// </summary>
    //-----------------------------------------------------------------------------
    void focus(entt::meta_any object);
    void focus_path(const fs::path& object);

    //-----------------------------------------------------------------------------
    //  Name : unselect ()
    /// <summary>
    /// Clears the selection data.
    /// </summary>
    //-----------------------------------------------------------------------------
    void unselect(bool clear_selection_tools = true);
    void unfocus();

    //-----------------------------------------------------------------------------
    //  Name : try_unselect ()
    /// <summary>
    /// Clears the selection data if it maches the type.
    /// </summary>
    //-----------------------------------------------------------------------------
    template<typename T>
    void try_unselect()
    {
        if(is_selected_type<T>())
        {
            unselect();
        }
    }

    template<typename T>
    void try_unfocus()
    {
        if(focused_data.object.type() == entt::resolve<T>())
        {
            unfocus();
        }
    }

    template<typename T>
    auto is_selected(const T& entry) -> bool
    {
        for(const auto& object : selection_data.objects)
        {
            if(is_selected_impl(entry, object))
            {
                return true;
            }
        }
        return false;
    }

    template<typename T>
    auto is_selected_type() -> bool
    {
        const auto& selected = get_active_selection();

        return selected && selected.type() == entt::resolve<T>();
    }

    auto get_active_selection() const -> const entt::meta_any&
    {
        return selection_data.objects.back();
    }

    auto get_active_selection() -> entt::meta_any&
    {
        return selection_data.objects.back();
    }

    auto get_selections() const -> hpp::span<const entt::meta_any>
    {
        return selection_data.objects;
    }

    auto get_selections() -> hpp::span<entt::meta_any>
    {
        return selection_data.objects;
    }

    template<typename T>
    auto get_active_selection_as() const -> const T&
    {
        return get_active_selection().cast<const T&>();
    }

    template<typename T>
    auto try_get_active_selection_as() -> T*
    {
        auto& active = get_active_selection();
        if(active.type() == entt::resolve<T>())
        {
            return &active.cast<T&>();
        }

        return nullptr;
    }

    template<typename T>
    auto try_get_active_selection_as() const -> const T*
    {
        const auto& active = get_active_selection();
        if(active.type() == entt::resolve<T>())
        {
            return &active.cast<T&>();
        }

        return nullptr;
    }

    template<typename T>
    auto try_get_selections_as() const -> std::vector<const T*>
    {
        std::vector<T*> result;

        for(const auto& obj : selection_data.objects)
        {
            if(obj.type() == entt::resolve<T>())
            {
                result.emplace_back(&obj.cast<T&>());
            }
        }

        return result;
    }

    template<typename T>
    auto try_get_selections_as() -> std::vector<T*>
    {
        std::vector<T*> result;

        for(auto& obj : selection_data.objects)
        {
            if(obj.type() == entt::resolve<T>())
            {
                result.emplace_back(&obj.cast<T&>());
            }
        }

        return result;
    }

    template<typename T>
    auto try_get_selections_as_copy() const -> std::vector<T>
    {
        std::vector<T> result;

        for(const auto& obj : selection_data.objects)
        {
            if(obj.type() == entt::resolve<T>())
            {
                result.emplace_back(obj.cast<T>());
            }
        }

        return result;
    }

    template<typename T>
    auto is_focused(const T& entry) -> bool
    {
        const auto& focused = focused_data.object;

        if(focused.type() != entt::resolve<T>())
        {
            return false;
        }

        return focused.cast<T>() == entry;
    }

    template<typename T>
    auto is_focused(const asset_handle<T>& entry) -> bool
    {
        const auto& focused = focused_data.object;

        if(focused.type() == entt::resolve<asset_handle<T>>())
        {
            return focused.cast<asset_handle<T>>() == entry;
        }

        if(focused.type() != entt::resolve<fs::path>())
        {
            return false;
        }

        return focused.cast<fs::path>() == fs::resolve_protocol(entry.id());
    }

    template<typename T>
    auto try_get_active_focus_as() const -> const T*
    {
        const auto& focused = focused_data.object;
        if(focused.type() == entt::resolve<T>())
        {
            return focused.try_cast<T>();
        }

        return nullptr;
    }

    template<typename T>
    void unselect(const T& entry)
    {
        // Capture the old selection state before making changes
        std::vector<entt::meta_any> old_selection = selection_data.objects;
        
        // Perform the unselect operation
        unselect_impl(entry);
        
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

    template<typename T>
    void select(const T& entry, select_mode mode = select_mode::normal, std::string hint = "")
    {
        if(hint.empty())
        {
            if constexpr(std::is_same_v<T, entt::handle>)
            {
                
                if(auto tag = entry.template try_get<tag_component>())
                {
                    hint = tag->name + " (Entity)";
                }
                else
                {
                    hint = "Entity";
                }
            }
        }
        // Capture the old selection state before making changes
        std::vector<entt::meta_any> old_selection = selection_data.objects;
        
        // Perform the select operation
        select_impl(entry, mode);
        
        // Capture the new selection state and create action
        std::vector<entt::meta_any> new_selection = selection_data.objects;
        
        // Only create action if selection actually changed
        bool selection_changed = old_selection.size() != new_selection.size();
        if (!selection_changed)
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
            queue_action("Select " + hint, std::make_shared<selection_action_t>(this, old_selection, new_selection, true));
            pop_undo_stack_enabled();
        }

        // auto& ctx = engine::context();
        // auto& ui_ev = ctx.get_cached<ui_events>();
        // ui_ev.on_selection_changed();
    }

    // Unified action system - all actions go through this interface
    // Whether an action is undoable depends on the action's is_undoable() method
    //
    // Usage examples:
    // 1. Non-undoable action: do_action("Quick Fix", []() { /* do something */ });
    // 2. Undoable action: do_action("Move", []() { move(); }, []() { restore(); });
    // 3. Custom action: do_action<transform_move_action_t>("Move Entity", entity, old_pos, new_pos);
    // 4. Manual undo/redo: undo(), redo(), can_undo(), can_redo()
    
    // Add an action with a lambda/function (non-undoable)
    void do_action(const std::string& name, const std::function<void()>& action);
    
    // Add an action with both do and undo lambdas (undoable)
    void do_action(const std::string& name, const std::function<void()>& do_action, const std::function<void()>& undo_action);
    
    // Add any action object (undoable or non-undoable determined by action itself)
    void do_action(const std::string& name, std::shared_ptr<editing_action_t> action);
    
    template<typename ActionType, typename... Args>
    void do_action(const std::string& name, Args&&... args)
    {
        auto action = std::make_shared<ActionType>(std::forward<Args>(args)...);
        do_action(name, std::move(action));
    }

    void queue_action(const std::string& name, const std::function<void()>& action);
    
    // Add an action with both do and undo lambdas (undoable)
    void queue_action(const std::string& name, const std::function<void()>& do_action, const std::function<void()>& undo_action);
    
    // Add any action object (undoable or non-undoable determined by action itself)
    void queue_action(const std::string& name, std::shared_ptr<editing_action_t> action);
    
    template<typename ActionType, typename... Args>
    void queue_action(const std::string& name, Args&&... args)
    {
        auto action = std::make_shared<ActionType>(std::forward<Args>(args)...);
        queue_action(name, std::move(action));
    }

    void add_action(const std::string& name, std::shared_ptr<editing_action_t> action, bool immediate = true);


    void push_undo_stack_enabled(bool enabled);
    void pop_undo_stack_enabled();
    
    // Execute all pending actions (called automatically each frame)
    void execute_actions();
    
    // Undo/Redo operations
    void undo();
    void redo();
    auto can_undo() const -> bool { return undo_stack.can_undo(); }
    auto can_redo() const -> bool { return undo_stack.can_redo(); }
    

    // Pending actions management
    auto has_pending_actions() const -> bool { return !pending_actions.empty(); }
    auto get_pending_actions_count() const -> size_t { return pending_actions.size(); }
    
    auto has_unsaved_changes() const -> bool { return has_unsaved_changes_; }
    void clear_unsaved_changes() { has_unsaved_changes_ = false; }

    void clear(bool clear_unsaved = true);

    // Prefab editing mode methods
    void enter_prefab_mode(rtti::context& ctx, const asset_handle<prefab>& prefab, bool auto_save = false);
    void exit_prefab_mode(rtti::context& ctx, save_option save_changes = save_option::prompt);
    auto is_prefab_mode() const -> bool { return current_mode == editing_mode::prefab; }
    void save_prefab_changes(rtti::context& ctx);
    
    // Returns the active scene based on the current edit mode
    auto get_active_scene(rtti::context& ctx) -> scene*;

    void unload_scenes_scripting(const std::vector<scene*>& scenes);

    /// enable editor grid
    bool show_grid = true;
    /// enable editor icon gizmos
    bool show_icon_gizmos = true;
    /// enable wireframe selection
    bool wireframe_selection = true;
    /// current manipulation gizmo operation.
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    /// current manipulation gizmo space.
    ImGuizmo::MODE mode = ImGuizmo::LOCAL;
    /// selection data containing selected object
    selection selection_data;

    focused focused_data;

    /// snap data containging various snap options
    snap snap_data;
    grid grid_data;
    billboard_gizmos billboard_data;
    gizmos_data gizmos;

    inverse_kinematics ik_data;
    
    // Current editing mode
    editing_mode current_mode = editing_mode::scene;
    
    // Currently edited prefab
    asset_handle<prefab> edited_prefab;
    
    // The entity created from the prefab that we're editing
    entt::handle prefab_entity;
    
    // Separate scene for prefab editing
    scene prefab_scene{"prefab_scene"};


    void sanity_check_selection_data()
    {
        if(selection_data.objects.empty())
        {
            selection_data = {};
        }
    }

    // Helper method to restore selection from a snapshot (used by actions)
    void restore_selection_impl(const std::vector<entt::meta_any>& selection_snapshot)
    {
        selection_data.objects.clear();
        for (const auto& obj : selection_snapshot)
        {
            // Only add valid selections
            if (obj)
            {
                // Check if it's an entity handle and if it's still valid
                if (obj.type() == entt::resolve<entt::handle>())
                {
                    auto handle = obj.cast<const entt::handle&>();
                    if (handle && handle.valid())
                    {
                        selection_data.objects.emplace_back(obj);
                    }
                }
                else
                {
                    // For non-entity selections (like asset handles), just add them
                    selection_data.objects.emplace_back(obj);
                }
            }
        }
        sanity_check_selection_data();
    }

    private:
    template<typename T>
    auto is_selected_impl(const T& entry, const entt::meta_any& selected) -> bool
    {
        if(selected.type() != entt::resolve<T>())
        {
            return false;
        }

        return selected.cast<const T&>() == entry;
    }

    // Internal implementation methods that perform selection without creating actions
    template<typename T>
    void select_impl(const T& entry, select_mode mode = select_mode::normal)
    {
        focus(entry);
        switch(mode)
        {
            case select_mode::normal:
            {
                selection_data.objects.clear();
                selection_data.objects.emplace_back(entry);
                break;
            }
            case select_mode::ctrl:
            {
                if(!selection_data.objects.empty())
                {
                    if(!selection_data.objects.back())
                    {
                        selection_data.objects.clear();
                    }
                }

                if(!is_selected(entry))
                {
                    selection_data.objects.emplace_back(entry);
                }
                else
                {
                    unselect_impl(entry);
                }
                break;
            }
            case select_mode::shift:
            {
                if(!selection_data.objects.empty())
                {
                    if(!selection_data.objects.back())
                    {
                        selection_data.objects.clear();
                    }
                }

                if(!is_selected(entry))
                {
                    selection_data.objects.emplace_back(entry);
                }
                else
                {
                    // make it active
                    unselect_impl(entry);

                    if(!selection_data.objects.back())
                    {
                        selection_data.objects.clear();
                    }
                    selection_data.objects.emplace_back(entry);
                }
                break;
            }
            default:
                break;
        }

        sanity_check_selection_data();
    }

    template<typename T>
    void unselect_impl(const T& entry)
    {
        std::erase_if(selection_data.objects,
                      [&](const auto& el)
                      {
                          return is_selected_impl(entry, el);
                      });
        sanity_check_selection_data();
    }

    struct scene_cache
    {
        scene* scn = nullptr;
        std::stringstream cache;
        asset_handle<scene_prefab> cache_source;
    };

    struct selection_cache
    {
        std::vector<hpp::uuid> uids;
    };

    std::map<std::string, scene_cache> caches_;
    selection_cache selection_cache_;
    void save_selection(rtti::context& ctx);
    void save_checkpoint(rtti::context& ctx, scene_cache& cache);
    void load_checkpoint(rtti::context& ctx, scene_cache& cache, bool recover_selection = false);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    bool waiting_for_compilation_before_play_{};
    bool has_unsaved_changes_;
    

    // Prompts the user to save changes and returns true if changes should be saved
    auto prompt_save_changes(rtti::context& ctx, const std::function<void()>& on_save, const std::function<void()>& on_continue) -> bool;
};
} // namespace unravel

