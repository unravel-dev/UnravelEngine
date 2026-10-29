/*
 * RmlUi Render Layer Stack
 *
 * Manages the layer framebuffer stack for RmlUi rendering (blend modes, filters, etc.)
 */

#pragma once

#include <RmlUi/Core/Types.h>
#include <graphics/frame_buffer.h>
#include <graphics/texture.h>
#include <base/basetypes.hpp>
#include <memory>
#include <vector>

namespace unravel
{

struct RmlUi_LayerFramebuffer
{
    uint64_t layer_id = 0;
    gfx::frame_buffer::ptr framebuffer;
    int samples = 1;
    bool needs_rebind = true;
    bgfx::ViewId pass_id = 0;

    auto is_valid() const -> bool { return framebuffer && framebuffer->is_valid(); }

    auto get_size() const -> usize32_t
    {
        return framebuffer ? framebuffer->get_size() : usize32_t{0, 0};
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

class RmlUi_RenderLayerStack
{
public:
    RmlUi_RenderLayerStack();
    ~RmlUi_RenderLayerStack();

    /// Pushes a layer cleared to transparent. @p clears_depth_stencil also clears the shared depth-stencil,
    /// for the frame's base layer.
    auto push_layer(bool clears_depth_stencil = false) -> Rml::LayerHandle;
    void pop_layer();

    auto get_layer(Rml::LayerHandle layer) const -> const RmlUi_LayerFramebuffer&;
    auto get_layer(Rml::LayerHandle layer) -> RmlUi_LayerFramebuffer&;

    auto get_top_layer() const -> const RmlUi_LayerFramebuffer&;
    auto get_top_layer() -> RmlUi_LayerFramebuffer&;

    auto get_top_layer_handle() const -> Rml::LayerHandle;
    auto get_top_layer_handle() -> Rml::LayerHandle;
    auto get_layers_size() const -> int { return layers_size_; }

    auto get_postprocess_primary() -> const RmlUi_LayerFramebuffer& { return ensure_framebuffer_postprocess(0); }
    auto get_postprocess_secondary() -> const RmlUi_LayerFramebuffer& { return ensure_framebuffer_postprocess(1); }
    auto get_postprocess_tertiary() -> const RmlUi_LayerFramebuffer& { return ensure_framebuffer_postprocess(2); }
    auto get_blend_mask() -> const RmlUi_LayerFramebuffer& { return ensure_framebuffer_postprocess(3); }
    /// A copy target with a mip chain, which bgfx fills after each render into it: a layer composited
    /// into a 3D view is filtered through it where the view shows the document small.
    auto get_world_composite() -> const RmlUi_LayerFramebuffer&;

    void swap_postprocess_primary_secondary();

    /// Starts a frame with its base layer. @p owns_base_layer false gives the base layer no frame buffer:
    /// the renderer draws it straight into the frame's target.
    void begin_frame(int new_width, int new_height, bool owns_base_layer = true);
    /// Continues the frame whose base layer is still open, for the next document drawing into it.
    void continue_frame();
    void end_frame();

private:
    void destroy_framebuffers();
    auto ensure_framebuffer_postprocess(int index) -> const RmlUi_LayerFramebuffer&;
    auto create_layer_framebuffer(int width, int height, int samples, bool with_depth_stencil,
                                 gfx::texture::ptr depth_texture) -> RmlUi_LayerFramebuffer;
    void destroy_layer_framebuffer(RmlUi_LayerFramebuffer& fb);
    auto find_shared_depth_stencil() const -> gfx::texture::ptr;

    int width_ = 0;
    int height_ = 0;
    bool owns_base_layer_ = true;
    uint64_t next_layer_id_ = 1;
    int layers_size_ = 0;

    std::vector<RmlUi_LayerFramebuffer> fb_layers_;
    std::vector<RmlUi_LayerFramebuffer> fb_postprocess_;
    RmlUi_LayerFramebuffer fb_world_composite_;
};

} // namespace unravel
