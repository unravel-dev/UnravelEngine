/*
 * RmlUi BGfx Renderer Interface
 * 
 * Rendering backend for RmlUi using bgfx through the engine's gfx wrapper
 */

#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>
#include <RmlUi/Core/Mesh.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>
#include <graphics/program.h>
#include <graphics/texture.h>
#include <graphics/frame_buffer.h>
#include <engine/rendering/gpu_program.h>
#include <engine/assets/asset_handle.h>
#include "RmlUi_Backend_Engine.h"
#include "RmlUi_RenderLayerStack.h"
#include "RmlUi_WorldMesh.h"
#include "RmlUi_WorldMeshCache.h"
#include "RmlUi_WorldMeshPool.h"

#include <bx/handlealloc.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <memory>
#include <vector>

namespace unravel
{

// Maximum number of handles to support for each resource type
#define MAX_COMPILED_GEOMETRIES size_t(4096*8)
#define MAX_COMPILED_TEXTURES size_t(4096*8)
#define MAX_COMPILED_FILTERS size_t(4096)
#define MAX_COMPILED_SHADERS size_t(4096)

// Buffer allocation thresholds (three-tier system)
#define TRANSIENT_GEOMETRY_VERTEX_THRESHOLD 99999     // Geometries with <= 8 vertices use transient buffers

// Define bgfx-style handles for compiled resources
BGFX_HANDLE(compiled_geometry_handle)
BGFX_HANDLE(compiled_texture_handle)
BGFX_HANDLE(compiled_filter_handle)
BGFX_HANDLE(compiled_shader_handle)

/**
 * @brief Template struct for managing compiled resources with handle allocators
 * @tparam CompiledType The type of compiled resource (e.g., CompiledTexture, CompiledFilter)
 * @tparam InternalHandle The internal handle type (e.g., compiled_texture_handle)
 * @tparam RmlHandle The RmlUi handle type (e.g., Rml::TextureHandle)
 * @tparam MaxHandles Maximum number of handles to support
 */
template<typename CompiledType, typename InternalHandle, typename RmlHandle, size_t MaxHandles>
class compiled_resource_manager
{
public:
    compiled_resource_manager() = default;
    ~compiled_resource_manager() = default;

    // Non-copyable, non-movable for safety
    compiled_resource_manager(const compiled_resource_manager&) = delete;
    compiled_resource_manager& operator=(const compiled_resource_manager&) = delete;
    compiled_resource_manager(compiled_resource_manager&&) = delete;
    compiled_resource_manager& operator=(compiled_resource_manager&&) = delete;

    /**
     * @brief Allocate a new resource handle
     * @return Internal handle index, or bx::kInvalidHandle if allocation failed
     */
    auto alloc() -> uint16_t
    {
        return handle_allocator_.alloc();
    }

    /**
     * @brief Free a resource handle
     * @param handle_idx Internal handle index to free
     */
    void free(uint16_t handle_idx)
    {
        if(handle_allocator_.isValid(handle_idx))
        {
            // Clear the resource entry
            compiled_resources_[handle_idx] = CompiledType{};
            handle_allocator_.free(handle_idx);
        }
    }

    /**
     * @brief Check if a handle is valid
     * @param handle_idx Internal handle index to check
     * @return True if handle is valid
     */
    auto is_valid(uint16_t handle_idx) const -> bool
    {
        return handle_allocator_.isValid(handle_idx);
    }

    /**
     * @brief Get resource by internal handle index
     * @param handle_idx Internal handle index
     * @return Reference to the compiled resource
     */
    auto get(uint16_t handle_idx) -> CompiledType&
    {
        return compiled_resources_[handle_idx];
    }

    /**
     * @brief Get resource by internal handle index (const version)
     * @param handle_idx Internal handle index
     * @return Const reference to the compiled resource
     */
    auto get(uint16_t handle_idx) const -> const CompiledType&
    {
        return compiled_resources_[handle_idx];
    }

    /**
     * @brief Convert internal handle to RmlUi handle
     * @param handle Internal handle
     * @return RmlUi handle (index + 1, since RmlUi uses 0 as invalid)
     */
    static auto to_rml_handle(InternalHandle handle) -> RmlHandle
    {
        return static_cast<RmlHandle>(handle.idx + 1);
    }

    /**
     * @brief Convert RmlUi handle to internal handle
     * @param handle RmlUi handle
     * @return Internal handle
     */
    static auto from_rml_handle(RmlHandle handle) -> InternalHandle
    {
        InternalHandle result;
        result.idx = (handle > 0) ? static_cast<uint16_t>(handle - 1) : bx::kInvalidHandle;
        return result;
    }

    /**
     * @brief Get the number of allocated handles
     * @return Number of allocated handles
     */
    auto get_num_handles() const -> uint16_t
    {
        return handle_allocator_.getNumHandles();
    }

    /**
     * @brief Calls @p func with every allocated resource.
     */
    template<typename Func>
    void for_each(Func func)
    {
        const uint16_t* handles = handle_allocator_.getHandles();
        for(uint16_t i = 0; i < handle_allocator_.getNumHandles(); ++i)
        {
            func(compiled_resources_[handles[i]]);
        }
    }

    /**
     * @brief Cleanup all resources
     * @param cleanup_func Function to call for each valid resource before freeing
     */
    template<typename CleanupFunc>
    void cleanup_all(CleanupFunc cleanup_func)
    {
        // The allocated handles, copied: free() reorders the allocator's list, and once a handle
        // has been freed the allocated ones are no longer 0 .. count - 1.
        const uint16_t* handles = handle_allocator_.getHandles();
        const std::vector<uint16_t> allocated(handles, handles + handle_allocator_.getNumHandles());
        for(const uint16_t handle_idx : allocated)
        {
            cleanup_func(compiled_resources_[handle_idx]);
            free(handle_idx);
        }
    }

private:
    bx::HandleAllocT<MaxHandles> handle_allocator_;
    std::array<CompiledType, MaxHandles> compiled_resources_;
};

enum class RmlUi_ProgramId : uint8_t
{
    Color,          // Color-only rendering
    Texture,        // Textured rendering
    Gradient,       // Gradient rendering
    Creation,       // Creation shader effect
    Passthrough,    // Simple texture passthrough
    ColorMatrix,    // Color matrix transformations (brightness, contrast, etc.)
    BlendMask,      // Mask blending
    Blur,           // Blur effect
    DropShadow,     // Drop shadow effect
    WorldColor,     // Color-only rendering straight into a 3D view, with anti-aliased outlines
    WorldTexture,   // Textured rendering straight into a 3D view, with anti-aliased outlines
    WorldGradient,  // Gradient rendering straight into a 3D view, with anti-aliased outlines
    WorldCreation,  // Creation shader effect straight into a 3D view, with anti-aliased outlines
    Count
};

enum class RmlUi_UniformId : uint8_t
{
    Transform = 0,      // Transform matrix
    Translate,          // Translation vector
    Tex,                // Primary texture sampler
    TexMask,            // Mask texture sampler
    Color,              // Color uniform
    ColorMatrix,        // Color transformation matrix
    // Gradient uniforms
    Func,               // Gradient function type
    P,                  // Gradient parameter P (start point/center)
    V,                  // Gradient parameter V (direction/radius)
    StopColors,         // Gradient stop colors array
    StopPositions,      // Gradient stop positions array
    NumStops,           // Number of gradient stops
    // Blur uniforms
    TexelOffset,        // Texel offset for blur
    TexCoordMin,        // Texture coordinate minimum
    TexCoordMax,        // Texture coordinate maximum
    Weights,            // Blur weights array
    // Creation shader uniforms
    Value,              // Time/animation value
    Dimensions,         // Shader dimensions
    TexRequiresPremultiplication, // 1 when texture RGB must be premultiplied by alpha before blending
    // Document-space clipping
    ElementTransform,   // The element's transform alone: geometry to document pixels
    ClipRect,           // Kept region in document pixels: left, top, right, bottom
    // Drawing straight into a 3D view
    DocumentEdge,       // Document size in pixels and the screen pixels its outline grows by
    TextureRect,        // Texture coordinate bounds of the draw's geometry
    TextureSize,        // Texel size of the bound texture and whether the draw filters its footprint
    Count
};

/**
 * @class RmlUi_RenderInterface
 * @brief RmlUi render interface implementation using bgfx
 * 
 * This class implements the RmlUi rendering interface using the engine's
 * gfx system (bgfx wrapper) for hardware-accelerated rendering.
 */
class RmlUi_RenderInterface : public Rml::RenderInterface
{
public:
    RmlUi_RenderInterface();
    ~RmlUi_RenderInterface();

    /**
     * @brief Initialize the renderer with engine context
     * @param ctx Engine context for accessing graphics systems
     * @return True if initialization was successful
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Cleanup renderer resources
     */
    void shutdown();

    /**
     * @brief Returns true if the renderer was successfully constructed
     */
    explicit operator bool() const { return is_initialized_; }

    /**
     * @brief Setup bgfx state for RmlUi rendering
     * @param state Per-frame state (viewport, framebuffer, etc.)
     */
    void begin_frame(RmlUi_FrameState& state);

    /**
     * @brief Finish frame and present to the framebuffer specified in begin_frame state
     */
    void end_frame();

    /**
     * @brief Clear the current render target
     */
    void clear();

    // -- Inherited from Rml::RenderInterface --

    auto CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) -> Rml::CompiledGeometryHandle override;
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override;

    auto LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) -> Rml::TextureHandle override;
    auto GenerateTexture(Rml::Span<const Rml::byte> source_data, Rml::Vector2i source_dimensions) -> Rml::TextureHandle override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
	void SetScissor(Rml::Rectanglei region, bool vertically_flip = false);

    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation mask_operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;

    void SetTransform(const Rml::Matrix4f* new_transform) override;

    auto PushLayer() -> Rml::LayerHandle override;
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
        Rml::Span<const Rml::CompiledFilterHandle> filters) override;
    void PopLayer() override;

    auto SaveLayerAsTexture() -> Rml::TextureHandle override;
    auto SaveLayerAsMaskImage() -> Rml::CompiledFilterHandle override;

    auto CompileFilter(const Rml::String& name, const Rml::Dictionary& parameters) -> Rml::CompiledFilterHandle override;
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override;

    auto CompileShader(const Rml::String& name, const Rml::Dictionary& parameters) -> Rml::CompiledShaderHandle override;
    void RenderShader(Rml::CompiledShaderHandle shader_handle, Rml::CompiledGeometryHandle geometry_handle, 
        Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle effect_handle) override;

    // Special texture handles for optimization
    static constexpr Rml::TextureHandle TextureEnableWithoutBinding = Rml::TextureHandle(-1);
    static constexpr Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

    // -- Utility functions --
    
    auto get_transform() const -> const Rml::Matrix4f& { return transform_; }
    void reset_program();

    /**
     * @brief The image sources whose texture asset changed since RmlUi loaded them: imported after
     * a missing or failed load, or reloaded with new content.
     *
     * RmlUi keeps a loaded image, and its size, until the source is released. The caller releases
     * these (Rml::ReleaseTexture), so RmlUi loads them again the next time it needs them.
     */
    auto collect_changed_texture_sources() -> std::vector<Rml::String>;


private:
    enum class GeometryBufferType : uint8_t
    {
        Static,     // Large geometries use static buffers
        Transient   // Small geometries use transient buffers
    };

    /// One of the draws after a geometry's draw: its mesh, and where it sits relative to the geometry.
    struct WorldNeighbour
    {
        std::shared_ptr<const RmlUi_ResidentWorldMesh> mesh;
        Rml::Vector2f offset;

        auto operator==(const WorldNeighbour& other) const -> bool
        {
            return mesh == other.mesh && offset == other.offset;
        }
    };

    /// The draws after a geometry's draw that its outline is compared with: at most the queue's length.
    struct WorldNeighbours
    {
        static constexpr size_t MAX_COUNT = 4;
        std::array<WorldNeighbour, MAX_COUNT> items;
        size_t count = 0;

        auto operator==(const WorldNeighbours& other) const -> bool
        {
            return count == other.count && std::equal(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(count), other.items.begin());
        }
    };

    struct CompiledGeometry
    {
        // Static buffer handles (used when buffer_type == Static)
        bgfx::VertexBufferHandle static_vertex_buffer = BGFX_INVALID_HANDLE;
        bgfx::IndexBufferHandle static_index_buffer = BGFX_INVALID_HANDLE;
        
        // Transient buffer data (used when buffer_type == Transient)
        Rml::Span<const Rml::Vertex> vertices;
        Rml::Span<const int> indices;
        
        uint32_t num_vertices;
        uint32_t num_indices;
        GeometryBufferType buffer_type = GeometryBufferType::Static;

        // Texture coordinate bounds of the vertices. A draw straight into a 3D view keeps its samples
        // inside them, so the neighbours of a sprite in an atlas never blend in.
        Rml::Vector2f tex_coord_min{0.0f, 0.0f};
        Rml::Vector2f tex_coord_max{1.0f, 1.0f};

        // The mesh as a draw straight into a 3D view reads it (RmlUi_WorldMesh), resident in the world mesh
        // pool and shared with every geometry of the same content (RmlUi_WorldMeshCache); null until the
        // geometry is first drawn so.
        std::shared_ptr<const RmlUi_ResidentWorldMesh> world_mesh;
        // The draws after this geometry's last draw whose outline boxes overlapped its own, and the mesh
        // drawn for them: world_mesh, or its variant for the edges of theirs that run along its own. Kept
        // while the same meshes follow at the same offsets, which saves comparing the outlines again.
        WorldNeighbours world_neighbours;
        std::shared_ptr<const RmlUi_ResidentWorldMesh> world_drawn_mesh;

        // Helper functions
        void bind_buffers(const bgfx::VertexLayout& vertex_layout) const;
        void destroy_buffers();
        void release_world_meshes();
        auto is_valid() const -> bool;
        auto is_transient() const -> bool { return buffer_type == GeometryBufferType::Transient; }
    };

    struct CompiledTexture
    {
        asset_handle<gfx::texture> asset;
        /// The asset's texture as RmlUi loaded it; null while the image is missing or failed to load.
        gfx::texture::ptr loaded;
        /// The source RmlUi loaded the image for; empty for generated textures.
        Rml::String source;
        gfx::texture::ptr generated_texture_ptr; // For textures created directly (like SaveLayerAsTexture)
        gfx::frame_buffer::ptr generated_framebuffer_ptr; // Keep framebuffer alive when texture is render target
        bool requires_premultiplication = false; // Asset textures store straight alpha; generated RmlUi textures do not
    };

    enum class FilterType { Invalid = 0, Passthrough, Blur, DropShadow, ColorMatrix, MaskImage };
    
    struct CompiledFilter
    {
        FilterType type = FilterType::Invalid;
        
        // Passthrough
        float blend_factor = 1.0f;
        
        // Blur
        float sigma = 1.0f;
        
        // Drop shadow
        Rml::Vector2f offset{0.0f, 0.0f};
        Rml::ColourbPremultiplied color{255, 255, 255, 255};
        
        // ColorMatrix
        Rml::Matrix4f color_matrix = Rml::Matrix4f::Identity();
    };

    enum class ShaderGradientFunction { Linear, Radial, Conic, RepeatingLinear, RepeatingRadial, RepeatingConic };
    enum class CompiledShaderType { Invalid = 0, Gradient, Creation };
    
    struct CompiledShader
    {
        CompiledShaderType type = CompiledShaderType::Invalid;
        
        // Gradient
        ShaderGradientFunction gradient_function = ShaderGradientFunction::Linear;
        Rml::Vector2f p{0.0f, 0.0f};
        Rml::Vector2f v{1.0f, 0.0f};
        Rml::Vector<float> stop_positions;
        Rml::Vector<Rml::Colourf> stop_colors;
        
        // Creation shader
        Rml::Vector2f dimensions{0.0f, 0.0f};
    };

    /**
     * @brief A draw into the 3D view of a world target, held back for a few draws: where a later draw's
     * outline runs along its own, it reaches under the later draw or pulls in beneath it instead of
     * fading its own edge, which would let it, or what lies behind, show through the later fading edge.
     */
    struct PendingWorldDraw
    {
        RmlUi_ProgramId program_id = RmlUi_ProgramId::WorldColor;
        uint16_t geometry_index = 0;
        bgfx::ViewId view_id = 0;
        Rml::Vector2f translation;
        Rml::Matrix4f element_transform;
        Rml::Rectanglef clip_region;
        uint64_t state = 0;
        // Texture draws
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        bool requires_premultiplication = false;
        usize32_t texture_size;
        // Shader draws
        uint16_t shader_index = 0;
        float elapsed_time = 0.0f;
    };

    // Initialization
    auto init_shaders() -> bool;
    auto init_vertex_layout() -> bool;

    /// Finds or builds the mesh a draw straight into a 3D view reads, the first time; false for an empty mesh.
    auto ensure_world_mesh(CompiledGeometry& geometry) -> bool;
    /// The mesh @p draw of @p geometry draws, given the queued draws after it: the base mesh, or its
    /// variant for the edges of theirs that run along its outline.
    auto get_drawn_world_mesh(CompiledGeometry& geometry, const PendingWorldDraw& draw, Rml::Span<const PendingWorldDraw> later_draws)
        -> const RmlUi_ResidentWorldMesh*;
    /// Recycles the world mesh pool and trims its cache, once per frame.
    void begin_world_mesh_frame();
    void cleanup_resources();

    /// The texture a draw with RmlUi's @p texture samples: the loaded asset, a generated texture, or a
    /// transparent one while an image is missing; null for an invalid handle.
    auto get_sampled_texture(Rml::TextureHandle texture, bool& requires_premultiplication) -> gfx::texture::ptr;
    void set_premultiplication_uniform(bool requires_premultiplication);

    // Draws into a world target's 3D view
    auto make_pending_world_draw(RmlUi_ProgramId program_id, uint16_t geometry_index, Rml::Vector2f translation) -> PendingWorldDraw;
    /// Queues @p draw and submits the oldest once the queue is full.
    void queue_world_draw(const PendingWorldDraw& draw);
    void flush_world_draws();
    /// Submits @p draw, given the queued draws after it.
    void submit_world_draw(const PendingWorldDraw& draw, Rml::Span<const PendingWorldDraw> later_draws);
    /// The draws among @p later_draws under the same transform as @p draw whose outline box overlaps its own.
    auto collect_world_neighbours(const CompiledGeometry& geometry,
                                  const PendingWorldDraw& draw,
                                  Rml::Span<const PendingWorldDraw> later_draws) const -> WorldNeighbours;
    void set_gradient_uniforms(const CompiledShader& shader);
    void set_creation_uniforms(const CompiledShader& shader, float elapsed_time);

    // Shader management
    void use_program(RmlUi_ProgramId program_id);
    auto get_uniform_handle(RmlUi_UniformId uniform_id) const -> bgfx::UniformHandle;
    void submit_transform_uniform(Rml::Vector2f translation);
    /// @param outline_growth Screen pixels the document's outline grows by, 0 to draw it as it is.
    void set_transform_uniforms(const Rml::Matrix4f& projection, const Rml::Matrix4f& element_transform,
                                Rml::Vector2f translation, float outline_growth);
    /**
     * @brief Sets how the texture program samples: within @p tex_coord_min .. @p tex_coord_max, and with
     * @p filters_footprint averaging taps across each pixel's footprint where the texture is minified,
     * as a document drawn straight into a 3D view is when seen from afar.
     */
    void set_texture_sampling(Rml::Vector2f tex_coord_min, Rml::Vector2f tex_coord_max, usize32_t texture_size,
                              bool filters_footprint);
    void set_scissor();
    /// Clips the next draw to the scissor region: by the hardware scissor in a layer of its own, by
    /// the clip rectangle in the shaders when drawing straight into a 3D view.
    void apply_clip();
    void set_clip_rect_uniform(const Rml::Rectanglef& region);
    /// The region a draw straight into a 3D view keeps, in document pixels: the scissor region when
    /// one is set, else the whole document, as a render target of the document's size would clip.
    auto get_world_clip_region() const -> Rml::Rectanglei;
    /// The blend state of a draw into the current layer; a draw straight into a 3D view also tests
    /// the view's depth.
    auto get_draw_state(Rml::BlendMode blend_mode) -> uint64_t;

    // Drawing straight into a 3D view (RmlUi_FrameState::document_to_clip)
    auto is_world_target_layer(Rml::LayerHandle layer_handle) const -> bool;
    auto is_drawing_to_world_target() const -> bool;
    /// Records that the frame met something only a base layer of its own can draw.
    void request_render_target();
    /// Draws the document's whole rectangle straight into the 3D view with @p program_id, its outline grown
    /// by @p outline_growth screen pixels.
    void submit_document_quad(RmlUi_ProgramId program_id, uint64_t state, bgfx::ViewId view_id, float outline_growth);
    /// Writes the document's plane into the 3D view's depth, so what is drawn later hides behind it.
    void write_document_depth();
    void composite_to_world_target(Rml::LayerHandle destination);
    void set_view_scissor(bgfx::ViewId pass_id, const Rml::Rectanglei& region);
    auto get_viewport_size() const -> Rml::Vector2i;

    // Layer management - pointer to stack from frame state (set in begin_frame)
    RmlUi_RenderLayerStack* render_layers_ = nullptr;

    // Blend mode conversion
    auto convert_blend_mode(Rml::BlendMode blend_mode) -> uint64_t;

    // Stencil handling
    void clear_stencil_buffer(uint32_t clear_value);

    // Geometry classification
    auto classify_geometry(uint32_t num_vertices, uint32_t num_indices) const -> GeometryBufferType;
        
    // Transient buffer management
    static auto allocate_transient_buffers(uint32_t num_vertices, uint32_t num_indices, const bgfx::VertexLayout& vertex_layout,
                                   bgfx::TransientVertexBuffer& tvb, bgfx::TransientIndexBuffer& tib) -> bool;

    // Filter rendering
    void render_filters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles);
    void render_blur(float sigma, const RmlUi_LayerFramebuffer& source_destination, const RmlUi_LayerFramebuffer& temp, Rml::Rectanglei window_region);
    void sigma_to_parameters(const float desired_sigma, int& out_pass_level, float& out_sigma);
    void set_blur_weights(float sigma);
    void set_tex_coord_limits(Rml::Rectanglei region, Rml::Vector2i framebuffer_size);
    
    // Layer composition
    void blit_layer_to_postprocess_primary(Rml::LayerHandle layer_handle);
    void composite_to_destination_layer(Rml::LayerHandle destination, Rml::BlendMode blend_mode);
    
    // Layer binding management
    auto get_layer_pass_id() -> bgfx::ViewId;
    auto get_layer_view(Rml::LayerHandle layer_handle) -> bgfx::ViewId;
    auto get_layer_id_from_handle(Rml::LayerHandle handle) const -> uint64_t;

    // State
    rtti::context* ctx_ = nullptr;
    bool is_initialized_ = false;

    // Current frame state (set in begin_frame, used until end_frame)
    RmlUi_FrameState* frame_state_ = nullptr;

    // Transform matrices
    Rml::Matrix4f transform_;
    Rml::Matrix4f projection_;
    Rml::Matrix4f element_transform_;

    // Shaders and programs
    std::array<gpu_program, static_cast<size_t>(RmlUi_ProgramId::Count)> programs_;
    std::array<bgfx::UniformHandle, static_cast<size_t>(RmlUi_UniformId::Count)> uniforms_ = {BGFX_INVALID_HANDLE};
    bgfx::VertexLayout vertex_layout_;
    /// The second vertex stream of a draw straight into a 3D view: RmlUi_WorldShapeVertex.
    bgfx::VertexLayout world_shape_layout_;
    /// World draws held back until a later draw tells whether it covers them (PendingWorldDraw).
    std::vector<PendingWorldDraw> pending_world_draws_;
    /// Where the meshes of draws straight into a 3D view live on the GPU, and the meshes shared by content.
    /// Declared before the geometries that hold meshes, so it outlives them.
    RmlUi_WorldMeshPool world_mesh_pool_;
    RmlUi_WorldMeshCache world_mesh_cache_{world_mesh_pool_};
    uint32_t world_mesh_frame_ = 0;

    // Active state
    RmlUi_ProgramId active_program_ = RmlUi_ProgramId::Color;
    std::bitset<static_cast<size_t>(RmlUi_ProgramId::Count)> program_transform_dirty_;
    Rml::Rectanglei scissor_state_;
    bool scissor_enabled_ = false;
    bool clip_mask_enabled_ = false;
    uint32_t stencil_test_ref_ = 1;

    // Resource storage with handle allocators
    compiled_resource_manager<CompiledGeometry, compiled_geometry_handle, Rml::CompiledGeometryHandle, MAX_COMPILED_GEOMETRIES> geometry_manager_;
    compiled_resource_manager<CompiledTexture, compiled_texture_handle, Rml::TextureHandle, MAX_COMPILED_TEXTURES> texture_manager_;
    compiled_resource_manager<CompiledFilter, compiled_filter_handle, Rml::CompiledFilterHandle, MAX_COMPILED_FILTERS> filter_manager_;
    compiled_resource_manager<CompiledShader, compiled_shader_handle, Rml::CompiledShaderHandle, MAX_COMPILED_SHADERS> shader_manager_;
    
};

} // namespace unravel
