/*
 * RmlUi BGfx Renderer Interface Implementation
 */

#include "RmlUi_RenderInterface.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/DecorationTypes.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/Types.h>

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/engine.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/render_window.h>
#include <engine/rendering/renderer.h>


#include <filesystem/filesystem.h>
#include <graphics/graphics.h>
#include <logging/logging.h>
#include <string_utils/utils.h>


#include <array>
// Determines the anti-aliasing quality when creating layers. Enables better-looking visuals, especially when transforms
// are applied.
#ifndef RMLUI_NUM_MSAA_SAMPLES
#define RMLUI_NUM_MSAA_SAMPLES 2
#endif

namespace unravel
{

RmlUi_RenderInterface::RmlUi_RenderInterface() : compiled_geometries_()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
}

RmlUi_RenderInterface::~RmlUi_RenderInterface()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);
    cleanup_resources();
}

auto RmlUi_RenderInterface::to_rml_handle(compiled_geometry_handle handle) -> Rml::CompiledGeometryHandle
{
    return static_cast<Rml::CompiledGeometryHandle>(handle.idx + 1); // +1 because RmlUi uses 0 as invalid
}

auto RmlUi_RenderInterface::from_rml_handle(Rml::CompiledGeometryHandle handle) -> compiled_geometry_handle
{
    compiled_geometry_handle result;
    result.idx = (handle > 0) ? static_cast<uint16_t>(handle - 1) : bx::kInvalidHandle;
    return result;
}

auto RmlUi_RenderInterface::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(is_initialized_)
    {
        APPLOG_WARNING("RmlUi BGfx renderer already initialized");
        return true;
    }

    ctx_ = &ctx;

    // Initialize vertex layout for RmlUi vertices
    if(!init_vertex_layout())
    {
        APPLOG_ERROR("Failed to initialize vertex layout");
        return false;
    }

    // Initialize shaders
    if(!init_shaders())
    {
        APPLOG_ERROR("Failed to initialize shaders");
        cleanup_resources();
        return false;
    }

    // Set up projection matrix (will be updated in set_viewport)
    projection_ = Rml::Matrix4f::Identity();
    transform_ = Rml::Matrix4f::Identity();

    // Initialize texture storage
    compiled_textures_.reserve(128);


    // Create fullscreen quad geometry
    Rml::MeshUtilities::GenerateQuad(mesh_fullscreen_quad_, Rml::Vector2f(-1), Rml::Vector2f(2), {});
    fullscreen_quad_geometry_ = CompileGeometry(mesh_fullscreen_quad_.vertices, mesh_fullscreen_quad_.indices);


    is_initialized_ = true;
    APPLOG_INFO("RmlUi BGfx renderer initialized successfully");
    return true;
}

void RmlUi_RenderInterface::shutdown()
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    if(!is_initialized_)
    {
        return;
    }

    cleanup_resources();
    is_initialized_ = false;
    ctx_ = nullptr;

    APPLOG_INFO("RmlUi BGfx renderer shutdown complete");
}

void RmlUi_RenderInterface::set_viewport(int viewport_width,
                                         int viewport_height,
                                         int viewport_offset_x,
                                         int viewport_offset_y)
{
    viewport_width_ = Rml::Math::Max(viewport_width, 1);
    viewport_height_ = Rml::Math::Max(viewport_height, 1);
    viewport_offset_x_ = viewport_offset_x;
    viewport_offset_y_ = viewport_offset_y;

    // Set up orthographic projection matrix for UI rendering
    // RmlUi uses screen coordinates with origin at top-left
    // Match the GL3 implementation exactly
    projection_ = Rml::Matrix4f::ProjectOrtho(0.0f,
                                              static_cast<float>(viewport_width_),
                                              static_cast<float>(viewport_height_),
                                              0.0f,
                                              -10000,
                                              10000);

    // https://matthewwellings.com/blog/the-new-vulkan-coordinate-system/
    Rml::Matrix4f correction_matrix;
    correction_matrix.SetColumns(Rml::Vector4f(1.0f, 0.0f, 0.0f, 0.0f),
                                 Rml::Vector4f(0.0f, -1.0f, 0.0f, 0.0f),
                                 Rml::Vector4f(0.0f, 0.0f, 0.5f, 0.0f),
                                 Rml::Vector4f(0.0f, 0.0f, 0.5f, 1.0f));

    projection_ = correction_matrix * projection_;

    // Mark all programs as needing transform update
    program_transform_dirty_.set();

    // APPLOG_TRACE("Viewport set to {}x{} at ({}, {})", viewport_width, viewport_height, viewport_offset_x,
    // viewport_offset_y);
}

void RmlUi_RenderInterface::begin_frame()
{
    if(!is_initialized_)
    {
        return;
    }

    // Increment frame counter for LRU tracking
    current_frame_++;

    // Initialize transform to projection matrix (matching GL3 implementation)
    SetTransform(nullptr);

    // Reset scissor and clip mask state
    scissor_state_ = Rml::Rectanglei::MakeInvalid();
    scissor_enabled_ = false;
    clip_mask_enabled_ = false;
    stencil_test_ref_ = 1;

    // Initialize the render layer stack for this frame
    render_layers_.begin_frame(viewport_width_, viewport_height_);
}

void RmlUi_RenderInterface::end_frame(const gfx::frame_buffer::ptr& framebuffer)
{
    if(!is_initialized_)
    {
        return;
    }

    if(!framebuffer)
    {
        // End the frame (pops the layer we pushed in begin_frame)
        render_layers_.end_frame();
        return;
    }

    // Follow GL implementation pattern:
    // 1. Blit top layer to postprocess primary
    // 2. Fullscreen passthrough to main surface
    // 3. End the frame (which pops the layer)

    const LayerFramebuffer& top_layer = render_layers_.get_top_layer();
    const LayerFramebuffer& postprocess_primary = render_layers_.get_postprocess_primary();

    // Blit top layer to postprocess primary (resolve MSAA)
    auto source_texture = top_layer.get_color_texture();
    // auto dest_texture = postprocess_primary.get_color_texture();

    // if(source_texture && dest_texture && source_texture->is_valid() && dest_texture->is_valid())
    // {
    //     gfx::render_pass blit_pass("rmlui_blit_to_postprocess");
    //     // Set identity view and projection for filter rendering
    //     auto view = Rml::Matrix4f::Identity();
    //     auto proj = Rml::Matrix4f::Identity();
    //     blit_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
    //     auto size = top_layer.get_size();
    //     gfx::blit(blit_pass.id,
    //               dest_texture->native_handle(),
    //               0,
    //               0,
    //               source_texture->native_handle(),
    //               0,
    //               0,
    //               static_cast<uint16_t>(size.width),
    //               static_cast<uint16_t>(size.height));
    // }

    // Bind main surface and do fullscreen passthrough
    // auto& rend = ctx_->get_cached<renderer>();
    // const auto& main_window = rend.get_main_window();
    // const auto& main_surface = main_window->get_surface();

    gfx::render_pass main_pass("rmlui_main_surface_pass");

    main_pass.bind(framebuffer.get());

    // Set up identity view and projection for fullscreen quad
    // NOTE: We don't apply the coordinate correction matrix here because the layer content
    // is already rendered in the corrected coordinate system, so we just need to present it as-is
    auto view = Rml::Matrix4f::Identity();
    auto proj = Rml::Matrix4f::Identity();
    main_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

    // Use passthrough shader to render postprocess primary to main surface
    use_program(RmlUi_ProgramId::Passthrough);
    auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Passthrough)];

    if(render_program.begin())
    {
        // Bind the postprocessed texture
        auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
        if(bgfx::isValid(tex_uniform) && source_texture && source_texture->is_valid())
        {
            gfx::set_texture(0, tex_uniform, source_texture->native_handle());
        }

        // Set blend state for premultiplied alpha (like GL implementation)
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
        gfx::set_state(state);

        draw_fullscreen_quad();

        // Submit fullscreen quad
        gfx::submit(main_pass.id, render_program.native_handle());
        render_program.end();
    }

    // End the frame (pops the layer we pushed in begin_frame)
    render_layers_.end_frame();
}

void RmlUi_RenderInterface::clear()
{
    // Clear is typically handled by the main renderer
    // This is a no-op for now
}

// -- Inherited from Rml::RenderInterface --

auto RmlUi_RenderInterface::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                            Rml::Span<const int> indices) -> Rml::CompiledGeometryHandle
{
    // APPLOG_TRACE("CompileGeometry: {} vertices, {} indices", vertices.size(), indices.size());

    if(vertices.empty() || indices.empty())
    {
        return 0; // Invalid handle
    }

    // Allocate a handle from the pool
    uint16_t geometry_idx = geometry_handles_.alloc();
    if(geometry_idx == bx::kInvalidHandle)
    {
        APPLOG_ERROR("Failed to allocate geometry handle - pool exhausted");
        return 0;
    }

    APP_SCOPE_PERF("UI/RmlUi/CompileGeometry");

    CompiledGeometry& geometry = compiled_geometries_[geometry_idx];
    // geometry = CompiledGeometry{}; // Reset to default state
    geometry.num_vertices = static_cast<uint32_t>(vertices.size());
    geometry.num_indices = static_cast<uint32_t>(indices.size());

    // Classify geometry based on size
    geometry.buffer_type = classify_geometry(geometry.num_vertices, geometry.num_indices);
    geometry.vertices = vertices;
    geometry.indices = indices;

    // Handle transient buffers (smallest geometries)
    if(geometry.buffer_type == GeometryBufferType::Transient)
    {
        APP_SCOPE_PERF("UI/RmlUi/CompileGeometry/Memcopy");

        // Convert internal handle to RmlUi handle
        compiled_geometry_handle internal_handle;
        internal_handle.idx = geometry_idx;
        Rml::CompiledGeometryHandle rml_handle = to_rml_handle(internal_handle);

        // APPLOG_TRACE("Compiled geometry handle: {} (internal: {}) using transient buffers", rml_handle,
        // geometry_idx);
        return rml_handle;
    }

    // Fall back to creating individual buffers for large geometries or when pool is full
    // Create vertex buffer
    // RmlUi vertices have: position(2 floats), colour(4 bytes), tex_coord(2 floats)
    {
        auto vertices_size = vertex_layout_.getSize(vertices.size());
        // Use gfx::copy() to ensure data lifetime - bgfx will manage the memory
        const gfx::memory_view* mem = gfx::copy(vertices.data(), vertices_size);

        geometry.static_vertex_buffer = gfx::create_vertex_buffer(mem, vertex_layout_);
    }

    // Create index buffer
    {
        auto indices_size = static_cast<uint32_t>(indices.size() * sizeof(int));
        // Use gfx::copy() to ensure data lifetime - bgfx will manage the memory
        const gfx::memory_view* mem = gfx::copy(indices.data(), indices_size);
        geometry.static_index_buffer = gfx::create_index_buffer(mem, BGFX_BUFFER_INDEX32);
    }
    // Verify buffers are valid
    if(!geometry.is_valid())
    {
        APPLOG_ERROR("Failed to create vertex or index buffer for RmlUi geometry");

        // Clean up and free the handle
        geometry.destroy_buffers();
        geometry_handles_.free(geometry_idx);
        return 0;
    }

    // Convert internal handle to RmlUi handle
    compiled_geometry_handle internal_handle;
    internal_handle.idx = geometry_idx;
    Rml::CompiledGeometryHandle rml_handle = to_rml_handle(internal_handle);

    // APPLOG_TRACE("Compiled geometry handle: {} (internal: {}) using static buffers", rml_handle, geometry_idx);
    return rml_handle;
}

void RmlUi_RenderInterface::RenderGeometry(Rml::CompiledGeometryHandle handle,
                                           Rml::Vector2f translation,
                                           Rml::TextureHandle texture)
{
    // if (texture != 0)
    // {
    //     APPLOG_TRACE("RenderGeometry: handle={}, translation=({}, {}), texture={}",
    //                  handle, translation.x, translation.y, texture);
    // }

    if(handle == 0)
    {
        APPLOG_ERROR("Invalid geometry handle: {}", handle);
        return;
    }

    APP_SCOPE_PERF("UI/RmlUi/RenderGeometry");

    // Convert RmlUi handle to internal handle
    compiled_geometry_handle internal_handle = from_rml_handle(handle);
    if(!geometry_handles_.isValid(internal_handle.idx))
    {
        APPLOG_ERROR("Invalid or released geometry handle: {}", handle);
        return;
    }

    // Get geometry using internal handle
    const auto& geometry = compiled_geometries_[internal_handle.idx];

    RmlUi_ProgramId program_id = RmlUi_ProgramId::Count;
    if(texture == TexturePostprocess)
    {
        // Do nothing.
    }
    else if(texture != 0)
    {
        program_id = RmlUi_ProgramId::Texture;
    }
    else
    {
        program_id = RmlUi_ProgramId::Color;
    }

    if(program_id == RmlUi_ProgramId::Count)
    {
        // Set vertex and index buffers
        geometry.bind_buffers(vertex_layout_);

        return;
    }

    // Use the appropriate program
    use_program(program_id);

    auto render_program = programs_[static_cast<size_t>(program_id)];
    if(render_program.begin())
    {
        // Ensure we have the correct layer bound before submitting
        // If we have layers, render to the top layer, otherwise main framebuffer
        auto pass_id = get_layer_pass_id();

        // Set vertex and index buffers
        geometry.bind_buffers(vertex_layout_);

        // Submit transform uniforms
        submit_transform_uniform(translation);

        // Apply scissor if enabled (BGfx requires per-draw-call scissor setting)
        set_scissor();

        // Set texture if provided
        if(texture != 0 && texture <= compiled_textures_.size())
        {
            const auto& tex = compiled_textures_[texture - 1];
            auto texture_uniform = get_uniform_handle(RmlUi_UniformId::Tex);

            if(tex.asset_handle.is_valid())
            {
                gfx::set_texture(0, texture_uniform, tex.asset_handle.get()->native_handle());
            }
            else if(bgfx::isValid(tex.generated_texture_handle))
            {
                gfx::set_texture(0, texture_uniform, tex.generated_texture_handle);
            }
            else
            {
                APPLOG_ERROR("Invalid texture uniform or handle: uniform={}, texture={}",
                             bgfx::isValid(texture_uniform),
                             bgfx::isValid(tex.generated_texture_handle));
            }
        }

        // Set up bgfx state for UI rendering
        // Enable alpha blending for UI elements
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

        // Add stencil test if clip mask is enabled
        if(clip_mask_enabled_)
        {
            state |= BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(stencil_test_ref_) |
                     BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP |
                     BGFX_STENCIL_OP_PASS_Z_KEEP;
        }

        if(clip_mask_enabled_)
        {
            gfx::set_state(state, stencil_test_ref_);
        }
        else
        {
            gfx::set_state(state);
        }

        // Submit draw call
        gfx::submit(pass_id, render_program.native_handle());

        render_program.end();
    }
}

void RmlUi_RenderInterface::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
    // APPLOG_TRACE("ReleaseGeometry: handle={}", handle);
    APP_SCOPE_PERF("UI/RmlUi/ReleaseGeometry");

    if(handle == 0)
    {
        APPLOG_ERROR("Invalid geometry handle: {}", handle);
        return;
    }

    // Convert RmlUi handle to internal handle
    compiled_geometry_handle internal_handle = from_rml_handle(handle);
    if(!geometry_handles_.isValid(internal_handle.idx))
    {
        APPLOG_ERROR("Invalid or already released geometry handle: {}", handle);
        return;
    }

    // Get geometry using internal handle
    auto& geometry = compiled_geometries_[internal_handle.idx];

    // Destroy/clear buffers
    geometry.destroy_buffers();

    // Clear the geometry entry and free the handle for reuse
    // geometry = CompiledGeometry{};
    geometry_handles_.free(internal_handle.idx);
}

auto RmlUi_RenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions,
                                        const Rml::String& source) -> Rml::TextureHandle
{
    APPLOG_TRACE("LoadTexture: source={}", source);

    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto texture = am.get_asset<gfx::texture>(source);
    if(!texture.is_valid())
    {
        APPLOG_ERROR("Failed to load texture: {}", source);
        return 0;
    }

    auto tex = texture.get();
    if(!tex)
    {
        APPLOG_ERROR("Failed to load texture: {}", source);
        return 0;
    }

    texture_dimensions = {tex->info.width, tex->info.height};

    CompiledTexture compiled_texture{};
    compiled_texture.asset_handle = texture;
    compiled_textures_.push_back(compiled_texture);

    return compiled_textures_.size();
}

auto RmlUi_RenderInterface::GenerateTexture(Rml::Span<const Rml::byte> source_data,
                                            Rml::Vector2i source_dimensions) -> Rml::TextureHandle
{
    APPLOG_TRACE("GenerateTexture: {}x{} pixels, {} bytes",
                 source_dimensions.x,
                 source_dimensions.y,
                 source_data.size());

    if(source_data.empty() || source_dimensions.x <= 0 || source_dimensions.y <= 0)
    {
        APPLOG_ERROR("Invalid texture data or dimensions");
        return 0;
    }

    // Validate that we have the expected RGBA8 data size
    const size_t expected_size = static_cast<size_t>(source_dimensions.x * source_dimensions.y * 4);
    if(source_data.size() != expected_size)
    {
        APPLOG_ERROR("Texture data size mismatch: expected {} bytes, got {} bytes", expected_size, source_data.size());
        return 0;
    }

    CompiledTexture texture{};

    // Use gfx::copy() instead of make_ref() because RmlUi's texture data has limited lifetime
    // bgfx::copy() creates an internal copy that bgfx owns and automatically releases
    const gfx::memory_view* mem = gfx::copy(source_data.data(), source_data.size());

    // Create bgfx texture from raw RGBA data
    // RmlUi provides RGBA8 data with premultiplied alpha
    // Use linear filtering for smooth text rendering
    texture.generated_texture_handle =
        gfx::create_texture_2d(static_cast<uint16_t>(source_dimensions.x),
                               static_cast<uint16_t>(source_dimensions.y),
                               false, // no mips
                               1,     // num layers
                               gfx::texture_format::RGBA8,
                               BGFX_TEXTURE_NONE, // Use default linear filtering for smooth text
                               mem);

    if(!bgfx::isValid(texture.generated_texture_handle))
    {
        APPLOG_ERROR("Failed to create bgfx texture");
        return 0;
    }

    // Store texture and return handle (index + 1, since 0 is invalid)
    compiled_textures_.push_back(texture);
    Rml::TextureHandle handle = compiled_textures_.size();

    APPLOG_TRACE("Generated texture handle: {} ({}x{} RGBA8)", handle, source_dimensions.x, source_dimensions.y);
    return handle;
}

void RmlUi_RenderInterface::ReleaseTexture(Rml::TextureHandle texture_handle)
{
    APPLOG_TRACE("ReleaseTexture: handle={}", texture_handle);

    if(texture_handle == 0 || texture_handle > compiled_textures_.size())
    {
        APPLOG_ERROR("Invalid texture handle: {}", texture_handle);
        return;
    }

    // Get texture (handle is 1-based)
    auto& texture = compiled_textures_[texture_handle - 1];

    // Destroy texture
    if(bgfx::isValid(texture.generated_texture_handle))
    {
        gfx::destroy(texture.generated_texture_handle);
    }

    // Clear the texture entry (but don't remove from vector to keep handles valid)
    texture = CompiledTexture{};
}

void RmlUi_RenderInterface::EnableScissorRegion(bool enable)
{
    if(!enable)
    {
        SetScissor(Rml::Rectanglei::MakeInvalid(), false);
    }
    // Note: When enable is true, we assume SetScissorRegion() will be called immediately after
}

static auto vertically_flipped(Rml::Rectanglei rect, int viewport_height) -> Rml::Rectanglei
{
    RMLUI_ASSERT(rect.Valid());
    Rml::Rectanglei flipped_rect = rect;
    flipped_rect.p0.y = viewport_height - rect.p1.y;
    flipped_rect.p1.y = viewport_height - rect.p0.y;
    return flipped_rect;
}

void RmlUi_RenderInterface::SetScissor(Rml::Rectanglei region, bool vertically_flip)
{
    scissor_state_ = region;
    scissor_enabled_ = region.Valid();
}

void RmlUi_RenderInterface::SetScissorRegion(Rml::Rectanglei region)
{
    SetScissor(region);
}

void RmlUi_RenderInterface::EnableClipMask(bool enable)
{
    clip_mask_enabled_ = enable;

    // BGfx handles stencil test through render state flags
    // We'll set the appropriate stencil state when rendering geometry
}

void RmlUi_RenderInterface::RenderToClipMask(Rml::ClipMaskOperation mask_operation,
                                             Rml::CompiledGeometryHandle geometry,
                                             Rml::Vector2f translation)
{
    if(geometry == 0)
    {
        APPLOG_ERROR("Invalid geometry handle: {}", geometry);
        return;
    }

    // Convert RmlUi handle to internal handle
    compiled_geometry_handle internal_handle = from_rml_handle(geometry);
    if(!geometry_handles_.isValid(internal_handle.idx))
    {
        APPLOG_ERROR("Invalid or released geometry handle: {}", geometry);
        return;
    }

    using Rml::ClipMaskOperation;

    // Handle stencil buffer clearing for Set and SetInverse operations (like GL3)
    if(mask_operation == ClipMaskOperation::Set)
    {
        // Clear stencil buffer to 0 (like GL3 does with glClear(GL_STENCIL_BUFFER_BIT))
        // BGfx doesn't have a direct equivalent, but we can clear by rendering a fullscreen quad
        clear_stencil_buffer(0);
    }
    else if(mask_operation == ClipMaskOperation::SetInverse)
    {
        // Clear stencil buffer to 1 (like GL3 does with glClearStencil(1) + glClear())
        clear_stencil_buffer(1);
    }

    // Temporarily disable clip mask to avoid recursive clipping during mask rendering
    bool prev_clip_mask_enabled = clip_mask_enabled_;
    clip_mask_enabled_ = false;

    // Configure stencil operation based on mask operation
    uint64_t stencil_state = 0;
    uint32_t stencil_ref = 1;

    switch(mask_operation)
    {
        case ClipMaskOperation::Set:
        {
            // Write 1 to stencil where geometry is rendered
            stencil_state = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff) |
                            BGFX_STENCIL_OP_FAIL_S_REPLACE | BGFX_STENCIL_OP_FAIL_Z_REPLACE |
                            BGFX_STENCIL_OP_PASS_Z_REPLACE;
            stencil_ref = 1;
        }
        break;

        case ClipMaskOperation::SetInverse:
        {
            // Write 0 where geometry exists (background was cleared to 1)
            stencil_state = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(0) | BGFX_STENCIL_FUNC_RMASK(0xff) |
                            BGFX_STENCIL_OP_FAIL_S_REPLACE | BGFX_STENCIL_OP_FAIL_Z_REPLACE |
                            BGFX_STENCIL_OP_PASS_Z_REPLACE;
            stencil_ref = 0;
        }
        break;

        case ClipMaskOperation::Intersect:
        {
            // Increment stencil where geometry intersects existing mask
            stencil_state = BGFX_STENCIL_TEST_EQUAL | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff) |
                            BGFX_STENCIL_OP_FAIL_S_KEEP | BGFX_STENCIL_OP_FAIL_Z_KEEP | BGFX_STENCIL_OP_PASS_Z_INCR;
            stencil_ref = 1;
        }
        break;
    }

    // Render the geometry to stencil buffer with color writes disabled
    const auto& geom = compiled_geometries_[internal_handle.idx];

    use_program(RmlUi_ProgramId::Color);
    auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Color)];

    if(render_program.begin())
    {
        // Ensure we have the correct layer bound before submitting
        // If we have layers, render to the top layer, otherwise main framebuffer
        auto pass_id = get_layer_pass_id();

        // Set vertex and index buffers
        geom.bind_buffers(vertex_layout_);

        // Submit transform uniforms
        submit_transform_uniform(translation);

        // Apply scissor if enabled (BGfx requires per-draw-call scissor setting)
        set_scissor();

        // Set render state for stencil writing (disable color writes)
        uint64_t render_state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | stencil_state;

        gfx::set_state(render_state, stencil_ref);

        // Submit the draw call
        gfx::submit(pass_id, render_program.native_handle());
        render_program.end();
    }

    // Restore clip mask state
    clip_mask_enabled_ = prev_clip_mask_enabled;

    // Update stencil test reference for subsequent rendering
    // For Intersect operations, we need to test against the incremented value
    if(mask_operation == ClipMaskOperation::Intersect)
    {
        stencil_test_ref_ = 2; // Test against incremented value
    }
    else
    {
        stencil_test_ref_ = 1; // Test against standard mask value
    }
}

void RmlUi_RenderInterface::SetTransform(const Rml::Matrix4f* new_transform)
{
    // Match GL3 implementation: combine projection with transform
    transform_ = (new_transform ? (projection_ * (*new_transform)) : projection_);

    // Mark all programs as needing transform update
    program_transform_dirty_.set();
}

// Layer management - stub implementations for now
auto RmlUi_RenderInterface::PushLayer() -> Rml::LayerHandle
{
    return render_layers_.push_layer();
}

void RmlUi_RenderInterface::CompositeLayers(Rml::LayerHandle source,
                                            Rml::LayerHandle destination,
                                            Rml::BlendMode blend_mode,
                                            Rml::Span<const Rml::CompiledFilterHandle> filters)
{
    // Blit source layer to postprocessing primary buffer
    blit_layer_to_postprocess_primary(source);

    // Apply filters to postprocessing buffer
    render_filters(filters);

    // Composite result to destination layer with blend mode
    composite_to_destination_layer(destination, blend_mode);
}

void RmlUi_RenderInterface::PopLayer()
{
    render_layers_.pop_layer();
}

auto RmlUi_RenderInterface::SaveLayerAsTexture() -> Rml::TextureHandle
{
    // TODO: Implement layer to texture saving
    return 0;
}

auto RmlUi_RenderInterface::SaveLayerAsMaskImage() -> Rml::CompiledFilterHandle
{
    // TODO: Implement layer to mask saving
    return 0;
}

// Filter and shader management
auto RmlUi_RenderInterface::CompileFilter(const Rml::String& name,
                                          const Rml::Dictionary& parameters) -> Rml::CompiledFilterHandle
{
    CompiledFilter filter = {};

    if(name == "opacity")
    {
        filter.type = FilterType::Passthrough;
        filter.blend_factor = Rml::Get(parameters, "value", 1.0f);
    }
    else if(name == "blur")
    {
        filter.type = FilterType::Blur;
        filter.sigma = Rml::Get(parameters, "sigma", 1.0f);
    }
    else if(name == "drop-shadow")
    {
        filter.type = FilterType::DropShadow;
        filter.sigma = Rml::Get(parameters, "sigma", 0.f);
        filter.color = Rml::Get(parameters, "color", Rml::Colourb()).ToPremultiplied();
        filter.offset = Rml::Get(parameters, "offset", Rml::Vector2f(0.f));
    }
    else if(name == "brightness")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
    }
    else if(name == "contrast")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float grayness = 0.5f - 0.5f * value;
        filter.color_matrix = Rml::Matrix4f::Diag(value, value, value, 1.f);
        filter.color_matrix.SetColumn(3, Rml::Vector4f(grayness, grayness, grayness, 1.f));
    }
    else if(name == "invert")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Math::Clamp(Rml::Get(parameters, "value", 1.0f), 0.f, 1.f);
        const float inverted = 1.f - 2.f * value;
        filter.color_matrix = Rml::Matrix4f::Diag(inverted, inverted, inverted, 1.f);
        filter.color_matrix.SetColumn(3, Rml::Vector4f(value, value, value, 1.f));
    }
    else if(name == "grayscale")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float rev_value = 1.f - value;
        const Rml::Vector3f gray = value * Rml::Vector3f(0.2126f, 0.7152f, 0.0722f);
        filter.color_matrix = Rml::Matrix4f::FromRows({gray.x + rev_value, gray.y, gray.z, 0.f},
                                                      {gray.x, gray.y + rev_value, gray.z, 0.f},
                                                      {gray.x, gray.y, gray.z + rev_value, 0.f},
                                                      {0.f, 0.f, 0.f, 1.f});
    }
    else if(name == "sepia")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float rev_value = 1.f - value;
        const Rml::Vector3f r_mix = value * Rml::Vector3f(0.393f, 0.769f, 0.189f);
        const Rml::Vector3f g_mix = value * Rml::Vector3f(0.349f, 0.686f, 0.168f);
        const Rml::Vector3f b_mix = value * Rml::Vector3f(0.272f, 0.534f, 0.131f);
        filter.color_matrix = Rml::Matrix4f::FromRows({r_mix.x + rev_value, r_mix.y, r_mix.z, 0.f},
                                                      {g_mix.x, g_mix.y + rev_value, g_mix.z, 0.f},
                                                      {b_mix.x, b_mix.y, b_mix.z + rev_value, 0.f},
                                                      {0.f, 0.f, 0.f, 1.f});
    }
    else if(name == "hue-rotate")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        const float s = Rml::Math::Sin(value);
        const float c = Rml::Math::Cos(value);
        filter.color_matrix = Rml::Matrix4f::FromRows(
            {0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0.f},
            {0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0.f},
            {0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0.f},
            {0.f, 0.f, 0.f, 1.f});
    }
    else if(name == "saturate")
    {
        filter.type = FilterType::ColorMatrix;
        const float value = Rml::Get(parameters, "value", 1.0f);
        filter.color_matrix =
            Rml::Matrix4f::FromRows({0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0.f},
                                    {0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0.f},
                                    {0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0.f},
                                    {0.f, 0.f, 0.f, 1.f});
    }
    else
    {
        APPLOG_WARNING("Unsupported filter type '{}'", name);
        return 0;
    }

    if(filter.type != FilterType::Invalid)
    {
        compiled_filters_.push_back(filter);
        return compiled_filters_.size(); // 0-based handle
    }

    return 0;
}

void RmlUi_RenderInterface::ReleaseFilter(Rml::CompiledFilterHandle filter)
{
    if(filter == 0 || filter > compiled_filters_.size())
    {
        APPLOG_ERROR("Invalid filter handle: {}", filter);
        return;
    }

    // Clear the filter entry (but don't remove from vector to keep handles valid)
    compiled_filters_[filter - 1] = CompiledFilter{};
}

auto RmlUi_RenderInterface::CompileShader(const Rml::String& name,
                                          const Rml::Dictionary& parameters) -> Rml::CompiledShaderHandle
{
    auto apply_color_stop_list = [](CompiledShader& shader, const Rml::Dictionary& shader_parameters)
    {
        auto it = shader_parameters.find("color_stop_list");
        RMLUI_ASSERT(it != shader_parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST);
        const Rml::ColorStopList& color_stop_list = it->second.GetReference<Rml::ColorStopList>();
        const int num_stops = Rml::Math::Min<int>((int)color_stop_list.size(), 16); // MAX_NUM_STOPS

        shader.stop_positions.resize(num_stops);
        shader.stop_colors.resize(num_stops);
        for(int i = 0; i < num_stops; i++)
        {
            const Rml::ColorStop& stop = color_stop_list[i];
            RMLUI_ASSERT(stop.position.unit == Rml::Unit::NUMBER);
            shader.stop_positions[i] = stop.position.number;

            // Convert to premultiplied float color
            Rml::Colourf color;
            for(int j = 0; j < 4; j++)
            {
                color[j] = (1.f / 255.f) * float(stop.color[j]);
            }
            shader.stop_colors[i] = color;
        }
    };

    CompiledShader shader = {};

    if(name == "linear-gradient")
    {
        shader.type = CompiledShaderType::Gradient;
        const bool repeating = Rml::Get(parameters, "repeating", false);
        shader.gradient_function =
            (repeating ? ShaderGradientFunction::RepeatingLinear : ShaderGradientFunction::Linear);
        shader.p = Rml::Get(parameters, "p0", Rml::Vector2f(0.f));
        shader.v = Rml::Get(parameters, "p1", Rml::Vector2f(0.f)) - shader.p;
        apply_color_stop_list(shader, parameters);
    }
    else if(name == "radial-gradient")
    {
        shader.type = CompiledShaderType::Gradient;
        const bool repeating = Rml::Get(parameters, "repeating", false);
        shader.gradient_function =
            (repeating ? ShaderGradientFunction::RepeatingRadial : ShaderGradientFunction::Radial);
        shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
        shader.v = Rml::Vector2f(1.f) / Rml::Get(parameters, "radius", Rml::Vector2f(1.f));
        apply_color_stop_list(shader, parameters);
    }
    else if(name == "conic-gradient")
    {
        shader.type = CompiledShaderType::Gradient;
        const bool repeating = Rml::Get(parameters, "repeating", false);
        shader.gradient_function = (repeating ? ShaderGradientFunction::RepeatingConic : ShaderGradientFunction::Conic);
        shader.p = Rml::Get(parameters, "center", Rml::Vector2f(0.f));
        const float angle = Rml::Get(parameters, "angle", 0.f);
        shader.v = {Rml::Math::Cos(angle), Rml::Math::Sin(angle)};
        apply_color_stop_list(shader, parameters);
    }
    else if(name == "shader")
    {
        const Rml::String value = Rml::Get(parameters, "value", Rml::String());
        if(value == "creation")
        {
            shader.type = CompiledShaderType::Creation;
            shader.dimensions = Rml::Get(parameters, "dimensions", Rml::Vector2f(0.f));
        }
    }
    else
    {
        APPLOG_WARNING("Unsupported shader type '{}'", name);
        return 0;
    }

    if(shader.type != CompiledShaderType::Invalid)
    {
        compiled_shaders_.push_back(shader);
        return compiled_shaders_.size() - 1; // 0-based handle
    }

    return 0;
}

void RmlUi_RenderInterface::RenderShader(Rml::CompiledShaderHandle shader_handle,
                                         Rml::CompiledGeometryHandle geometry_handle,
                                         Rml::Vector2f translation,
                                         Rml::TextureHandle texture)
{
    if(shader_handle > compiled_shaders_.size() || geometry_handle == 0)
    {
        APPLOG_ERROR("Invalid shader or geometry handle: shader={}, geometry={}", shader_handle, geometry_handle);
        return;
    }

    // Convert RmlUi geometry handle to internal handle
    compiled_geometry_handle internal_handle = from_rml_handle(geometry_handle);
    if(!geometry_handles_.isValid(internal_handle.idx))
    {
        APPLOG_ERROR("Invalid or released geometry handle: {}", geometry_handle);
        return;
    }

    const CompiledShader& shader = compiled_shaders_[shader_handle];
    const CompiledGeometry& geometry = compiled_geometries_[internal_handle.idx];

    switch(shader.type)
    {
        case CompiledShaderType::Gradient:
        {
            RMLUI_ASSERT(shader.stop_positions.size() == shader.stop_colors.size());
            const int num_stops = (int)shader.stop_positions.size();

            use_program(RmlUi_ProgramId::Gradient);
            auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Gradient)];

            if(render_program.begin())
            {
                // Ensure we have the correct layer bound before submitting
                // If we have layers, render to the top layer, otherwise main framebuffer
                auto pass_id = get_layer_pass_id();

                // Set vertex and index buffers
                geometry.bind_buffers(vertex_layout_);

                // Set gradient uniforms
                auto func_uniform = get_uniform_handle(RmlUi_UniformId::Func);
                auto p_uniform = get_uniform_handle(RmlUi_UniformId::P);
                auto v_uniform = get_uniform_handle(RmlUi_UniformId::V);
                auto num_stops_uniform = get_uniform_handle(RmlUi_UniformId::NumStops);
                auto stop_positions_uniform = get_uniform_handle(RmlUi_UniformId::StopPositions);
                auto stop_colors_uniform = get_uniform_handle(RmlUi_UniformId::StopColors);

                if(bgfx::isValid(func_uniform))
                {
                    std::array<float, 4> func_data = {static_cast<float>(shader.gradient_function), 0.0f, 0.0f, 0.0f};
                    gfx::set_uniform(func_uniform, func_data.data());
                }

                if(bgfx::isValid(p_uniform))
                {
                    std::array<float, 4> p_data = {shader.p.x, shader.p.y, 0.0f, 0.0f};
                    gfx::set_uniform(p_uniform, p_data.data());
                }

                if(bgfx::isValid(v_uniform))
                {
                    std::array<float, 4> v_data = {shader.v.x, shader.v.y, 0.0f, 0.0f};
                    gfx::set_uniform(v_uniform, v_data.data());
                }

                if(bgfx::isValid(num_stops_uniform))
                {
                    std::array<float, 4> num_stops_data = {static_cast<float>(num_stops), 0.0f, 0.0f, 0.0f};
                    gfx::set_uniform(num_stops_uniform, num_stops_data.data());
                }

                if(bgfx::isValid(stop_positions_uniform) && !shader.stop_positions.empty())
                {
                    gfx::set_uniform(stop_positions_uniform, shader.stop_positions.data(), num_stops);
                }

                if(bgfx::isValid(stop_colors_uniform) && !shader.stop_colors.empty())
                {
                    gfx::set_uniform(stop_colors_uniform, shader.stop_colors[0], num_stops);
                }

                submit_transform_uniform(translation);

                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
                gfx::set_state(state);

                gfx::submit(pass_id, render_program.native_handle());
                render_program.end();
            }
        }
        break;

        case CompiledShaderType::Creation:
        {
            // Get current time from system interface
            double time = 0.0;
            if(auto* sys_interface = Rml::GetSystemInterface())
            {
                time = sys_interface->GetElapsedTime();
            }

            use_program(RmlUi_ProgramId::Creation);
            auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Creation)];

            if(render_program.begin())
            {
                // Ensure we have the correct layer bound before submitting
                // If we have layers, render to the top layer, otherwise main framebuffer
                auto pass_id = get_layer_pass_id();

                // Set vertex and index buffers
                geometry.bind_buffers(vertex_layout_);

                // Set creation shader uniforms
                auto value_uniform = get_uniform_handle(RmlUi_UniformId::Value);
                auto dimensions_uniform = get_uniform_handle(RmlUi_UniformId::Dimensions);

                if(bgfx::isValid(value_uniform))
                {
                    std::array<float, 4> value_data = {static_cast<float>(time), 0.0f, 0.0f, 0.0f};
                    gfx::set_uniform(value_uniform, value_data.data());
                }

                if(bgfx::isValid(dimensions_uniform))
                {
                    std::array<float, 4> dimensions_data = {shader.dimensions.x, shader.dimensions.y, 0.0f, 0.0f};
                    gfx::set_uniform(dimensions_uniform, dimensions_data.data());
                }

                submit_transform_uniform(translation);

                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
                gfx::set_state(state);

                gfx::submit(pass_id, render_program.native_handle());
                render_program.end();
            }
        }
        break;

        case CompiledShaderType::Invalid:
        default:
        {
            APPLOG_WARNING("Unhandled render shader type {}", static_cast<int>(shader.type));
        }
        break;
    }
}

void RmlUi_RenderInterface::ReleaseShader(Rml::CompiledShaderHandle effect_handle)
{
    if(effect_handle > compiled_shaders_.size())
    {
        APPLOG_ERROR("Invalid shader handle: {}", effect_handle);
        return;
    }

    // Clear the shader entry (but don't remove from vector to keep handles valid)
    compiled_shaders_[effect_handle] = CompiledShader{};
}

void RmlUi_RenderInterface::reset_program()
{
    active_program_ = RmlUi_ProgramId::Color;
    program_transform_dirty_.set();
}

// Private implementation methods

auto RmlUi_RenderInterface::init_vertex_layout() -> bool
{
    // Define vertex layout for RmlUi vertices
    // RmlUi::Vertex has: position(2 floats), color(4 bytes), texcoord(2 floats)
    vertex_layout_.begin()
        .add(gfx::attribute::Position, 2, gfx::attribute_type::Float)
        .add(gfx::attribute::Color0, 4, gfx::attribute_type::Uint8, true) // normalized
        .add(gfx::attribute::TexCoord0, 2, gfx::attribute_type::Float)
        .end();

    return true;
}

auto RmlUi_RenderInterface::init_shaders() -> bool
{
    APPLOG_TRACE("Initializing RmlUi shaders...");

    if(!ctx_ || !ctx_->has<asset_manager>())
    {
        APPLOG_ERROR("Asset manager not available for shader loading");
        return false;
    }

    auto& am = ctx_->get_cached<asset_manager>();

    // Load vertex shader
    auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/vs_rmlui_main.sc");
    if(!vs_shader)
    {
        APPLOG_ERROR("Failed to load RmlUi vertex shader");
        return false;
    }

    // Load additional vertex shaders
    auto vs_passthrough_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/vs_rmlui_passthrough.sc");
    auto vs_blur_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/vs_rmlui_blur.sc");

    // Load fragment shaders
    auto fs_color_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_color.sc");
    auto fs_texture_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_texture.sc");
    auto fs_gradient_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_gradient.sc");
    auto fs_creation_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_creation.sc");
    auto fs_passthrough_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_passthrough.sc");
    auto fs_color_matrix_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_color_matrix.sc");
    auto fs_blend_mask_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_blend_mask.sc");
    auto fs_blur_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_blur.sc");
    auto fs_drop_shadow_shader = am.get_asset<gfx::shader>("engine:/data/shaders/rmlui/fs_rmlui_drop_shadow.sc");

    if(!vs_passthrough_shader || !vs_blur_shader || !fs_color_shader || !fs_texture_shader || !fs_gradient_shader ||
       !fs_creation_shader || !fs_passthrough_shader || !fs_color_matrix_shader || !fs_blend_mask_shader ||
       !fs_blur_shader || !fs_drop_shadow_shader)
    {
        APPLOG_ERROR("Failed to load RmlUi shaders");
        return false;
    }

    // Create programs
    programs_[static_cast<size_t>(RmlUi_ProgramId::Color)] = gpu_program(vs_shader, fs_color_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::Texture)] = gpu_program(vs_shader, fs_texture_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::Gradient)] = gpu_program(vs_shader, fs_gradient_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::Creation)] = gpu_program(vs_shader, fs_creation_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::Passthrough)] =
        gpu_program(vs_passthrough_shader, fs_passthrough_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::ColorMatrix)] =
        gpu_program(vs_passthrough_shader, fs_color_matrix_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::BlendMask)] =
        gpu_program(vs_passthrough_shader, fs_blend_mask_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::Blur)] = gpu_program(vs_blur_shader, fs_blur_shader);
    programs_[static_cast<size_t>(RmlUi_ProgramId::DropShadow)] =
        gpu_program(vs_passthrough_shader, fs_drop_shadow_shader);

    // Verify programs are valid
    for(size_t i = 0; i < static_cast<size_t>(RmlUi_ProgramId::Count); ++i)
    {
        if(!programs_[i].is_valid())
        {
            APPLOG_ERROR("RmlUi program {} is invalid", i);
            return false;
        }
    }

    // Create uniform handles
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Transform)] =
        gfx::create_uniform("u_transform", gfx::uniform_type::Mat4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Translate)] =
        gfx::create_uniform("u_translate", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Tex)] = gfx::create_uniform("s_tex", gfx::uniform_type::Sampler);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::TexMask)] =
        gfx::create_uniform("s_texMask", gfx::uniform_type::Sampler);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Color)] = gfx::create_uniform("u_color", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::ColorMatrix)] =
        gfx::create_uniform("u_color_matrix", gfx::uniform_type::Mat4);

    // Gradient uniforms
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Func)] =
        gfx::create_uniform("u_gradient_func", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::P)] = gfx::create_uniform("u_gradient_p", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::V)] = gfx::create_uniform("u_gradient_v", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::StopColors)] =
        gfx::create_uniform("u_gradient_stops", gfx::uniform_type::Vec4, 16);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::StopPositions)] =
        gfx::create_uniform("u_gradient_positions", gfx::uniform_type::Vec4, 4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::NumStops)] =
        gfx::create_uniform("u_gradient_num_stops", gfx::uniform_type::Vec4);

    // Blur uniforms
    uniforms_[static_cast<size_t>(RmlUi_UniformId::TexelOffset)] =
        gfx::create_uniform("u_texelOffset", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::TexCoordMin)] =
        gfx::create_uniform("u_texCoordMin", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::TexCoordMax)] =
        gfx::create_uniform("u_texCoordMax", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Weights)] =
        gfx::create_uniform("u_weights", gfx::uniform_type::Vec4, 4);

    // Creation shader uniforms
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Value)] = gfx::create_uniform("u_value", gfx::uniform_type::Vec4);
    uniforms_[static_cast<size_t>(RmlUi_UniformId::Dimensions)] =
        gfx::create_uniform("u_dimensions", gfx::uniform_type::Vec4);

    // Verify uniforms are valid
    for(size_t i = 0; i < static_cast<size_t>(RmlUi_UniformId::Count); ++i)
    {
        if(!bgfx::isValid(uniforms_[i]))
        {
            APPLOG_ERROR("RmlUi uniform {} is invalid", i);
            return false;
        }
    }

    APPLOG_INFO("RmlUi shaders initialized successfully");
    return true;
}

void RmlUi_RenderInterface::cleanup_resources()
{
    // Cleanup all compiled geometries using handle allocator
    auto geometry_handles = geometry_handles_;
    for(uint16_t i = 0; i < geometry_handles.getNumHandles(); ++i)
    {
        if(geometry_handles.isValid(i))
        {
            CompiledGeometry& geometry = compiled_geometries_[i];
            geometry.destroy_buffers();
            geometry_handles_.free(i);
        }
    }

    // Cleanup all compiled textures
    for(auto& texture : compiled_textures_)
    {
        if(bgfx::isValid(texture.generated_texture_handle))
        {
            gfx::destroy(texture.generated_texture_handle);
        }

        texture.asset_handle = {};
    }

    // Cleanup uniforms
    for(auto& uniform : uniforms_)
    {
        if(bgfx::isValid(uniform))
        {
            gfx::destroy(uniform);
            uniform = gfx::uniform_handle{gfx::invalid_handle};
        }
    }

    // Clear all containers
    compiled_textures_.clear();
    compiled_filters_.clear();
    compiled_shaders_.clear();
    // Layer cleanup is handled by the RenderLayerStack destructor
}

void RmlUi_RenderInterface::use_program(RmlUi_ProgramId program_id)
{
    if(active_program_ != program_id)
    {
        active_program_ = program_id;
        // Program will be set when submitting the draw call
    }
}

auto RmlUi_RenderInterface::get_uniform_handle(RmlUi_UniformId uniform_id) const -> gfx::uniform_handle
{
    size_t index = static_cast<size_t>(uniform_id);
    if(index < uniforms_.size())
    {
        return uniforms_[index];
    }
    return gfx::uniform_handle{gfx::invalid_handle};
}

void RmlUi_RenderInterface::set_scissor()
{
    if(scissor_enabled_)
    {
        bool vertically_flip = true;
        auto region = scissor_state_;
        if(vertically_flip)
        {
            region = vertically_flipped(region, viewport_height_);
        }

        // Some render APIs don't like offscreen positions, so clamp them to the viewport.
        const int x = Rml::Math::Clamp(region.Left(), 0, viewport_width_);
        const int y = Rml::Math::Clamp(region.Top(), 0, viewport_height_);
        const int width = Rml::Math::Clamp(region.Width(), 0, viewport_width_ - x);
        const int height = Rml::Math::Clamp(region.Height(), 0, viewport_height_ - y);

        // Store scissor in BGfx cache for per-draw-call application
        gfx::set_scissor(static_cast<uint16_t>(x),
                         static_cast<uint16_t>(y),
                         static_cast<uint16_t>(width),
                         static_cast<uint16_t>(height));
    }
}

void RmlUi_RenderInterface::submit_transform_uniform(Rml::Vector2f translation)
{
    // Calculate final transform matrix
    // Start with projection matrix
    // Rml::Matrix4f final_transform = projection_ * transform_;

    // // Apply current transfor

    // Set transform uniform
    auto transform_uniform = get_uniform_handle(RmlUi_UniformId::Transform);
    if(bgfx::isValid(transform_uniform))
    {
        gfx::set_uniform(transform_uniform, transform_.Transpose().data());
    }

    // Set translation uniform (for shader convenience)
    auto translate_uniform = get_uniform_handle(RmlUi_UniformId::Translate);
    if(bgfx::isValid(translate_uniform))
    {
        std::array<float, 4> translate_data = {translation.x, translation.y, 0.0f, 0.0f};
        gfx::set_uniform(translate_uniform, translate_data.data());
    }

    // Mark program as updated
    program_transform_dirty_[static_cast<size_t>(active_program_)] = false;
}

auto RmlUi_RenderInterface::convert_blend_mode(Rml::BlendMode blend_mode) -> uint64_t
{
    // Convert RmlUi blend modes to bgfx render state flags
    switch(blend_mode)
    {
        case Rml::BlendMode::Replace:
            return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

        case Rml::BlendMode::Blend:
            return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

            // Additional blend modes for advanced effects
            // case Rml::BlendMode::Add:
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            //            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

            // case Rml::BlendMode::Multiply:
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            //            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ZERO);

            // case Rml::BlendMode::Screen:
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            //            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE_MINUS_DST_COLOR, BGFX_STATE_BLEND_ONE);

            // case Rml::BlendMode::Overlay:
            //     // Overlay is complex, fallback to normal blend for now
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;

            // case Rml::BlendMode::ColorDodge:
            //     // Color dodge approximation
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            //            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR, BGFX_STATE_BLEND_ONE);

            // case Rml::BlendMode::ColorBurn:
            //     // Color burn approximation
            //     return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
            //            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE_MINUS_SRC_COLOR);

        default:
            return BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
    }
}

void RmlUi_RenderInterface::clear_stencil_buffer(uint32_t clear_value)
{
    // BGfx doesn't have a direct stencil clear function like OpenGL's glClear(GL_STENCIL_BUFFER_BIT)
    // We need to render a fullscreen quad to clear the stencil buffer
    // Convert RmlUi handle to internal handle
    compiled_geometry_handle internal_handle = from_rml_handle(fullscreen_quad_geometry_);
    if(!geometry_handles_.isValid(internal_handle.idx))
    {
        APPLOG_ERROR("Invalid fullscreen quad geometry handle");
        return;
    }

    const auto& geom = compiled_geometries_[internal_handle.idx];

    use_program(RmlUi_ProgramId::Color);
    auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Color)];

    if(render_program.begin())
    {
        // Ensure we have the correct layer bound before submitting
        // If we have layers, render to the top layer, otherwise main framebuffer
        auto pass_id = get_layer_pass_id();

        // Set vertex and index buffers
        geom.bind_buffers(vertex_layout_);

        // Submit identity transform for fullscreen quad
        auto transform_uniform = get_uniform_handle(RmlUi_UniformId::Transform);
        auto translate_uniform = get_uniform_handle(RmlUi_UniformId::Translate);

        if(bgfx::isValid(transform_uniform))
        {
            Rml::Matrix4f identity = Rml::Matrix4f::Identity();
            gfx::set_uniform(transform_uniform, identity.Transpose().data());
        }

        if(bgfx::isValid(translate_uniform))
        {
            std::array<float, 4> translate_data = {0.0f, 0.0f, 0.0f, 0.0f};
            gfx::set_uniform(translate_uniform, translate_data.data());
        }

        // Don't apply scissor for fullscreen stencil clearing

        // Set render state to write only to stencil buffer
        uint64_t stencil_state = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(clear_value) |
                                 BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_REPLACE |
                                 BGFX_STENCIL_OP_FAIL_Z_REPLACE | BGFX_STENCIL_OP_PASS_Z_REPLACE;

        uint64_t render_state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_ALWAYS | stencil_state;

        gfx::set_state(render_state, clear_value);

        // Submit the draw call
        gfx::submit(pass_id, render_program.native_handle());
        render_program.end();
    }
}

void RmlUi_RenderInterface::render_filters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles)
{
    for(const Rml::CompiledFilterHandle filter_handle : filter_handles)
    {
        if(filter_handle == 0 || filter_handle > compiled_filters_.size())
        {
            APPLOG_ERROR("Invalid filter handle: {}", filter_handle);
            continue;
        }

        const CompiledFilter& filter = compiled_filters_[filter_handle - 1];

        switch(filter.type)
        {
            case FilterType::Passthrough:
            {
                use_program(RmlUi_ProgramId::Passthrough);
                auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Passthrough)];

                if(render_program.begin())
                {
                    auto source_texture = render_layers_.get_postprocess_primary().get_color_texture();
                    auto destination = render_layers_.get_postprocess_secondary();

                    gfx::render_pass pass("rmlui_passthrough_pass");
                    pass.bind(destination.framebuffer.get());

                    // Set identity view and projection for filter rendering
                    auto view = Rml::Matrix4f::Identity();
                    auto proj = Rml::Matrix4f::Identity();
                    pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

                    // Set blend factor (opacity)
                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                    if(filter.blend_factor < 1.0f)
                    {
                        // state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_CONSTANT, BGFX_STATE_BLEND_ZERO);
                        //  Note: BGfx doesn't have direct equivalent to glBlendColor,
                        //  we'd need to pass this as a uniform or use different approach
                    }

                    gfx::set_state(state);

                    // Bind source texture and render to destination
                    auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
                    if(bgfx::isValid(tex_uniform) && source_texture)
                    {
                        gfx::set_texture(0, tex_uniform, source_texture->native_handle());
                    }

                    draw_fullscreen_quad();

                    gfx::submit(pass.id, render_program.native_handle());
                    render_program.end();

                    auto& layer = render_layers_.get_top_layer();
                    layer.needs_rebind = true;
                }

                // Swap primary and secondary postprocess buffers
                render_layers_.swap_postprocess_primary_secondary();
            }
            break;

            case FilterType::Blur:
            {
                render_blur(filter.sigma,
                            render_layers_.get_postprocess_primary(),
                            render_layers_.get_postprocess_secondary(),
                            scissor_state_);
            }
            break;

            case FilterType::DropShadow:
            {
                use_program(RmlUi_ProgramId::DropShadow);
                auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::DropShadow)];

                if(render_program.begin())
                {
                    auto source = render_layers_.get_postprocess_primary();
                    auto destination = render_layers_.get_postprocess_secondary();

                    gfx::render_pass pass("rmlui_drop_shadow_pass");
                    pass.bind(destination.framebuffer.get());

                    // Set identity view and projection for filter rendering
                    auto view = Rml::Matrix4f::Identity();
                    auto proj = Rml::Matrix4f::Identity();
                    pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

                    // Set shadow color
                    auto color_uniform = get_uniform_handle(RmlUi_UniformId::Color);
                    if(bgfx::isValid(color_uniform))
                    {
                        Rml::Colourf color;
                        for(int i = 0; i < 4; i++)
                            color[i] = (1.f / 255.f) * float(filter.color[i]);

                        std::array<float, 4> color_data = {color.red, color.green, color.blue, color.alpha};
                        gfx::set_uniform(color_uniform, color_data.data());
                    }

                    // Set texture coordinate limits
                    set_tex_coord_limits(scissor_state_, {viewport_width_, viewport_height_});

                    // Bind texture and render
                    auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
                    auto color_texture = source.get_color_texture();
                    if(bgfx::isValid(tex_uniform) && color_texture)
                    {
                        gfx::set_texture(0, tex_uniform, color_texture->native_handle());
                    }

                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                    gfx::set_state(state);

                    draw_fullscreen_quad();

                    gfx::submit(pass.id, render_program.native_handle());

                    render_program.end();

                    auto& layer = render_layers_.get_top_layer();
                    layer.needs_rebind = true;
                }

                // Apply blur if sigma > 0.5
                if(filter.sigma >= 0.5f)
                {
                    render_blur(filter.sigma,
                                render_layers_.get_postprocess_secondary(),
                                render_layers_.get_postprocess_tertiary(),
                                scissor_state_);
                }

                // Composite shadow with original
                // TODO: Implement shadow compositing
            }
            break;

            case FilterType::ColorMatrix:
            {
                use_program(RmlUi_ProgramId::ColorMatrix);
                auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::ColorMatrix)];

                if(render_program.begin())
                {
                    const auto& source = render_layers_.get_postprocess_primary();
                    const auto& destination = render_layers_.get_postprocess_secondary();

                    gfx::render_pass pass("rmlui_color_matrix_pass");
                    pass.bind(destination.framebuffer.get());

                    // Set identity view and projection for filter rendering
                    auto view = Rml::Matrix4f::Identity();
                    auto proj = Rml::Matrix4f::Identity();
                    pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
                    // Set color matrix
                    auto matrix_uniform = get_uniform_handle(RmlUi_UniformId::ColorMatrix);
                    if(bgfx::isValid(matrix_uniform))
                    {
                        gfx::set_uniform(matrix_uniform, filter.color_matrix.Transpose().data());
                    }

                    // Bind texture and render
                    auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
                    auto color_texture = source.get_color_texture();
                    if(bgfx::isValid(tex_uniform) && color_texture)
                    {
                        gfx::set_texture(0, tex_uniform, color_texture->native_handle());
                    }

                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                    gfx::set_state(state);

                    draw_fullscreen_quad();

                    gfx::submit(pass.id, render_program.native_handle());
                    render_program.end();

                    auto& layer = render_layers_.get_top_layer();
                    layer.needs_rebind = true;
                }

                // Swap buffers
                render_layers_.swap_postprocess_primary_secondary();
            }
            break;

            case FilterType::MaskImage:
            {
                use_program(RmlUi_ProgramId::BlendMask);
                auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::BlendMask)];

                if(render_program.begin())
                {
                    auto source = render_layers_.get_postprocess_primary();
                    auto destination = render_layers_.get_postprocess_secondary();

                    gfx::render_pass pass("rmlui_blend_mask_pass");
                    pass.bind(destination.framebuffer.get());

                    // Set identity view and projection for filter rendering
                    auto view = Rml::Matrix4f::Identity();
                    auto proj = Rml::Matrix4f::Identity();
                    pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
                    // Bind source and mask textures
                    auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
                    auto mask_uniform = get_uniform_handle(RmlUi_UniformId::TexMask);

                    auto color_texture = source.get_color_texture();
                    if(bgfx::isValid(tex_uniform) && color_texture)
                    {
                        gfx::set_texture(0, tex_uniform, color_texture->native_handle());
                    }

                    auto mask_texture = render_layers_.get_blend_mask().get_color_texture();
                    if(bgfx::isValid(mask_uniform) && mask_texture && mask_texture->is_valid())
                    {
                        gfx::set_texture(1, mask_uniform, mask_texture->native_handle());
                    }

                    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
                    gfx::set_state(state);

                    draw_fullscreen_quad();

                    gfx::submit(pass.id, render_program.native_handle());

                    render_program.end();

                    auto& layer = render_layers_.get_top_layer();
                    layer.needs_rebind = true;
                }

                // Swap buffers
                render_layers_.swap_postprocess_primary_secondary();
            }
            break;

            case FilterType::Invalid:
            default:
            {
                APPLOG_WARNING("Unhandled filter type {}", static_cast<int>(filter.type));
            }
            break;
        }
    }
}

void RmlUi_RenderInterface::render_blur(float sigma,
                                        const LayerFramebuffer& source_destination,
                                        const LayerFramebuffer& temp,
                                        Rml::Rectanglei window_region)
{
    if(!source_destination.is_valid() || !temp.is_valid())
    {
        APPLOG_ERROR("Invalid framebuffers for blur");
        return;
    }

    RMLUI_ASSERT(&source_destination != &temp && source_destination.get_size().width == temp.get_size().width &&
                 source_destination.get_size().height == temp.get_size().height);
    RMLUI_ASSERT(window_region.Valid());

    int pass_level = 0;
    sigma_to_parameters(sigma, pass_level, sigma);

    const Rml::Rectanglei original_scissor = scissor_state_;

    // Begin by downscaling so that the blur pass can be done at a reduced resolution for large sigma.
    Rml::Rectanglei scissor = window_region;

    use_program(RmlUi_ProgramId::Passthrough);
    auto passthrough_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Passthrough)];
    // set_scissor_bgfx(scissor, true);

    auto size = source_destination.get_size();
    int fb_width = static_cast<int>(size.width);
    int fb_height = static_cast<int>(size.height);

    // Downscale by iterative half-scaling with bilinear filtering, to reduce aliasing.
    // Scale UVs if we have even dimensions, such that texture fetches align perfectly between texels, thereby producing
    // a 50% blend of neighboring texels.
    const Rml::Vector2f uv_scaling = {(fb_width % 2 == 1) ? (1.f - 1.f / float(fb_width)) : 1.f,
                                      (fb_height % 2 == 1) ? (1.f - 1.f / float(fb_height)) : 1.f};

    if(passthrough_program.begin())
    {
        for(int i = 0; i < pass_level; i++)
        {
            scissor.p0 = (scissor.p0 + Rml::Vector2i(1)) / 2;
            scissor.p1 = Rml::Math::Max(scissor.p1 / 2, scissor.p0);
            const bool from_source = (i % 2 == 0);

            // Create render pass for downscaling
            gfx::render_pass downscale_pass("rmlui_downscale_pass");
            const LayerFramebuffer& destination_fb = from_source ? temp : source_destination;
            downscale_pass.bind(destination_fb.framebuffer.get());

            // Overwrite the view rectangle set by the bind
            gfx::set_view_rect(downscale_pass.id,
                               uint16_t(0),
                               uint16_t(0),
                               source_destination.get_size().width / 2,
                               source_destination.get_size().height / 2);

            // Set view and projection for downscaling
            auto view = Rml::Matrix4f::Identity();
            auto proj = Rml::Matrix4f::Identity();
            downscale_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

            // Bind source texture
            const LayerFramebuffer& source_fb = from_source ? source_destination : temp;
            auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
            auto source_texture = source_fb.get_color_texture();
            if(bgfx::isValid(tex_uniform) && source_texture)
            {
                gfx::set_texture(0, tex_uniform, source_texture->native_handle());
            }

            set_scissor_bgfx(scissor, true);

            auto geometry = draw_fullscreen_quad_with_uv_scaling({}, uv_scaling);
            // draw_fullscreen_quad();
            gfx::submit(downscale_pass.id, passthrough_program.native_handle());
            ReleaseGeometry(geometry);
        }
        passthrough_program.end();
    }

    // Note: BGfx viewport is handled by render pass view rectangles

    // Ensure texture data end up in the temp buffer. Depending on the last downscaling, we might need to move it from
    // the source_destination buffer.
    const bool transfer_to_temp_buffer = (pass_level % 2 == 0);
    if(transfer_to_temp_buffer && passthrough_program.begin())
    {
        gfx::render_pass transfer_pass("rmlui_transfer_to_temp_pass");
        transfer_pass.bind(temp.framebuffer.get());

        auto view = Rml::Matrix4f::Identity();
        auto proj = Rml::Matrix4f::Identity();
        transfer_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

        auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
        auto source_texture = source_destination.get_color_texture();
        if(bgfx::isValid(tex_uniform) && source_texture)
        {
            gfx::set_texture(0, tex_uniform, source_texture->native_handle());
        }
        draw_fullscreen_quad();
        gfx::submit(transfer_pass.id, passthrough_program.native_handle());
        passthrough_program.end();
    }

    // Set up blur uniforms
    use_program(RmlUi_ProgramId::Blur);
    auto blur_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Blur)];

    if(blur_program.begin())
    {
        set_blur_weights(sigma);
        set_tex_coord_limits(scissor, {fb_width, fb_height});

        auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
        auto texel_offset_uniform = get_uniform_handle(RmlUi_UniformId::TexelOffset);

        // Blur render pass - vertical
        {
            gfx::render_pass vertical_blur_pass("rmlui_vertical_blur_pass");
            vertical_blur_pass.bind(source_destination.framebuffer.get());

            auto view = Rml::Matrix4f::Identity();
            auto proj = Rml::Matrix4f::Identity();
            vertical_blur_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

            auto temp_texture = temp.get_color_texture();
            if(bgfx::isValid(tex_uniform) && temp_texture)
            {
                gfx::set_texture(0, tex_uniform, temp_texture->native_handle());
            }

            if(bgfx::isValid(texel_offset_uniform))
            {
                std::array<float, 4> vertical_offset = {0.0f, 1.0f / float(temp.get_size().height), 0.0f, 0.0f};
                gfx::set_uniform(texel_offset_uniform, vertical_offset.data());
            }

            draw_fullscreen_quad();
            gfx::submit(vertical_blur_pass.id, blur_program.native_handle());
        }

        // Blur render pass - horizontal
        {
            gfx::render_pass horizontal_blur_pass("rmlui_horizontal_blur_pass");
            horizontal_blur_pass.bind(temp.framebuffer.get());

            auto view = Rml::Matrix4f::Identity();
            auto proj = Rml::Matrix4f::Identity();
            horizontal_blur_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

            // Add a 1px transparent border around the blur region by first clearing with a padded scissor
            // set_scissor_bgfx(scissor.Extend(1), true);
            horizontal_blur_pass.clear(BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
            // set_scissor_bgfx(scissor, true);

            auto source_texture = source_destination.get_color_texture();
            if(bgfx::isValid(tex_uniform) && source_texture)
            {
                gfx::set_texture(0, tex_uniform, source_texture->native_handle());
            }

            if(bgfx::isValid(texel_offset_uniform))
            {
                std::array<float, 4> horizontal_offset = {1.0f / float(source_destination.get_size().width),
                                                          0.0f,
                                                          0.0f,
                                                          0.0f};
                gfx::set_uniform(texel_offset_uniform, horizontal_offset.data());
            }

            draw_fullscreen_quad();
            gfx::submit(horizontal_blur_pass.id, blur_program.native_handle());
        }

        blur_program.end();
    }

    // Blit the blurred image to the scissor region with upscaling
    // set_scissor_bgfx(window_region, true);

    // Use BGfx blit for upscaling
    auto temp_texture = temp.get_color_texture();
    auto dest_texture = source_destination.get_color_texture();
    if(temp_texture && dest_texture && temp_texture->is_valid() && dest_texture->is_valid())
    {
        gfx::render_pass upscale_pass("rmlui_upscale_blur_pass");

        const Rml::Vector2i src_min = scissor.p0;
        const Rml::Vector2i src_max = scissor.p1;
        const Rml::Vector2i dst_min = window_region.p0;
        const Rml::Vector2i dst_max = window_region.p1;

        gfx::blit(upscale_pass.id,
                  dest_texture->native_handle(),
                  static_cast<uint16_t>(dst_min.x),
                  static_cast<uint16_t>(dst_min.y),
                  temp_texture->native_handle(),
                  static_cast<uint16_t>(src_min.x),
                  static_cast<uint16_t>(src_min.y),
                  static_cast<uint16_t>(src_max.x - src_min.x),
                  static_cast<uint16_t>(src_max.y - src_min.y));

        // Handle exact power-of-two upscaling for stability
        const Rml::Vector2i target_min = src_min * (1 << pass_level);
        const Rml::Vector2i target_max = src_max * (1 << pass_level);
        if(target_min != dst_min || target_max != dst_max)
        {
            gfx::blit(upscale_pass.id,
                      dest_texture->native_handle(),
                      static_cast<uint16_t>(target_min.x),
                      static_cast<uint16_t>(target_min.y),
                      temp_texture->native_handle(),
                      static_cast<uint16_t>(src_min.x),
                      static_cast<uint16_t>(src_min.y),
                      static_cast<uint16_t>(src_max.x - src_min.x),
                      static_cast<uint16_t>(src_max.y - src_min.y));
        }
    }

    // Restore render state
    // set_scissor_bgfx(original_scissor, false);

    auto& layer = render_layers_.get_top_layer();
    layer.needs_rebind = true;
}

void RmlUi_RenderInterface::sigma_to_parameters(const float desired_sigma, int& out_pass_level, float& out_sigma)
{
    constexpr int max_num_passes = 10;
    static_assert(max_num_passes < 31, "");
    constexpr float max_single_pass_sigma = 3.0f;
    out_pass_level =
        Rml::Math::Clamp(Rml::Math::Log2(int(desired_sigma * (2.f / max_single_pass_sigma))), 0, max_num_passes);
    out_sigma = Rml::Math::Clamp(desired_sigma / float(1 << out_pass_level), 0.0f, max_single_pass_sigma);
}

void RmlUi_RenderInterface::set_blur_weights(float sigma)
{
    constexpr int blur_num_weights = 4; // (BLUR_SIZE + 1) / 2 where BLUR_SIZE = 7
    std::array<float, blur_num_weights> weights;
    float normalization = 0.0f;

    for(int i = 0; i < blur_num_weights; i++)
    {
        if(Rml::Math::Absolute(sigma) < 0.1f)
        {
            weights[i] = float(i == 0);
        }
        else
        {
            weights[i] = Rml::Math::Exp(-float(i * i) / (2.0f * sigma * sigma)) /
                         (Rml::Math::SquareRoot(2.f * Rml::Math::RMLUI_PI) * sigma);
        }

        normalization += (i == 0 ? 1.f : 2.0f) * weights[i];
    }

    for(int i = 0; i < blur_num_weights; i++)
    {
        weights[i] /= normalization;
    }

    auto weights_uniform = get_uniform_handle(RmlUi_UniformId::Weights);
    if(bgfx::isValid(weights_uniform))
    {
        gfx::set_uniform(weights_uniform, weights.data(), blur_num_weights);
    }
}

void RmlUi_RenderInterface::set_tex_coord_limits(Rml::Rectanglei region, Rml::Vector2i framebuffer_size)
{
    // Offset by half-texel values so that texture lookups are clamped to fragment centers
    const Rml::Vector2f min = (Rml::Vector2f(region.p0) + Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);
    const Rml::Vector2f max = (Rml::Vector2f(region.p1) - Rml::Vector2f(0.5f)) / Rml::Vector2f(framebuffer_size);

    auto min_uniform = get_uniform_handle(RmlUi_UniformId::TexCoordMin);
    auto max_uniform = get_uniform_handle(RmlUi_UniformId::TexCoordMax);

    if(bgfx::isValid(min_uniform))
    {
        std::array<float, 4> min_data = {min.x, min.y, 0.0f, 0.0f};
        gfx::set_uniform(min_uniform, min_data.data());
    }

    if(bgfx::isValid(max_uniform))
    {
        std::array<float, 4> max_data = {max.x, max.y, 0.0f, 0.0f};
        gfx::set_uniform(max_uniform, max_data.data());
    }
}

void RmlUi_RenderInterface::draw_fullscreen_quad()
{
     // For fullscreen quads, we don't want to apply the coordinate correction transform
    // because we're rendering textures that are already in the corrected coordinate system
    RenderGeometry(fullscreen_quad_geometry_, {}, TexturePostprocess);
}

auto RmlUi_RenderInterface::draw_fullscreen_quad_with_uv_scaling(Rml::Vector2f uv_offset, Rml::Vector2f uv_scaling)
    -> Rml::CompiledGeometryHandle
{
    // Create a temporary fullscreen quad with custom UV scaling
    Rml::Mesh mesh = mesh_fullscreen_quad_;

    if(uv_offset != Rml::Vector2f() || uv_scaling != Rml::Vector2f(1.f))
    {
        for(Rml::Vertex& vertex : mesh.vertices)
        {
            vertex.tex_coord = (vertex.tex_coord * uv_scaling) + uv_offset;
        }
    }

    const Rml::CompiledGeometryHandle geometry = CompileGeometry(mesh.vertices, mesh.indices);
    RenderGeometry(geometry, {}, TexturePostprocess);

    return geometry;
}

void RmlUi_RenderInterface::set_scissor_bgfx(Rml::Rectanglei region, bool vertically_flip)
{
    if(region.Valid() && vertically_flip)
    {
        region = vertically_flipped(region, viewport_height_);
    }

    if(region.Valid())
    {
        // Some render APIs don't like offscreen positions, so clamp them to the viewport.
        const int x = Rml::Math::Clamp(region.Left(), 0, viewport_width_);
        const int y = Rml::Math::Clamp(region.Top(), 0, viewport_height_);
        const int width = Rml::Math::Clamp(region.Width(), 0, viewport_width_ - x);
        const int height = Rml::Math::Clamp(region.Height(), 0, viewport_height_ - y);

        // Store scissor in BGfx cache for per-draw-call application
        gfx::set_scissor(static_cast<uint16_t>(x),
                         static_cast<uint16_t>(y),
                         static_cast<uint16_t>(width),
                         static_cast<uint16_t>(height));
    }
}

void RmlUi_RenderInterface::blit_layer_to_postprocess_primary(Rml::LayerHandle layer_handle)
{
    if(layer_handle >= render_layers_.get_layers_size())
    {
        APPLOG_ERROR("Invalid layer handle for blit: {}", layer_handle);
        return;
    }

    const LayerFramebuffer& source_layer = render_layers_.get_layer(layer_handle);
    if(!source_layer.is_valid())
    {
        APPLOG_ERROR("Source layer framebuffer is invalid");
        return;
    }

    const LayerFramebuffer& primary_fb = render_layers_.get_postprocess_primary();
    if(!primary_fb.is_valid())
    {
        APPLOG_ERROR("Primary postprocess framebuffer is invalid");
        return;
    }

    // Create a render pass for the blit operation
    gfx::render_pass blit_pass("rmlui_blit_layer_pass");
    // Set identity view and projection for filter rendering
    auto view = Rml::Matrix4f::Identity();
    auto proj = Rml::Matrix4f::Identity();
    blit_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
    // Get source and destination textures
    auto source_texture = source_layer.get_color_texture();
    auto destination_texture = primary_fb.get_color_texture();

    // Use BGfx blit to copy the texture content
    if(source_texture && source_texture->is_valid() && destination_texture && destination_texture->is_valid())
    {
        auto size = source_layer.get_size();
        gfx::blit(blit_pass.id,
                  destination_texture->native_handle(),
                  0,
                  0,
                  source_texture->native_handle(),
                  0,
                  0,
                  static_cast<uint16_t>(size.width),
                  static_cast<uint16_t>(size.height));
    }
    else
    {
        APPLOG_ERROR("Invalid texture objects for blit operation");
    }
}

void RmlUi_RenderInterface::composite_to_destination_layer(Rml::LayerHandle destination, Rml::BlendMode blend_mode)
{
    if(destination >= render_layers_.get_layers_size())
    {
        APPLOG_ERROR("Invalid destination layer handle: {}", destination);
        return;
    }

    const LayerFramebuffer& dest_layer = render_layers_.get_layer(destination);
    const LayerFramebuffer& source_fb = render_layers_.get_postprocess_primary();

    if(!dest_layer.is_valid() || !source_fb.is_valid())
    {
        APPLOG_ERROR("Invalid framebuffers for composition");
        return;
    }

    // Use passthrough program to render the postprocessed result
    use_program(RmlUi_ProgramId::Passthrough);
    auto render_program = programs_[static_cast<size_t>(RmlUi_ProgramId::Passthrough)];

    if(render_program.begin())
    {
        // Draw fullscreen quad to composite the result
        gfx::render_pass pass("rmlui_composite_pass");
        pass.bind(dest_layer.framebuffer.get());

        // Set identity view and projection for compositing
        auto view = Rml::Matrix4f::Identity();
        auto proj = Rml::Matrix4f::Identity();
        pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

        // Bind the postprocessed texture
        auto tex_uniform = get_uniform_handle(RmlUi_UniformId::Tex);
        auto source_texture = source_fb.get_color_texture();
        if(bgfx::isValid(tex_uniform) && source_texture && source_texture->is_valid())
        {
            gfx::set_texture(0, tex_uniform, source_texture->native_handle());
        }

        // Set up render state based on blend mode
        uint64_t state = convert_blend_mode(blend_mode);
        gfx::set_state(state);

        draw_fullscreen_quad();

        gfx::submit(pass.id, render_program.native_handle());

        render_program.end();
    }
}

auto RmlUi_RenderInterface::get_layer_pass_id() -> gfx::view_id
{
    auto& target_layer = render_layers_.get_top_layer();

    if(target_layer.needs_rebind)
    {
        // Create a new render pass for this layer
        gfx::render_pass layer_pass("rmlui_layer_pass");
        layer_pass.bind(target_layer.framebuffer.get());
        auto view = Rml::Matrix4f::Identity();
        auto proj = Rml::Matrix4f::Identity();
        layer_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());

        target_layer.pass_id = layer_pass.id;
        target_layer.needs_rebind = false;
    }

    return target_layer.pass_id;
}

// ===============================
// RenderLayerStack Implementation
// ===============================

RmlUi_RenderInterface::RenderLayerStack::RenderLayerStack()
{
    fb_postprocess_.resize(4); // Primary, secondary, tertiary, blend mask
}

RmlUi_RenderInterface::RenderLayerStack::~RenderLayerStack()
{
    destroy_framebuffers();
}

auto RmlUi_RenderInterface::RenderLayerStack::push_layer() -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ <= static_cast<int>(fb_layers_.size()));

    if(layers_size_ == static_cast<int>(fb_layers_.size()))
    {
        // All framebuffers should share a single stencil buffer
        gfx::texture::ptr shared_depth_stencil = nullptr;
        if(!fb_layers_.empty())
        {
            shared_depth_stencil = fb_layers_.front().get_depth_texture();
        }

        fb_layers_.push_back(
            create_layer_framebuffer(width_, height_, RMLUI_NUM_MSAA_SAMPLES, true, shared_depth_stencil));
    }

    // Assign unique ID to this layer

    layers_size_++;
    auto layer_handle = get_top_layer_handle();
    auto& layer = get_layer(layer_handle);
    layer.layer_id = next_layer_id_++;
    layer.needs_rebind = true;

    gfx::render_pass layer_pass("rmlui_layer_clear_pass");
    layer_pass.bind(layer.framebuffer.get());
    auto view = Rml::Matrix4f::Identity();
    auto proj = Rml::Matrix4f::Identity();
    layer_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
    layer_pass.clear(BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);

    return layer_handle;
}

void RmlUi_RenderInterface::RenderLayerStack::pop_layer()
{
    RMLUI_ASSERT(layers_size_ > 0);
    layers_size_ -= 1;

    if(layers_size_ > 0)
    {
        auto& layer = get_top_layer();
        layer.needs_rebind = true;
    }
}

auto RmlUi_RenderInterface::RenderLayerStack::get_layer(Rml::LayerHandle layer) const -> const LayerFramebuffer&
{
    RMLUI_ASSERT(static_cast<size_t>(layer) < static_cast<size_t>(layers_size_));
    return fb_layers_[layer];
}

auto RmlUi_RenderInterface::RenderLayerStack::get_layer(Rml::LayerHandle layer) -> LayerFramebuffer&
{
    RMLUI_ASSERT(static_cast<size_t>(layer) < static_cast<size_t>(layers_size_));
    return fb_layers_[layer];
}

auto RmlUi_RenderInterface::RenderLayerStack::get_top_layer() const -> const LayerFramebuffer&
{
    return get_layer(get_top_layer_handle());
}

auto RmlUi_RenderInterface::RenderLayerStack::get_top_layer() -> LayerFramebuffer&
{
    return get_layer(get_top_layer_handle());
}

auto RmlUi_RenderInterface::RenderLayerStack::get_top_layer_handle() const -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ > 0);
    return static_cast<Rml::LayerHandle>(layers_size_ - 1);
}

auto RmlUi_RenderInterface::RenderLayerStack::get_top_layer_handle() -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ > 0);
    return static_cast<Rml::LayerHandle>(layers_size_ - 1);
}

void RmlUi_RenderInterface::RenderLayerStack::swap_postprocess_primary_secondary()
{
    std::swap(fb_postprocess_[0], fb_postprocess_[1]);
}

void RmlUi_RenderInterface::RenderLayerStack::begin_frame(int new_width, int new_height)
{
    RMLUI_ASSERT(layers_size_ == 0);

    if(new_width != width_ || new_height != height_)
    {
        width_ = new_width;
        height_ = new_height;

        destroy_framebuffers();
    }

    push_layer();
}

void RmlUi_RenderInterface::RenderLayerStack::end_frame()
{
    RMLUI_ASSERT(layers_size_ == 1);
    pop_layer();
}

void RmlUi_RenderInterface::RenderLayerStack::destroy_framebuffers()
{
    RMLUI_ASSERTMSG(layers_size_ == 0,
                    "Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().");

    for(LayerFramebuffer& fb : fb_layers_)
    {   
        destroy_layer_framebuffer(fb);
    }

    fb_layers_.clear();

    for(LayerFramebuffer& fb : fb_postprocess_)
    {
        destroy_layer_framebuffer(fb);
    }
}

auto RmlUi_RenderInterface::RenderLayerStack::ensure_framebuffer_postprocess(int index) -> const LayerFramebuffer&
{
    RMLUI_ASSERT(index < static_cast<int>(fb_postprocess_.size()));
    LayerFramebuffer& fb = fb_postprocess_[index];
    if(!fb.is_valid())
    {
        fb = create_layer_framebuffer(width_, height_, 1, false, nullptr); // No MSAA for postprocess buffers
    }
    return fb;
}

auto RmlUi_RenderInterface::RenderLayerStack::create_layer_framebuffer(int width,
                                                                       int height,
                                                                       int samples,
                                                                       bool with_depth_stencil,
                                                                       gfx::texture::ptr depth_texture)
    -> LayerFramebuffer
{
    LayerFramebuffer fb{};
    fb.samples = samples;

    // BGfx texture creation flags
    uint64_t texture_flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST;
    if(samples > 1)
    {
        // texture_flags |= BGFX_TEXTURE_RT_MSAA_X2; // BGfx uses specific flags for MSAA levels
        // if(samples >= 4)
        // {
        //     texture_flags |= BGFX_TEXTURE_RT_MSAA_X4;
        // }
        // if(samples >= 8)
        // {
        //     texture_flags |= BGFX_TEXTURE_RT_MSAA_X8;
        // }
        // if(samples >= 16)
        // {
        //     texture_flags |= BGFX_TEXTURE_RT_MSAA_X16;
        // }

        // texture_flags |= BGFX_TEXTURE_RT_WRITE_ONLY;
    }

    std::vector<gfx::texture::ptr> textures;

    // Create color texture (MSAA or regular)
    auto color_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(width),
                                                        static_cast<uint16_t>(height),
                                                        false, // no mips
                                                        1,     // num layers
                                                        gfx::texture_format::RGBA8,
                                                        texture_flags);

    if(!color_texture->is_valid())
    {
        APPLOG_ERROR("Failed to create color texture");
        return fb;
    }

    textures.push_back(color_texture);

    // Create depth/stencil buffer if needed
    if(with_depth_stencil)
    {
        if(!depth_texture)
        {
            // Create new depth/stencil buffer with same MSAA settings
            depth_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(width),
                                                           static_cast<uint16_t>(height),
                                                           false, // no mips
                                                           1,     // num layers
                                                           gfx::texture_format::D24S8,
                                                           texture_flags);

            if(!depth_texture->is_valid())
            {
                APPLOG_ERROR("Failed to create depth/stencil texture");
                return fb;
            }
        }
        textures.push_back(depth_texture);
    }

    // Create framebuffer with the textures
    fb.framebuffer = std::make_shared<gfx::frame_buffer>(textures);

    if(!fb.framebuffer->is_valid())
    {
        APPLOG_ERROR("Failed to create framebuffer");
        return fb;
    }

    APPLOG_TRACE("Created framebuffer {}x{} with {} MSAA samples", width, height, samples);
    return fb;
}

void RmlUi_RenderInterface::RenderLayerStack::destroy_layer_framebuffer(LayerFramebuffer& fb)
{
    // Smart pointers will automatically clean up when reset
    fb.framebuffer.reset();

    fb = LayerFramebuffer{}; // Reset to invalid state
}

// CompiledGeometry helper function implementations

void RmlUi_RenderInterface::CompiledGeometry::bind_buffers(const gfx::vertex_layout& vertex_layout) const
{
    switch(buffer_type)
    {
        case GeometryBufferType::Transient:
            // Bind transient buffers

            gfx::transient_vertex_buffer transient_vertex_buffer;
            gfx::transient_index_buffer transient_index_buffer;
            if(allocate_transient_buffers(num_vertices,
                                          num_indices,
                                          vertex_layout,
                                          transient_vertex_buffer,
                                          transient_index_buffer))
            {
                // Copy vertex data
                if(transient_vertex_buffer.data)
                {
                    bx::memCopy(transient_vertex_buffer.data, vertices.data(), vertex_layout.getSize(vertices.size()));
                }

                // Copy index data
                if(transient_index_buffer.data)
                {
                    bx::memCopy(transient_index_buffer.data, indices.data(), indices.size() * sizeof(int));
                }

                gfx::set_vertex_buffer(0, &transient_vertex_buffer);
                gfx::set_index_buffer(&transient_index_buffer);
            }
            break;

        case GeometryBufferType::Static:
        default:
            // Bind static buffers
            gfx::set_vertex_buffer(0, static_vertex_buffer);
            gfx::set_index_buffer(static_index_buffer);
            break;
    }
}

void RmlUi_RenderInterface::CompiledGeometry::destroy_buffers()
{
    switch(buffer_type)
    {
        case GeometryBufferType::Transient:
            // Transient buffers are automatically managed by bgfx, just clear the structs
            vertices = {};
            indices = {};
            break;

        case GeometryBufferType::Static:
        default:
            // Destroy static buffers
            if(bgfx::isValid(static_vertex_buffer))
            {
                gfx::destroy(static_vertex_buffer);
                static_vertex_buffer = BGFX_INVALID_HANDLE;
            }
            if(bgfx::isValid(static_index_buffer))
            {
                gfx::destroy(static_index_buffer);
                static_index_buffer = BGFX_INVALID_HANDLE;
            }
            break;
    }
}

auto RmlUi_RenderInterface::CompiledGeometry::is_valid() const -> bool
{
    switch(buffer_type)
    {
        case GeometryBufferType::Transient:
            return vertices.size() > 0 && indices.size() > 0;

        case GeometryBufferType::Static:
        default:
            return bgfx::isValid(static_vertex_buffer) && bgfx::isValid(static_index_buffer);
    }
}

// Geometry classification and transient buffer management

auto RmlUi_RenderInterface::classify_geometry(uint32_t num_vertices, uint32_t num_indices) const -> GeometryBufferType
{
    // Smallest geometries use transient buffers (most efficient for single-frame use)
    if(num_vertices <= TRANSIENT_GEOMETRY_VERTEX_THRESHOLD)
    {
        return GeometryBufferType::Transient;
    }
    return GeometryBufferType::Static;
}

auto RmlUi_RenderInterface::allocate_transient_buffers(uint32_t num_vertices,
                                                       uint32_t num_indices,
                                                       const gfx::vertex_layout& vertex_layout,
                                                       gfx::transient_vertex_buffer& tvb,
                                                       gfx::transient_index_buffer& tib) -> bool
{
    // Check if transient buffers are available
    if(gfx::get_avail_transient_vertex_buffer(num_vertices, vertex_layout) < num_vertices ||
       gfx::get_avail_transient_index_buffer(num_indices, true) < num_indices) // true for 32-bit indices
    {
        return false;
    }

    // Allocate transient buffers
    gfx::alloc_transient_vertex_buffer(&tvb, num_vertices, vertex_layout);
    gfx::alloc_transient_index_buffer(&tib, num_indices, true); // true for 32-bit indices

    return tvb.data != nullptr && tib.data != nullptr;
}

} // namespace unravelS
