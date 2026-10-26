#include "gi_adaptive_probes.h"

#include "gi_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_pass_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

namespace unravel
{

namespace
{

/// Mirror of GI_ADAPTIVE_MAX_SAMPLES: the most candidates per uniform probe (4 x 4).
constexpr uint32_t max_adaptive_samples = 16u;
/// Mirror of gi_adaptive_probes.sh's layout: the counter, then per uniform tile its probe count, its probe list and
/// its placement mask (GI_ADAPTIVE_TILE_STRIDE).
constexpr uint32_t adaptive_counter_entries = 1;
constexpr uint32_t adaptive_tile_stride = max_adaptive_samples + 2u;

/// Uniform tiles per 8x8 group of the marking and spawning passes (one thread per candidate) under @p layout.
auto get_group_tiles(const gi_pass::adaptive_probe_layout& layout) -> usize32_t
{
    return {gi_pass::group_edge / layout.samples_x, gi_pass::group_edge / layout.samples_y};
}

} // namespace

void gi_adaptive_probes::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_gi_frame, "u_gi_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_probes, "u_gi_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_view, "u_gi_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gi_adaptive, "u_gi_adaptive", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_gi_depth, "s_gi_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_normal, "s_gi_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_gi_probe_records, "s_gi_probe_records", bgfx::UniformType::Sampler);
}

gi_adaptive_probes::~gi_adaptive_probes()
{
    gi_pass::destroy_handle(state_);
    gi_pass::destroy_handle(args_);
}

auto gi_adaptive_probes::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/gi/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    mark_program_ = load("cs_gi_adaptive_mark");
    spawn_program_ = load("cs_gi_adaptive_spawn");
    args_program_ = load("cs_gi_adaptive_args");
    if(!has_programs())
    {
        APPLOG_WARNING("[GI] Adaptive probe programs failed to load; the screen probe gather cannot run.");
    }
    return has_programs();
}

auto gi_adaptive_probes::has_programs() const -> bool
{
    for(const auto* program : {&mark_program_, &spawn_program_, &args_program_})
    {
        if(!*program || !(*program)->is_valid())
        {
            return false;
        }
    }
    return true;
}

auto gi_adaptive_probes::ensure_resources(uint32_t uniform_tiles) -> bool
{
    // The placement clears the state it uses every frame, so a larger buffer starts empty.
    if(!bgfx::isValid(state_) || uniform_tiles > state_tiles_)
    {
        gi_pass::destroy_handle(state_);
        state_ = gi_pass::make_uint_buffer(adaptive_counter_entries + uniform_tiles * adaptive_tile_stride);
        state_tiles_ = bgfx::isValid(state_) ? uniform_tiles : 0u;
    }
    if(!bgfx::isValid(args_))
    {
        args_ = bgfx::createIndirectBuffer(args_slot_count);
    }
    return bgfx::isValid(state_) && bgfx::isValid(args_);
}

auto gi_adaptive_probes::get_capacity(uint32_t probes_x, uint32_t probes_y) -> uint32_t
{
    return uint32_t(float(probes_x * probes_y) * gi::GI_ADAPTIVE_ALLOCATION_FRACTION);
}

void gi_adaptive_probes::bind_state(uint8_t stage, bgfx::Access::Enum access) const
{
    bgfx::setBuffer(stage, state_, access);
}

void gi_adaptive_probes::dispatch(bgfx::ViewId view, const gpu_program& program, args_slot slot) const
{
    gfx::dispatch_indirect(view, program.native_handle(), args_, uint16_t(slot), 1);
}

void gi_adaptive_probes::set_uniforms(const frame_inputs& inputs) const
{
    gfx::set_uniform(uniforms_.u_gi_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_gi_probes, inputs.probes);
    gfx::set_uniform(uniforms_.u_gi_view, inputs.view);
    const float adaptive[4] = {float(inputs.capacity),
                               float(inputs.border_resolution),
                               float(inputs.layout.samples_x),
                               float(inputs.layout.samples_y)};
    gfx::set_uniform(uniforms_.u_gi_adaptive, adaptive);
}

void gi_adaptive_probes::run_mark(const frame_inputs& inputs) const
{
    const auto& params = *inputs.params;
    gfx::render_pass pass("GI/Adaptive Probes Mark");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    mark_program_->begin();
    gfx::set_texture(uniforms_.s_gi_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_gi_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_gi_probe_records, 2, inputs.probe_records);
    bind_state(3, bgfx::Access::ReadWrite);
    set_uniforms(inputs);
    const auto group_tiles = get_group_tiles(inputs.layout);
    gfx::dispatch(pass.id,
                   mark_program_->native_handle(),
                   gi_pass::divide_round_up(inputs.probes_x, group_tiles.width),
                   gi_pass::divide_round_up(inputs.probes_y, group_tiles.height),
                   1);
    mark_program_->end();
}

void gi_adaptive_probes::run_spawn(const frame_inputs& inputs) const
{
    const auto& params = *inputs.params;
    gfx::render_pass pass("GI/Adaptive Probes Spawn");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    spawn_program_->begin();
    gfx::set_texture(uniforms_.s_gi_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_gi_normal, 1, params.g_buffer->get_texture(1));
    bind_state(2, bgfx::Access::ReadWrite);
    gi_pass::bind_image(3, inputs.probe_records, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    set_uniforms(inputs);
    const auto group_tiles = get_group_tiles(inputs.layout);
    gfx::dispatch(pass.id,
                   spawn_program_->native_handle(),
                   gi_pass::divide_round_up(inputs.probes_x, group_tiles.width),
                   gi_pass::divide_round_up(inputs.probes_y, group_tiles.height),
                   1);
    spawn_program_->end();
}

void gi_adaptive_probes::run_args(const frame_inputs& inputs) const
{
    gfx::render_pass pass("GI/Probe Dispatch Args");
    args_program_->begin();
    bind_state(0, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, args_, bgfx::Access::ReadWrite);
    set_uniforms(inputs);
    gfx::dispatch(pass.id, args_program_->native_handle(), 1, 1, 1);
    args_program_->end();
}

void gi_adaptive_probes::run(const frame_inputs& inputs) const
{
    APP_SCOPE_PERF("Rendering/GI/Adaptive Probes");
    if(inputs.place)
    {
        run_mark(inputs);
        run_spawn(inputs);
    }
    run_args(inputs);
}

} // namespace unravel
