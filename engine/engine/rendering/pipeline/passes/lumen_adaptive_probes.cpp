#include "lumen_adaptive_probes.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/gi/lumen_constants.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

namespace unravel
{

namespace
{

constexpr uint32_t adaptive_samples_x = uint32_t(gi::lumen::LUMEN_ADAPTIVE_SAMPLES_X);
constexpr uint32_t adaptive_samples_y = uint32_t(gi::lumen::LUMEN_ADAPTIVE_SAMPLES_Y);
/// Uniform tiles per 8x8 group of the marking and spawning passes (one thread per candidate).
constexpr uint32_t adaptive_group_tiles_x = lumen_pass::group_edge / adaptive_samples_x;
constexpr uint32_t adaptive_group_tiles_y = lumen_pass::group_edge / adaptive_samples_y;
static_assert(lumen_pass::group_edge % adaptive_samples_x == 0u && lumen_pass::group_edge % adaptive_samples_y == 0u,
              "a group holds whole uniform tiles of candidates");
/// Mirror of lumen_adaptive_probes.sh's layout: the counter, then per uniform tile its probe count, its probe list and
/// its placement mask (LUMEN_ADAPTIVE_TILE_STRIDE).
constexpr uint32_t adaptive_counter_entries = 1;
constexpr uint32_t adaptive_tile_stride = adaptive_samples_x * adaptive_samples_y + 2u;

} // namespace

void lumen_adaptive_probes::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_adaptive, "u_lumen_adaptive", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_depth, "s_lumen_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_normal, "s_lumen_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_probe_records, "s_lumen_probe_records", bgfx::UniformType::Sampler);
}

lumen_adaptive_probes::~lumen_adaptive_probes()
{
    lumen_pass::destroy_handle(state_);
    lumen_pass::destroy_handle(args_);
}

auto lumen_adaptive_probes::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    mark_program_ = load("cs_lumen_adaptive_mark");
    spawn_program_ = load("cs_lumen_adaptive_spawn");
    args_program_ = load("cs_lumen_adaptive_args");
    if(!has_programs())
    {
        APPLOG_WARNING("[GI] Adaptive probe programs failed to load; the screen probe gather cannot run.");
    }
    return has_programs();
}

auto lumen_adaptive_probes::has_programs() const -> bool
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

auto lumen_adaptive_probes::ensure_resources(uint32_t uniform_tiles) -> bool
{
    // The placement clears the state it uses every frame, so a larger buffer starts empty.
    if(!bgfx::isValid(state_) || uniform_tiles > state_tiles_)
    {
        lumen_pass::destroy_handle(state_);
        state_ = lumen_pass::make_uint_buffer(adaptive_counter_entries + uniform_tiles * adaptive_tile_stride);
        state_tiles_ = bgfx::isValid(state_) ? uniform_tiles : 0u;
    }
    if(!bgfx::isValid(args_))
    {
        args_ = bgfx::createIndirectBuffer(args_slot_count);
    }
    return bgfx::isValid(state_) && bgfx::isValid(args_);
}

auto lumen_adaptive_probes::get_capacity(uint32_t probes_x, uint32_t probes_y) -> uint32_t
{
    return uint32_t(float(probes_x * probes_y) * gi::lumen::LUMEN_ADAPTIVE_ALLOCATION_FRACTION);
}

void lumen_adaptive_probes::bind_state(uint8_t stage, bgfx::Access::Enum access) const
{
    bgfx::setBuffer(stage, state_, access);
}

void lumen_adaptive_probes::dispatch(bgfx::ViewId view, const gpu_program& program, args_slot slot) const
{
    bgfx::dispatch(view, program.native_handle(), args_, uint16_t(slot), 1);
}

void lumen_adaptive_probes::set_uniforms(const frame_inputs& inputs) const
{
    gfx::set_uniform(uniforms_.u_lumen_frame, inputs.frame);
    gfx::set_uniform(uniforms_.u_lumen_probes, inputs.probes);
    gfx::set_uniform(uniforms_.u_lumen_view, inputs.view);
    const float adaptive[4] = {float(inputs.capacity), float(inputs.border_resolution), 0.0f, 0.0f};
    gfx::set_uniform(uniforms_.u_lumen_adaptive, adaptive);
}

void lumen_adaptive_probes::run_mark(const frame_inputs& inputs) const
{
    const auto& params = *inputs.params;
    gfx::render_pass pass("GI/Adaptive Probes Mark");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    mark_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_probe_records, 2, inputs.probe_records);
    bind_state(3, bgfx::Access::ReadWrite);
    set_uniforms(inputs);
    bgfx::dispatch(pass.id,
                   mark_program_->native_handle(),
                   lumen_pass::divide_round_up(inputs.probes_x, adaptive_group_tiles_x),
                   lumen_pass::divide_round_up(inputs.probes_y, adaptive_group_tiles_y),
                   1);
    mark_program_->end();
}

void lumen_adaptive_probes::run_spawn(const frame_inputs& inputs) const
{
    const auto& params = *inputs.params;
    gfx::render_pass pass("GI/Adaptive Probes Spawn");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    spawn_program_->begin();
    gfx::set_texture(uniforms_.s_lumen_depth, 0, params.g_buffer->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_normal, 1, params.g_buffer->get_texture(1));
    bind_state(2, bgfx::Access::ReadWrite);
    lumen_pass::bind_image(3, inputs.probe_records, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    set_uniforms(inputs);
    bgfx::dispatch(pass.id,
                   spawn_program_->native_handle(),
                   lumen_pass::divide_round_up(inputs.probes_x, adaptive_group_tiles_x),
                   lumen_pass::divide_round_up(inputs.probes_y, adaptive_group_tiles_y),
                   1);
    spawn_program_->end();
}

void lumen_adaptive_probes::run_args(const frame_inputs& inputs) const
{
    gfx::render_pass pass("GI/Probe Dispatch Args");
    args_program_->begin();
    bind_state(0, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, args_, bgfx::Access::ReadWrite);
    set_uniforms(inputs);
    bgfx::dispatch(pass.id, args_program_->native_handle(), 1, 1, 1);
    args_program_->end();
}

void lumen_adaptive_probes::run(const frame_inputs& inputs) const
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
