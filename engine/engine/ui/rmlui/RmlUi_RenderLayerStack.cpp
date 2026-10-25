/*
 * RmlUi Render Layer Stack Implementation
 */

#include "RmlUi_RenderLayerStack.h"
#include <RmlUi/Core/Debug.h>
#include <graphics/graphics.h>
#include <logging/logging.h>
#include <string_utils/utils.h>
#include <graphics/render_pass.h>
#include <base/platform/config.hpp>

// Samples per pixel of the UI layers: smoother edges, especially under transforms. bgfx stores every
// sample of a multisampled target and resolves it in a separate copy, so on a phone the full-screen
// layer costs more memory traffic than the smoother edges are worth.
#ifndef RMLUI_NUM_MSAA_SAMPLES
#if UNRAVEL_PLATFORM_ANDROID
#define RMLUI_NUM_MSAA_SAMPLES 1
#else
#define RMLUI_NUM_MSAA_SAMPLES 2
#endif
#endif

namespace unravel
{

RmlUi_RenderLayerStack::RmlUi_RenderLayerStack()
{
    fb_postprocess_.resize(4); // Primary, secondary, tertiary, blend mask
}

RmlUi_RenderLayerStack::~RmlUi_RenderLayerStack()
{
    destroy_framebuffers();
}

auto RmlUi_RenderLayerStack::push_layer(bool clears_depth_stencil) -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ <= static_cast<int>(fb_layers_.size()));

    const bool is_external_base_layer = layers_size_ == 0 && !owns_base_layer_;
    if(layers_size_ == static_cast<int>(fb_layers_.size()))
    {
        if(is_external_base_layer)
        {
            fb_layers_.emplace_back();
        }
        else
        {
            fb_layers_.push_back(create_layer_framebuffer(width_,
                                                          height_,
                                                          RMLUI_NUM_MSAA_SAMPLES,
                                                          true,
                                                          find_shared_depth_stencil()));
        }
    }

    layers_size_++;
    auto layer_handle = get_top_layer_handle();
    auto& layer = get_layer(layer_handle);
    layer.layer_id = next_layer_id_++;
    layer.needs_rebind = true;

    gfx::render_pass::push_scope(fmt::format("Layer {}", layer.layer_id).c_str());
    if(is_external_base_layer)
    {
        return layer_handle;
    }
    gfx::render_pass layer_pass("Layer Clear Pass");
    layer_pass.bind(layer.framebuffer.get());
    auto view = Rml::Matrix4f::Identity();
    auto proj = Rml::Matrix4f::Identity();
    layer_pass.set_view_proj(view.Transpose().data(), proj.Transpose().data());
    // The layers share one depth-stencil: only the frame's first layer with a frame buffer starts it
    // cleared, so a nested layer keeps the clip mask of the layer it was pushed from.
    const bool is_first_owned_layer = layers_size_ == 2 && !owns_base_layer_;
    const uint16_t clear_flags = clears_depth_stencil || is_first_owned_layer
                                     ? BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL
                                     : BGFX_CLEAR_COLOR;
    layer_pass.clear(clear_flags, 0x00000000, 1.0f, 0);

    return layer_handle;
}

void RmlUi_RenderLayerStack::pop_layer()
{
    RMLUI_ASSERT(layers_size_ > 0);
    layers_size_ -= 1;

    if(layers_size_ > 0)
    {
        auto& layer = get_top_layer();
        layer.needs_rebind = true;
    }

    gfx::render_pass::pop_scope();
}

auto RmlUi_RenderLayerStack::get_layer(Rml::LayerHandle layer) const -> const RmlUi_LayerFramebuffer&
{
    RMLUI_ASSERT(static_cast<size_t>(layer) < static_cast<size_t>(layers_size_));
    return fb_layers_[layer];
}

auto RmlUi_RenderLayerStack::get_layer(Rml::LayerHandle layer) -> RmlUi_LayerFramebuffer&
{
    RMLUI_ASSERT(static_cast<size_t>(layer) < static_cast<size_t>(layers_size_));
    return fb_layers_[layer];
}

auto RmlUi_RenderLayerStack::get_top_layer() const -> const RmlUi_LayerFramebuffer&
{
    return get_layer(get_top_layer_handle());
}

auto RmlUi_RenderLayerStack::get_top_layer() -> RmlUi_LayerFramebuffer&
{
    return get_layer(get_top_layer_handle());
}

auto RmlUi_RenderLayerStack::get_top_layer_handle() const -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ > 0);
    return static_cast<Rml::LayerHandle>(layers_size_ - 1);
}

auto RmlUi_RenderLayerStack::get_top_layer_handle() -> Rml::LayerHandle
{
    RMLUI_ASSERT(layers_size_ > 0);
    return static_cast<Rml::LayerHandle>(layers_size_ - 1);
}

void RmlUi_RenderLayerStack::swap_postprocess_primary_secondary()
{
    std::swap(fb_postprocess_[0], fb_postprocess_[1]);
}

void RmlUi_RenderLayerStack::begin_frame(int new_width, int new_height, bool owns_base_layer)
{
    RMLUI_ASSERT(layers_size_ == 0);

    if(new_width != width_ || new_height != height_ || owns_base_layer != owns_base_layer_)
    {
        width_ = new_width;
        height_ = new_height;
        owns_base_layer_ = owns_base_layer;
        destroy_framebuffers();
    }

    gfx::render_pass::push_scope(fmt::format("RmlUi").c_str());
    push_layer(true);
}

void RmlUi_RenderLayerStack::end_frame()
{
    RMLUI_ASSERT(layers_size_ == 1);
    pop_layer();
    gfx::render_pass::pop_scope();
}

void RmlUi_RenderLayerStack::destroy_framebuffers()
{
    RMLUI_ASSERTMSG(layers_size_ == 0,
                    "Do not call this during frame rendering, that is, between BeginFrame() and EndFrame().");

    for(RmlUi_LayerFramebuffer& fb : fb_layers_)
    {
        destroy_layer_framebuffer(fb);
    }
    fb_layers_.clear();

    for(RmlUi_LayerFramebuffer& fb : fb_postprocess_)
    {
        destroy_layer_framebuffer(fb);
    }
    destroy_layer_framebuffer(fb_world_composite_);
}

auto RmlUi_RenderLayerStack::ensure_framebuffer_postprocess(int index) -> const RmlUi_LayerFramebuffer&
{
    RMLUI_ASSERT(index < static_cast<int>(fb_postprocess_.size()));
    RmlUi_LayerFramebuffer& fb = fb_postprocess_[index];
    if(!fb.is_valid())
    {
        fb = create_layer_framebuffer(width_, height_, 1, false, nullptr);
    }
    return fb;
}

auto RmlUi_RenderLayerStack::get_world_composite() -> const RmlUi_LayerFramebuffer&
{
    if(fb_world_composite_.is_valid())
    {
        return fb_world_composite_;
    }
    auto color_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(width_),
                                                       static_cast<uint16_t>(height_),
                                                       true,
                                                       1,
                                                       bgfx::TextureFormat::RGBA8,
                                                       BGFX_TEXTURE_RT);
    if(!color_texture->is_valid())
    {
        APPLOG_ERROR("Failed to create the world composite texture");
        return fb_world_composite_;
    }
    fb_world_composite_.framebuffer = std::make_shared<gfx::frame_buffer>(std::vector<gfx::texture::ptr>{color_texture});
    return fb_world_composite_;
}

auto RmlUi_RenderLayerStack::create_layer_framebuffer(int width,
                                                     int height,
                                                     int samples,
                                                     bool with_depth_stencil,
                                                     gfx::texture::ptr depth_texture)
    -> RmlUi_LayerFramebuffer
{
    RmlUi_LayerFramebuffer fb{};
    fb.samples = samples;

    uint64_t texture_flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST;
    if(samples > 1)
    {
        texture_flags |= BGFX_TEXTURE_RT_MSAA_X2;
        if(samples >= 4)
            texture_flags |= BGFX_TEXTURE_RT_MSAA_X4;
        if(samples >= 8)
            texture_flags |= BGFX_TEXTURE_RT_MSAA_X8;
        if(samples >= 16)
            texture_flags |= BGFX_TEXTURE_RT_MSAA_X16;
    }

    uint64_t depth_texture_flags = texture_flags;
    if(samples > 1)
    {
        depth_texture_flags |= BGFX_TEXTURE_RT_WRITE_ONLY;
    }

    auto color_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(width),
                                                       static_cast<uint16_t>(height),
                                                       false,
                                                       1,
                                                       bgfx::TextureFormat::RGBA8,
                                                       texture_flags);

    if(!color_texture->is_valid())
    {
        APPLOG_ERROR("Failed to create color texture");
        return fb;
    }

    std::vector<gfx::texture::ptr> textures;
    textures.push_back(color_texture);

    if(with_depth_stencil)
    {
        if(!depth_texture)
        {
            depth_texture = std::make_shared<gfx::texture>(static_cast<uint16_t>(width),
                                                          static_cast<uint16_t>(height),
                                                          false,
                                                          1,
                                                          bgfx::TextureFormat::D24S8,
                                                          depth_texture_flags);

            if(!depth_texture->is_valid())
            {
                APPLOG_ERROR("Failed to create depth/stencil texture");
                return fb;
            }
        }
        textures.push_back(depth_texture);
    }

    fb.framebuffer = std::make_shared<gfx::frame_buffer>(textures);

    if(!fb.framebuffer->is_valid())
    {
        APPLOG_ERROR("Failed to create framebuffer");
        return fb;
    }

    return fb;
}

void RmlUi_RenderLayerStack::destroy_layer_framebuffer(RmlUi_LayerFramebuffer& fb)
{
    fb.framebuffer.reset();
    fb = RmlUi_LayerFramebuffer{};
}

auto RmlUi_RenderLayerStack::find_shared_depth_stencil() const -> gfx::texture::ptr
{
    // A base layer drawn straight into the frame's target has none: the next layer holds it.
    for(const RmlUi_LayerFramebuffer& layer : fb_layers_)
    {
        if(auto depth_stencil = layer.get_depth_texture())
        {
            return depth_stencil;
        }
    }
    return nullptr;
}

} // namespace unravel
