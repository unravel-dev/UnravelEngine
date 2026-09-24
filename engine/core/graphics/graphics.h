#pragma once

#include "format.h"
#include "vertex_decl.h"
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/bounds.h>
#include <bx/string.h>
#include <cstdint>
#include <functional>
#include <string>
#include <set>

namespace gfx
{
/**/
bool init(bgfx::Init init_data);

/**/
void shutdown();

/// Policy when @ref eviction::reclaim_for reports insufficient headroom before a GPU texture
/// allocation (render targets / compute-write surfaces).
enum class allocation_failure_policy : std::uint8_t
{
    warn_and_try,      ///< Log a warning and attempt the allocation anyway (default).
    skip_with_fallback ///< Skip the allocation; @ref texture::native_handle binds the fallback texture.
};

void set_allocation_failure_policy(allocation_failure_policy policy);
auto get_allocation_failure_policy() -> allocation_failure_policy;

/// Valid magenta 4x4 RGBA8 texture created during @ref init and destroyed in @ref shutdown. Returned
/// from @ref texture::native_handle when the resource has no live GPU handle.
auto fallback_texture() -> bgfx::TextureHandle;

/**/
uint32_t frame(uint8_t _flags = BGFX_FRAME_NONE);

/**
 * Directory for the renderer's binary cache (driver-compiled pipeline state, Vulkan pipeline
 * cache). Set BEFORE init. Empty (the default) disables caching. Without it, backends that
 * compile pipelines at first use (D3D12 especially) re-pay the full driver compilation of
 * every heavy shader on every launch - seconds of a single frame for a large compute chain.
 */
void set_cache_directory(const std::string& directory);

/**
 * Image binding for 3D textures. The plain overload leaves the layer range at "all"
 * (UINT16_MAX), which bgfx's OpenGL backend turns into a NON-layered glBindImageTexture -
 * for a 3D texture that binds a single 2D slice "treated as a different texture target"
 * (GL 4.6 8.26), so every image3D store beyond it is undefined. NVIDIA/AMD bind the whole
 * level anyway and hide it; Mesa follows the spec and drops the stores, which left every
 * GI volume unwritten on Linux/GL. An explicit (0, 1) range flips bgfx to a layered
 * binding of the whole level; on D3D12/Vulkan a 3D texture has exactly one layer, so the
 * same range resolves to the full default view and nothing changes.
 */
void set_image_3d(uint8_t _stage,
                  bgfx::TextureHandle _handle,
                  uint8_t _mip,
                  bgfx::Access::Enum _access,
                  bgfx::TextureFormat::Enum _format = bgfx::TextureFormat::Count);

//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void set_trace_logger(const std::function<void(const std::string&, const char* _filePath, uint16_t _line)>& logger);
void set_info_logger(const std::function<void(const std::string&, const char* _filePath, uint16_t _line)>& logger);
void set_warning_logger(const std::function<void(const std::string&, const char* _filePath, uint16_t _line)>& logger);
void set_error_logger(const std::function<void(const std::string&, const char* _filePath, uint16_t _line)>& logger);
void set_debug_logger(const std::function<void(const std::string&, const char* _filePath, uint16_t _line)>& logger);

/// Emit a message through the logger callback registered for @p category ("trace", "debug",
/// "info", "warning", "error"). No-op when the host has not installed a callback for that
/// category. Used by the graphics layer (e.g. the eviction system) to surface messages through
/// the same channel bgfx uses, so the user only has to wire one logger up to the engine.
void log(const std::string& category,
         const std::string& log_msg,
         const char* _filePath = nullptr,
         uint16_t _line = 0);

/// Hooks for bgfx::CallbackI::profilerBegin / profilerBeginLiteral / profilerEnd. The graphics layer
/// only forwards events; pair nested begin/end and any token stack in the code that installs hooks.
using gfx_profiler_begin_hook =
    std::function<void(const char* name, uint32_t abgr, const char* file_path, uint16_t line)>;
using gfx_profiler_begin_literal_hook = gfx_profiler_begin_hook;
using gfx_profiler_end_hook = std::function<void()>;

void set_profiler_hooks(gfx_profiler_begin_hook on_begin,
                        gfx_profiler_begin_literal_hook on_begin_literal,
                        gfx_profiler_end_hook on_end);
void clear_profiler_hooks();

void frames(int _count, int32_t _flags = BGFX_FRAME_NONE);

bool is_origin_bottom_left();
bool is_homogeneous_depth();
float get_half_texel();
uint32_t get_max_blend_transforms();

uint64_t screen_quad(float dest_width, float dest_height, float depth = 0.0f, float width = 1.0f, float height = 1.0f);
uint64_t clip_quad(float depth = 0.0f, float width = 1.0f, float height = 1.0f);

/// Single large triangle covering the clip rect (same UV convention as @ref clip_quad).
/// Use for fullscreen passes that must not have a quad diagonal (e.g. SSIL half-res trace).
/// Returns 0 if transient VB allocation failed — fall back to @ref clip_quad in that case.
uint64_t clip_fullscreen_triangle(float depth = 0.0f, float width = 1.0f, float height = 1.0f);

struct clip_quad_def
{
    float depth = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float uv_offset_x = 0.0f;
    float uv_offset_y = 0.0f;
    float uv_scaling_x = 1.0f;
    float uv_scaling_y = 1.0f;
};

uint64_t clip_quad_ex(const clip_quad_def& def);

void get_size_from_ratio(bgfx::BackbufferRatio::Enum _ratio, uint16_t& _width, uint16_t& _height);


const std::string& get_renderer_filename_extension(bgfx::RendererType::Enum _type);
const std::string& get_current_renderer_filename_extension();
const std::set<std::string>& get_renderer_platform_supported_filename_extensions();
bgfx::RendererType::Enum get_renderer_based_on_filename_extension(const std::string& _type);

bool is_supported(uint64_t flag);

bool check_avail_transient_buffers(uint32_t _numVertices,
                                   const bgfx::VertexLayout& _layout,
                                   uint32_t _numIndices,
                                   bool _index32 = false);
uint32_t get_render_frame();

void set_world_transform(const void* _mtx, uint16_t _num = 1);

/// Previous-frame counterpart of @ref set_world_transform (uniform @c u_prev_world), consumed by
/// the velocity (motion vector) pass. Same 128-matrix capacity as @c u_world.
void set_prev_world_transform(const void* _mtx, uint16_t _num = 1);

/// Estimate of GPU memory a texture occupies for eviction accounting. Matches bgfx's internal
/// @c textureMemoryUsed baseline (@ref texture_info::storageSize), with row-pitch uplift for
/// uncompressed formats (256-byte alignment) and a 4 KiB minimum for committed image allocations.
/// MSAA sample count from @p _flags is applied for render targets. True device occupancy comes from
/// @ref get_stats (gpuMemoryUsed / gpuMemoryMax); the eviction budget also carries a safety margin.
uint64_t estimate_texture_gpu_size(const bgfx::TextureInfo& _info, uint64_t _flags);


} // namespace gfx
