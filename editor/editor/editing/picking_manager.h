#pragma once

#include <base/basetypes.hpp>
#include <engine/frame_times.h>
#include <engine/assets/asset_handle.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/material_graph/graph_material_programs.h>
#include <hpp/optional.hpp>
#include <functional>

#include "editing_manager.h"
#include "scene_raycast.h"

namespace gfx
{
struct frame_buffer;
struct texture;
} // namespace gfx

namespace unravel
{

class picking_manager
{
public:
    // Callback type for custom pick actions
    using pick_callback = std::function<void(entt::handle entity, const math::vec2& screen_pos)>;

    picking_manager();
    ~picking_manager();

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    // Original pick function - maintains backward compatibility
    void request_pick(const camera& cam, editing_manager::select_mode mode, math::vec2 pos, math::vec2 area = {});
    
    // New function for querying without selection
    void query_pick(math::vec2 pos, const camera& cam, pick_callback callback, bool force = false);
    
    // Check if a pick operation is in progress
    auto is_picking() const -> bool;

    void cancel_pick();
    
    constexpr static int tex_id_dim = 1;

    void on_frame_render(rtti::context& ctx, const frame_times& times);

    void on_frame_pick(rtti::context& ctx, delta_t dt);

    auto get_pick_texture() const -> const std::shared_ptr<gfx::texture>&;

private:
    // Common picking setup function
    void setup_pick_camera(const camera& cam, math::vec2 pos, math::vec2 area = {});
    
    // Process picking results and call appropriate handlers
    void process_pick_result(rtti::context& ctx, scene* target_scene, ENTT_ID_TYPE id_key);

    /**
     * @brief The decal drawn where the click hit @p picked, a model, or @p picked itself: a click on a decal selects
     *        it, and a click on a decal already selected passes through to the surface under it. Only a plain click
     *        selection asks (pick_ray_ is set); the surface the click's ray hits first must be @p picked.
     */
    auto pick_decal_over(rtti::context& ctx, scene& scn, entt::handle picked) const -> entt::handle;

    /// surface used to render into
    std::shared_ptr<gfx::frame_buffer> surface_;
    ///
    std::shared_ptr<gfx::texture> blit_tex_;
    /// picking program
    std::unique_ptr<gpu_program> program_;

    std::unique_ptr<gpu_program> program_gizmos_;

    std::unique_ptr<gpu_program> program_skinned_;
    /// The ids of graph materials whose World Position Offset moves their vertices: their own vertex shader with the
    /// id's fragment shader, so a click on the moved surface picks it.
    graph_material_programs moved_vertex_programs_;
    asset_handle<gfx::shader> id_fragment_shader_;
    /// Read blit into this
    std::array<std::uint8_t, tex_id_dim * tex_id_dim * 4> blit_data_;
    /// Indicates if is reading and when it will be ready
    std::uint32_t reading_ = 0;

    bool start_readback_ = false;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    hpp::optional<camera> original_camera_{};
    hpp::optional<camera> pick_camera_{};
    std::vector<entt::handle> picked_entities_{};

    editing_manager::select_mode pick_mode_{};
    
    // Store the original pick position for callbacks
    math::vec2 pick_position_{};
    /// The ray of a single click, through the view at pick_position_; none for an area pick.
    hpp::optional<scene_ray> pick_ray_{};

    math::vec2 pick_area_{};
    
    // Optional callback for custom pick actions
    pick_callback pick_callback_{};
    

};
} // namespace unravel
