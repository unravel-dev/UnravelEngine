#pragma once
#include "editor_actions.h"
#include "uuid/uuid.h"

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <math/math.h>
#include <rttr/variant.h>
#include <uuid/uuid.h>

#include <editor/imgui/integration/imgui.h>

namespace unravel
{

struct editing_action_t
{
    virtual ~editing_action_t() = default;
    std::string name{};

    virtual void do_action() = 0;
    virtual void undo_action() = 0;
};

struct untracked_action_t : editing_action_t
{
    using action_t = std::function<void()>;
    action_t action{};

    void do_action() override { action(); }
    void undo_action() override {  }
};

struct transform_move_action_t : editing_action_t
{
    void do_action() override
    {
        // TODO: Implement
    }
    void undo_action() override
    {
        // TODO: Implement
    }
};

struct editing_manager
{
    using editing_actions_t = std::vector<std::unique_ptr<editing_action_t>>;

    struct selection
    {
        std::vector<rttr::variant> objects{rttr::variant{}};
    };

    struct focused
    {
        //rttr::variant object;
        entt::meta_any object;
        int frames{};

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
    // void select(rttr::variant object, bool add = false);
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

        return selected && selected.is_type<T>();
    }

    auto get_active_selection() const -> const rttr::variant&
    {
        return selection_data.objects.back();
    }

    auto get_active_selection() -> rttr::variant&
    {
        return selection_data.objects.back();
    }

    auto get_selections() const -> hpp::span<const rttr::variant>
    {
        return selection_data.objects;
    }

    auto get_selections() -> hpp::span<rttr::variant>
    {
        return selection_data.objects;
    }

    template<typename T>
    auto get_active_selection_as() const -> const T&
    {
        return get_active_selection().get_value<T>();
    }

    template<typename T>
    auto try_get_active_selection_as() -> T*
    {
        auto& active = get_active_selection();
        if(active.is_type<T>())
        {
            return &active.get_value<T>();
        }

        return nullptr;
    }

    template<typename T>
    auto try_get_active_selection_as() const -> const T*
    {
        const auto& active = get_active_selection();
        if(active.is_type<T>())
        {
            return &active.get_value<T>();
        }

        return nullptr;
    }

    template<typename T>
    auto try_get_selections_as() const -> std::vector<const T*>
    {
        std::vector<T*> result;

        for(const auto& obj : selection_data.objects)
        {
            if(obj.is_type<T>())
            {
                result.emplace_back(&obj.get_value<T>());
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
            if(obj.is_type<T>())
            {
                result.emplace_back(&obj.get_value<T>());
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
            if(obj.is_type<T>())
            {
                result.emplace_back(obj.get_value<T>());
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
        std::erase_if(selection_data.objects,
                      [&](const auto& el)
                      {
                          return is_selected_impl(entry, el);
                      });
        sanity_check_selection_data();

    }

    template<typename T>
    void select(const T& entry, select_mode mode = select_mode::normal)
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
                    unselect(entry);
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
                    unselect(entry);

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

        // auto& ctx = engine::context();
        // auto& ui_ev = ctx.get_cached<ui_events>();
        // ui_ev.on_selection_changed();
    }

    void add_action(const std::string& name, const untracked_action_t::action_t& action);
    void execute_actions();
    auto has_unsaved_changes() const -> bool { return has_unsaved_changes_; }
    void clear_unsaved_changes() { has_unsaved_changes_ = false; }

    void clear();

    // Prefab editing mode methods
    void enter_prefab_mode(rtti::context& ctx, const asset_handle<prefab>& prefab, bool auto_save = false);
    void exit_prefab_mode(rtti::context& ctx, save_option save_changes = save_option::prompt);
    auto is_prefab_mode() const -> bool { return current_mode == editing_mode::prefab; }
    void save_prefab_changes(rtti::context& ctx);
    
    // Returns the active scene based on the current edit mode
    auto get_active_scene(rtti::context& ctx) -> scene*;

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

    inverse_kinematics ik_data;
    
    // Current editing mode
    editing_mode current_mode = editing_mode::scene;
    
    // Currently edited prefab
    asset_handle<prefab> edited_prefab;
    
    // The entity created from the prefab that we're editing
    entt::handle prefab_entity;
    
    // Separate scene for prefab editing
    scene prefab_scene{"prefab_scene"};


    private:
    template<typename T>
    auto is_selected_impl(const T& entry, const rttr::variant& selected) -> bool
    {
        if(!selected.is_type<T>())
        {
            return false;
        }

        return selected.get_value<T>() == entry;
    }

    void sanity_check_selection_data()
    {
        if(selection_data.objects.empty())
        {
            selection_data = {};
        }
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
    void load_checkpoint(rtti::context& ctx, scene_cache& cache, bool recover_selection = false, bool flatten_prefabs = false);

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    bool waiting_for_compilation_before_play_{};
    editing_actions_t actions_;
    bool has_unsaved_changes_{};

    // Prompts the user to save changes and returns true if changes should be saved
    auto prompt_save_changes(rtti::context& ctx) -> bool;
};
} // namespace unravel
