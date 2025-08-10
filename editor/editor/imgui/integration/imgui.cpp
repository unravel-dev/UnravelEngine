/*
 * Copyright 2014-2015 Daniel Collin. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */
#include "imgui.h"
#include "backend/imgui_impl_ospp.h"

#include <bgfx/embedded_shader.h>
#include <bx/allocator.h>
#include <bx/math.h>
#include <bx/timer.h>
#include <graphics/utils/bgfx_utils.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <set>
#include <cstdint>

#include "shaders/fs_imgui_image.bin.h"
#include "shaders/fs_ocornut_imgui.bin.h"
#include "shaders/vs_imgui_image.bin.h"
#include "shaders/vs_ocornut_imgui.bin.h"


#include "fonts/icons/icons_material_design_icons.ttf.h"

#include "fonts/roboto/roboto_regular.ttf.h"
#include "fonts/roboto/robotomono_regular.ttf.h"

#include "fonts/inter/inter_thin.ttf.h"
#include "fonts/inter/inter_light.ttf.h"
#include "fonts/inter/inter_extra_light.ttf.h"
#include "fonts/inter/inter_regular.ttf.h"
#include "fonts/inter/inter_medium.ttf.h"
#include "fonts/inter/inter_semi_bold.ttf.h"
#include "fonts/inter/inter_bold.ttf.h"
#include "fonts/inter/inter_extra_bold.ttf.h"
#include "fonts/inter/inter_black.ttf.h"


#include <engine/engine.h>
#include <engine/assets/asset_manager.h>
#include <graphics/shader.h>

static const gfx::embedded_shader s_embeddedShaders[] = {BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
                                                         BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
                                                         BGFX_EMBEDDED_SHADER(vs_imgui_image),
                                                         BGFX_EMBEDDED_SHADER(fs_imgui_image),

                                                         BGFX_EMBEDDED_SHADER_END()};

struct FontRangeMerge
{
    const void* data;
    size_t size;
    ImWchar ranges[3];
};

static FontRangeMerge s_fontRangeMerge[] = {
    {s_iconsMaterialDesignIconsTtf, sizeof(s_iconsMaterialDesignIconsTtf), {ICON_MIN_MDI, ICON_MAX_MDI, 0}},
};

static void* memAlloc(size_t _size, void* _userData);
static void memFree(void* _ptr, void* _userData);

struct OcornutImguiContext
{
    void renderData(gfx::view_id id, ImDrawData* _drawData)
    {
        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates !=
        // framebuffer coordinates)
        int fb_width = (int)(_drawData->DisplaySize.x * _drawData->FramebufferScale.x);
        int fb_height = (int)(_drawData->DisplaySize.y * _drawData->FramebufferScale.y);
        if(fb_width <= 0 || fb_height <= 0)
            return;

        gfx::set_view_mode(id, gfx::view_mode::Sequential);

        const gfx::caps* caps = gfx::get_caps();
        {
            float ortho[16];
            float x = _drawData->DisplayPos.x;
            float y = _drawData->DisplayPos.y;
            float width = _drawData->DisplaySize.x;
            float height = _drawData->DisplaySize.y;

            bx::mtxOrtho(ortho, x, x + width, y + height, y, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
            gfx::set_view_transform(id, nullptr, ortho);
            gfx::set_view_rect(id, 0, 0, uint16_t(fb_width), uint16_t(fb_height));
        }

        // (0,0) unless using multi-viewports
        const ImVec2 clipPos = _drawData->DisplayPos;

        // (1,1) unless using retina display which are often (2,2)
        const ImVec2 clipScale = _drawData->FramebufferScale;

        // Render command lists
        for(int32_t ii = 0, num = _drawData->CmdListsCount; ii < num; ++ii)
        {
            gfx::transient_vertex_buffer tvb;
            gfx::transient_index_buffer tib;

            const ImDrawList* drawList = _drawData->CmdLists[ii];
            uint32_t numVertices = (uint32_t)drawList->VtxBuffer.size();
            uint32_t numIndices = (uint32_t)drawList->IdxBuffer.size();

            if(!checkAvailTransientBuffers(numVertices, m_layout, numIndices))
            {
                // not enough space in transient buffer just quit drawing the rest...
                break;
            }

            gfx::alloc_transient_vertex_buffer(&tvb, numVertices, m_layout);
            gfx::alloc_transient_index_buffer(&tib, numIndices, sizeof(ImDrawIdx) == 4);

            ImDrawVert* verts = (ImDrawVert*)tvb.data;
            bx::memCopy(verts, drawList->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert));

            ImDrawIdx* indices = (ImDrawIdx*)tib.data;
            bx::memCopy(indices, drawList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));

            gfx::encoder* encoder = gfx::begin();

            std::set<uint32_t> converted{};

            for(const ImDrawCmd *cmd = drawList->CmdBuffer.begin(), *cmdEnd = drawList->CmdBuffer.end(); cmd != cmdEnd;
                ++cmd)
            {
                m_drawCalls++;
                if(cmd->UserCallback)
                {
                    cmd->UserCallback(drawList, cmd);
                }
                else if(0 != cmd->ElemCount)
                {
                    uint64_t state = 0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA;

                    uint8_t tex_index = 0;
                    gfx::texture_handle th = m_texture;
                    gfx::program_handle program = m_program;



                    if(0 != cmd->TextureId)
                    {
                        ImGui::ImTexture texture;
                        texture.id = cmd->TextureId;


                        if(0 != (IMGUI_FLAGS_FLIP_UV & texture.s.flags))
                        {
                            for(uint32_t e = 0; e < cmd->ElemCount; ++e)
                            {
                                auto index = indices[cmd->IdxOffset + e];
                                if(converted.emplace(index).second)
                                {
                                    auto v = (ImDrawVert*)(verts + index);
                                    v->uv.y = 1.0f - v->uv.y;
                                }
                            }
                        }

                        state |= 0 != (IMGUI_FLAGS_ALPHA_BLEND & texture.s.flags)
                                     ? BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                                     : BGFX_STATE_NONE;
                        th = texture.s.handle;
                        tex_index = texture.s.index;
                        if(0 != texture.s.mip)
                        {
                            const float lodEnabled[4] = {float(texture.s.mip), 1.0f, 0.0f, 0.0f};
                            gfx::set_uniform(u_imageLodEnabled, lodEnabled);

                            program = m_imageProgram;
                        }

                        if(0 != (IMGUI_FLAGS_CUBEMAP & texture.s.flags))
                        {
                            const float lodEnabled[4] = {float(texture.s.mip), 1.0f, 0.0f, 0.0f};
                            gfx::set_uniform(u_imageLodEnabled, lodEnabled);
                            program = m_cubemapImageProgram;
                        }

                        if(texture.s.phandle.idx != gfx::invalid_handle)
                        {
                            program = texture.s.phandle;
                        }
                    }
                    else
                    {
                        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
                    }

                    // Project scissor/clipping rectangles into framebuffer space
                    ImVec4 clipRect;
                    clipRect.x = (cmd->ClipRect.x - clipPos.x) * clipScale.x;
                    clipRect.y = (cmd->ClipRect.y - clipPos.y) * clipScale.y;
                    clipRect.z = (cmd->ClipRect.z - clipPos.x) * clipScale.x;
                    clipRect.w = (cmd->ClipRect.w - clipPos.y) * clipScale.y;

                    if(clipRect.x < fb_width && clipRect.y < fb_height && clipRect.z >= 0.0f && clipRect.w >= 0.0f)
                    {
                        const uint16_t xx = uint16_t(bx::max(clipRect.x, 0.0f));
                        const uint16_t yy = uint16_t(bx::max(clipRect.y, 0.0f));
                        encoder->setScissor(xx,
                                            yy,
                                            uint16_t(bx::min(clipRect.z, 65535.0f) - xx),
                                            uint16_t(bx::min(clipRect.w, 65535.0f) - yy));

                        encoder->setState(state);
                        encoder->setTexture(tex_index, s_tex, th);
                        encoder->setVertexBuffer(0, &tvb, cmd->VtxOffset, numVertices);
                        encoder->setIndexBuffer(&tib, cmd->IdxOffset, cmd->ElemCount);
                        encoder->submit(id, program);
                    }


                }
            }

            gfx::end(encoder);
        }
    }

    void RenderCallback(unravel::render_window* window, ImGuiViewport* viewport, void*)
    {
        bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);

        auto& pass = window->begin_present_pass();

        if(clear)
        {
            pass.clear();
        }

        renderData(pass.id, viewport->DrawData);
    }

    void processEvent(os::event& e)
    {
        ImGui_ImplOSPP_ProcessEvent(e);
    }

    void create(unravel::render_window* window, float _fontSize, bx::AllocatorI* _allocator)
    {
        m_allocator = _allocator;

        if(nullptr == _allocator)
        {
            static bx::DefaultAllocator allocator;
            m_allocator = &allocator;
        }

        ImGui::SetAllocatorFunctions(memAlloc, memFree, nullptr);

        m_imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_imgui);

        ImGuiIO& io = ImGui::GetIO();

        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        // io.IniFilename = nullptr;

        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports; // We can create multi-viewports on the
                                                                   // Renderer side (optional)

        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;


        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        //        io.ConfigViewportsNoTaskBarIcon = true;
        io.ConfigDockingTransparentPayload = true;

        auto type = gfx::get_renderer_type();
        m_program = gfx::create_program(gfx::create_embedded_shader(s_embeddedShaders, type, "vs_ocornut_imgui"),
                                        gfx::create_embedded_shader(s_embeddedShaders, type, "fs_ocornut_imgui"),
                                        true);

        u_imageLodEnabled = gfx::create_uniform("u_imageLodEnabled", gfx::uniform_type::Vec4);
        m_imageProgram = gfx::create_program(gfx::create_embedded_shader(s_embeddedShaders, type, "vs_imgui_image"),
                                             gfx::create_embedded_shader(s_embeddedShaders, type, "fs_imgui_image"),
                                             true);
        auto& ctx = unravel::engine::context();
        auto& am = ctx.get_cached<unravel::asset_manager>();
        auto vs_imgui_cubemap_image = am.get_asset<gfx::shader>("editor:/data/shaders/imgui/vs_imgui_cubemap_image.sc");
        auto fs_imgui_cubemap_image = am.get_asset<gfx::shader>("editor:/data/shaders/imgui/fs_imgui_cubemap_image.sc");

        m_cubemapImageProgram = gfx::create_program(vs_imgui_cubemap_image.get()->native_handle(), fs_imgui_cubemap_image.get()->native_handle(), false);

        m_layout.begin()
            .add(gfx::attribute::Position, 2, gfx::attribute_type::Float)
            .add(gfx::attribute::TexCoord0, 2, gfx::attribute_type::Float)
            .add(gfx::attribute::Color0, 4, gfx::attribute_type::Uint8, true)
            .end();

        s_tex = gfx::create_uniform("s_tex", gfx::uniform_type::Sampler);

        uint8_t* data{};
        int32_t width{};
        int32_t height{};
        {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            config.MergeMode = false;
            //			config.MergeGlyphCenterV = true;

            const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();

            m_font[ImGui::Font::Thin] = io.Fonts->AddFontFromMemoryTTF((void*)inter_thin_ttf,
                                                                          sizeof(inter_thin_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);

            m_font[ImGui::Font::ExtraLight] = io.Fonts->AddFontFromMemoryTTF((void*)inter_extra_light_ttf,
                                                                          sizeof(inter_extra_light_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::Light] = io.Fonts->AddFontFromMemoryTTF((void*)inter_light_ttf,
                                                                          sizeof(inter_light_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);

            m_font[ImGui::Font::Regular] = io.Fonts->AddFontFromMemoryTTF((void*)inter_regular_ttf,
                                                                          sizeof(inter_regular_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::Medium] = io.Fonts->AddFontFromMemoryTTF((void*)inter_medium_ttf,
                                                                          sizeof(inter_medium_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::SemiBold] = io.Fonts->AddFontFromMemoryTTF((void*)inter_semi_bold_ttf,
                                                                          sizeof(inter_semi_bold_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::Bold] = io.Fonts->AddFontFromMemoryTTF((void*)inter_bold_ttf,
                                                                          sizeof(inter_bold_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::ExtraBold] = io.Fonts->AddFontFromMemoryTTF((void*)inter_extra_bold_ttf,
                                                                          sizeof(inter_extra_bold_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);
            m_font[ImGui::Font::Black] = io.Fonts->AddFontFromMemoryTTF((void*)inter_black_ttf,
                                                                          sizeof(inter_black_ttf),
                                                                          _fontSize,
                                                                          &config,
                                                                          ranges);


            m_font[ImGui::Font::Mono] = io.Fonts->AddFontFromMemoryTTF((void*)s_robotoMonoRegularTtf,
                                                                       sizeof(s_robotoMonoRegularTtf),
                                                                       _fontSize,
                                                                       &config,
                                                                       ranges);

            config.MergeMode = true;
            config.DstFont = m_font[ImGui::Font::Regular];

            for(uint32_t ii = 0; ii < BX_COUNTOF(s_fontRangeMerge); ++ii)
            {
                const FontRangeMerge& frm = s_fontRangeMerge[ii];

                io.Fonts->AddFontFromMemoryTTF((void*)frm.data, (int)frm.size, _fontSize, &config, frm.ranges);
            }

            config.MergeMode = true;
            config.DstFont = m_font[ImGui::Font::Bold];

            for(uint32_t ii = 0; ii < BX_COUNTOF(s_fontRangeMerge); ++ii)
            {
                const FontRangeMerge& frm = s_fontRangeMerge[ii];

                io.Fonts->AddFontFromMemoryTTF((void*)frm.data, (int)frm.size, _fontSize, &config, frm.ranges);
            }
        }

        io.Fonts->GetTexDataAsRGBA32(&data, &width, &height);

        m_texture = gfx::create_texture_2d((uint16_t)width,
                                           (uint16_t)height,
                                           false,
                                           1,
                                           gfx::texture_format::BGRA8,
                                           0,
                                           gfx::copy(data, width * height * 4));

        auto renderCallback = [this](unravel::render_window* window, ImGuiViewport* viewport, void* args)
        {
            RenderCallback(window, viewport, args);
        };

        auto swapCallback = [this](unravel::render_window* window, ImGuiViewport* viewport, void* args)
        {
        };

        ImGui_ImplOSPP_Init(window, renderCallback, swapCallback);
    }

    void destroy()
    {
        m_keepAlive.clear();
        ImGui_ImplOSPP_Shutdown();
        ImGui::DestroyContext(m_imgui);
        ImGui::SetCurrentContext(nullptr);

        gfx::destroy(s_tex);
        gfx::destroy(m_texture);

        gfx::destroy(u_imageLodEnabled);
        gfx::destroy(m_imageProgram);
        gfx::destroy(m_cubemapImageProgram);
        gfx::destroy(m_program);

        m_allocator = nullptr;
    }

    void beginFrame(float dt)
    {
        m_keepAlive.clear();

        ImGui_ImplOSPP_NewFrame(dt);

        ImGui::NewFrame();

        ImGuizmo::BeginFrame();
    }

    void endFrame(gfx::view_id id)
    {
        m_drawCalls = 0;
        ImGui::Render();
        ImGui_ImplOSPP_EndFrame();
        renderData(id, ImGui::GetDrawData());
    }

    ImGuiContext* m_imgui{};
    bx::AllocatorI* m_allocator{};
    gfx::vertex_layout m_layout;
    gfx::program_handle m_program;
    gfx::program_handle m_imageProgram;
    gfx::program_handle m_cubemapImageProgram;
    gfx::texture_handle m_texture;
    gfx::uniform_handle s_tex;
    gfx::uniform_handle u_imageLodEnabled;
    std::vector<gfx::texture::ptr> m_keepAlive;
    ImFont* m_font[ImGui::Font::Count];
    std::vector<float> m_fontScale{};
    uint64_t m_drawCalls{};
};

static OcornutImguiContext s_ctx;

static void* memAlloc(size_t _size, void* _userData)
{
    BX_UNUSED(_userData);
    return bx::alloc(s_ctx.m_allocator, _size);
}

static void memFree(void* _ptr, void* _userData)
{
    BX_UNUSED(_userData);
    bx::free(s_ctx.m_allocator, _ptr);
}

void imguiCreate(unravel::render_window* window, float _fontSize, bx::AllocatorI* _allocator)
{
    s_ctx.create(window, _fontSize, _allocator);
}

void imguiDestroy()
{
    s_ctx.destroy();
}

void imguiProcessEvent(os::event& e)
{
    s_ctx.processEvent(e);
}

void imguiBeginFrame(float dt)
{
    s_ctx.beginFrame(dt);
    ImGui::PushFont(ImGui::Font::Regular);
}

void imguiEndFrame(gfx::view_id id)
{
    ImGui::PopFont();
    s_ctx.endFrame(id);
}

namespace ImGui
{
void PushFont(Font::Enum _font)
{
    PushFont(s_ctx.m_font[_font]);
}

ImFont* GetFont(Font::Enum _font)
{
    return s_ctx.m_font[_font];
}

void PushEnabled(bool _enabled)
{
    extern void PushItemFlag(int option, bool enabled);
    PushItemFlag(ImGuiItemFlags_Disabled, !_enabled);
    PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (_enabled ? 1.0f : 0.5f));
}

void PopEnabled()
{
    extern void PopItemFlag();
    PopItemFlag();
    PopStyleVar();
}


bool IsReadonly()
{
    auto ctx = GetCurrentContext();
    return (ctx->CurrentItemFlags & ImGuiItemFlags_ReadOnly) != 0;
}

void PushReadonly(bool _enabled)
{
    _enabled |= IsReadonly();
    extern void PushItemFlag(int option, bool enabled);
    PushItemFlag(ImGuiItemFlags_ReadOnly, _enabled);
    //PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (_enabled ? 0.8f : 1.8f));
    PushStyleColor(ImGuiCol_Text, _enabled ? GetStyleColorVec4(ImGuiCol_TextDisabled) : GetStyleColorVec4(ImGuiCol_Text));
}

void PopReadonly()
{
    extern void PopItemFlag();
    PopItemFlag();
    PopStyleColor();
    //PopStyleVar();
}

void PushWindowFontSize(int size)
{
    auto ctx = GetCurrentContext();
    ImGuiWindow* window = ctx->CurrentWindow;
    IM_ASSERT(window);
    auto currentScale = window->FontWindowScale;
    s_ctx.m_fontScale.emplace_back(currentScale);

    auto currentSize = GetFontSize();
    float scale = float(size) / currentSize;

    ImGui::SetWindowFontScale(scale);
}

void PopWindowFontSize()
{
    IM_ASSERT(!s_ctx.m_fontScale.empty());
    auto scale = s_ctx.m_fontScale.back();
    s_ctx.m_fontScale.pop_back();
    ImGui::SetWindowFontScale(scale);

}

void PushWindowFontScale(float scale)
{
    auto ctx = GetCurrentContext();
    ImGuiWindow* window = ctx->CurrentWindow;
    IM_ASSERT(window);
    auto currentScale = window->FontWindowScale;
    s_ctx.m_fontScale.emplace_back(currentScale);

    ImGui::SetWindowFontScale(scale);
}

void PopWindowFontScale()
{
    IM_ASSERT(!s_ctx.m_fontScale.empty());
    auto scale = s_ctx.m_fontScale.back();
    s_ctx.m_fontScale.pop_back();
    ImGui::SetWindowFontScale(scale);

}

uint64_t GetDrawCalls()
{
    return s_ctx.m_drawCalls;
}

void KeepAliveOneFrame(const gfx::texture::ptr& tex)
{
    s_ctx.m_keepAlive.emplace_back(tex);
}


} // namespace ImGui
