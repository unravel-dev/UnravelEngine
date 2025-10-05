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

#include <bx/handlealloc.h>
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
     * @brief Cleanup all resources
     * @param cleanup_func Function to call for each valid resource before freeing
     */
    template<typename CleanupFunc>
    void cleanup_all(CleanupFunc cleanup_func)
    {
        auto handles_copy = handle_allocator_;
        for(uint16_t i = 0; i < handles_copy.getNumHandles(); ++i)
        {
            if(handles_copy.isValid(i))
            {
                cleanup_func(compiled_resources_[i]);
                free(i);
            }
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
     * @brief Set viewport dimensions
     * @param viewport_width Width in pixels
     * @param viewport_height Height in pixels
     * @param viewport_offset_x X offset (default 0)
     * @param viewport_offset_y Y offset (default 0)
     */
    void set_viewport(int viewport_width, int viewport_height, int viewport_offset_x = 0, int viewport_offset_y = 0);

    /**
     * @brief Setup bgfx state for RmlUi rendering
     */
    void begin_frame();

    /**
     * @brief Finish frame and restore previous state
     */
    void end_frame(const gfx::frame_buffer::ptr& framebuffer = nullptr);

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
    

private:
    enum class GeometryBufferType : uint8_t
    {
        Static,     // Large geometries use static buffers
        Transient   // Small geometries use transient buffers
    };

    struct CompiledGeometry
    {
        // Static buffer handles (used when buffer_type == Static)
        gfx::vertex_buffer_handle static_vertex_buffer = BGFX_INVALID_HANDLE;
        gfx::index_buffer_handle static_index_buffer = BGFX_INVALID_HANDLE;
        
        // Transient buffer data (used when buffer_type == Transient)
        Rml::Span<const Rml::Vertex> vertices;
        Rml::Span<const int> indices;
        
        uint32_t num_vertices;
        uint32_t num_indices;
        GeometryBufferType buffer_type = GeometryBufferType::Static;
        
        // Helper functions
        void bind_buffers(const gfx::vertex_layout& vertex_layout) const;
        void destroy_buffers();
        auto is_valid() const -> bool;
        auto is_transient() const -> bool { return buffer_type == GeometryBufferType::Transient; }
    };

    struct CompiledTexture
    {
        asset_handle<gfx::texture> asset_handle;
        gfx::texture::ptr generated_texture_ptr; // For textures created directly (like SaveLayerAsTexture)
        gfx::frame_buffer::ptr generated_framebuffer_ptr; // Keep framebuffer alive when texture is render target
    };

    struct LayerFramebuffer
    {
        uint64_t layer_id = 0;                 // Unique identifier for this layer
        gfx::frame_buffer::ptr framebuffer;
        int samples = 1;                       // MSAA sample count (needed for blit operations)
        bool needs_rebind = true;
        gfx::view_id pass_id = 0;
        
        auto is_valid() const -> bool { return framebuffer && framebuffer->is_valid(); }
        
        // Convenience methods to get info from framebuffer
        auto get_size() const -> usize32_t
        { 
            if (!framebuffer)
            {
                return {0, 0};
            }
                
            return framebuffer->get_size();
        }
        
        auto get_color_texture() const -> gfx::texture::ptr
        {
            return framebuffer ? framebuffer->get_texture(0) : nullptr;
        }
        
        auto get_depth_texture() const -> gfx::texture::ptr
        {
            return framebuffer && framebuffer->get_attachment_count() > 1 ? framebuffer->get_texture(1) : nullptr;
        }
    };

    // Layer stack management (similar to GL implementation)
    class RenderLayerStack {
    public:
        RenderLayerStack();
        ~RenderLayerStack();

        // Push a new layer. All references to previously retrieved layers are invalidated.
        auto push_layer() -> Rml::LayerHandle;

        // Pop the top layer. All references to previously retrieved layers are invalidated.
        void pop_layer();

        auto get_layer(Rml::LayerHandle layer) const -> const LayerFramebuffer&;
        auto get_layer(Rml::LayerHandle layer) -> LayerFramebuffer&;

        auto get_top_layer() const -> const LayerFramebuffer&;
        auto get_top_layer() -> LayerFramebuffer&;

        auto get_top_layer_handle() const -> Rml::LayerHandle;
        auto get_top_layer_handle() -> Rml::LayerHandle;
        auto get_layers_size() const -> int { return layers_size_; }

        auto get_postprocess_primary() -> const LayerFramebuffer& { return ensure_framebuffer_postprocess(0); }
        auto get_postprocess_secondary() -> const LayerFramebuffer& { return ensure_framebuffer_postprocess(1); }
        auto get_postprocess_tertiary() -> const LayerFramebuffer& { return ensure_framebuffer_postprocess(2); }
        auto get_blend_mask() -> const LayerFramebuffer& { return ensure_framebuffer_postprocess(3); }

        void swap_postprocess_primary_secondary();

        void begin_frame(int new_width, int new_height);
        void end_frame();

    private:

        void destroy_framebuffers();
        auto ensure_framebuffer_postprocess(int index) -> const LayerFramebuffer&;
        auto create_layer_framebuffer(int width, int height, int samples, bool with_depth_stencil, gfx::texture::ptr depth_texture) -> LayerFramebuffer;
        void destroy_layer_framebuffer(LayerFramebuffer& fb);

        int width_ = 0;
        int height_ = 0;
        uint64_t next_layer_id_ = 1;  // Next unique layer ID to assign

        // The number of active layers is manually tracked since we re-use the framebuffers stored in the fb_layers stack.
        int layers_size_ = 0;

        std::vector<LayerFramebuffer> fb_layers_;
        std::vector<LayerFramebuffer> fb_postprocess_;
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

    // Initialization
    auto init_shaders() -> bool;
    auto init_vertex_layout() -> bool;
    void cleanup_resources();

    // Shader management
    void use_program(RmlUi_ProgramId program_id);
    auto get_uniform_handle(RmlUi_UniformId uniform_id) const -> gfx::uniform_handle;
    void submit_transform_uniform(Rml::Vector2f translation);
    void set_scissor();
    void set_view_scissor(gfx::view_id pass_id, const Rml::Rectanglei& region);

    // Layer management - now handled by RenderLayerStack
    RenderLayerStack render_layers_;

    // Blend mode conversion
    auto convert_blend_mode(Rml::BlendMode blend_mode) -> uint64_t;

    // Stencil handling
    void clear_stencil_buffer(uint32_t clear_value);

    // Geometry classification
    auto classify_geometry(uint32_t num_vertices, uint32_t num_indices) const -> GeometryBufferType;
        
    // Transient buffer management
    static auto allocate_transient_buffers(uint32_t num_vertices, uint32_t num_indices, const gfx::vertex_layout& vertex_layout,
                                   gfx::transient_vertex_buffer& tvb, gfx::transient_index_buffer& tib) -> bool;

    // Filter rendering
    void render_filters(Rml::Span<const Rml::CompiledFilterHandle> filter_handles);
    void render_blur(float sigma, const LayerFramebuffer& source_destination, const LayerFramebuffer& temp, Rml::Rectanglei window_region);
    void sigma_to_parameters(const float desired_sigma, int& out_pass_level, float& out_sigma);
    void set_blur_weights(float sigma);
    void set_tex_coord_limits(Rml::Rectanglei region, Rml::Vector2i framebuffer_size);
    
    // Layer composition
    void blit_layer_to_postprocess_primary(Rml::LayerHandle layer_handle);
    void composite_to_destination_layer(Rml::LayerHandle destination, Rml::BlendMode blend_mode);
    
    // Layer binding management
    auto get_layer_pass_id() -> gfx::view_id;
    auto get_layer_id_from_handle(Rml::LayerHandle handle) const -> uint64_t;

    // State
    rtti::context* ctx_ = nullptr;
    bool is_initialized_ = false;

    // Viewport
    int viewport_width_ = 0;
    int viewport_height_ = 0;
    int viewport_offset_x_ = 0;
    int viewport_offset_y_ = 0;

    // Transform matrices
    Rml::Matrix4f transform_;
    Rml::Matrix4f projection_;

    // Shaders and programs
    std::array<gpu_program, static_cast<size_t>(RmlUi_ProgramId::Count)> programs_;
    std::array<gfx::uniform_handle, static_cast<size_t>(RmlUi_UniformId::Count)> uniforms_ = {BGFX_INVALID_HANDLE};
    gfx::vertex_layout vertex_layout_;

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
