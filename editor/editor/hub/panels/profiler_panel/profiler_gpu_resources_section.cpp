#include "profiler_gpu_resources_section.h"

#include "profiler_statistics_utils.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <graphics/graphics.h>
#include <imgui/imgui.h>

namespace unravel
{

namespace
{
constexpr float resource_bar_width = 90.0f;
}

void profiler_draw_gpu_resources_section()
{
    if(!ImGui::CollapsingHeader(ICON_MDI_PUZZLE "\tGPU Resources"))
    {
        return;
    }
    const auto* caps = gfx::get_caps();
    const auto* stats = gfx::get_stats();
    if(caps == nullptr || stats == nullptr)
    {
        return;
    }
    const float item_height = ImGui::GetTextLineHeightWithSpacing();

    ImGui::PushFont(ImGui::Font::Mono);

    ImGui::Text("Resource Usage (Current / Maximum):");
    ImGui::Separator();

    ImGui::Text("Buffers:");
    ImGui::Indent();

    using namespace profiler_statistics_utils;
    draw_resource_bar("TIB",
                      "Transient Index Buffer Used",
                      stats->transientIbUsed,
                      caps->limits.maxTransientIbSize,
                      resource_bar_width,
                      item_height);

    draw_resource_bar("TVB",
                      "Transient Vertex Buffer Used",
                      stats->transientVbUsed,
                      caps->limits.maxTransientVbSize,
                      resource_bar_width,
                      item_height);

    draw_resource_bar("DIB",
                      "Dynamic Index Buffers",
                      stats->numDynamicIndexBuffers,
                      caps->limits.maxDynamicIndexBuffers,
                      resource_bar_width,
                      item_height);

    draw_resource_bar("DVB",
                      "Dynamic Vertex Buffers",
                      stats->numDynamicVertexBuffers,
                      caps->limits.maxDynamicVertexBuffers,
                      resource_bar_width,
                      item_height);

    draw_resource_bar(" IB", "Index Buffers", stats->numIndexBuffers, caps->limits.maxIndexBuffers, resource_bar_width, item_height);

    draw_resource_bar(" VB",
                      "Vertex Buffers",
                      stats->numVertexBuffers,
                      caps->limits.maxVertexBuffers,
                      resource_bar_width,
                      item_height);

    ImGui::Unindent();
    ImGui::Separator();

    ImGui::Text("Shading:");
    ImGui::Indent();

    draw_resource_bar("  P", "Shader Programs", stats->numPrograms, caps->limits.maxPrograms, resource_bar_width, item_height);

    draw_resource_bar("  S", "Shaders", stats->numShaders, caps->limits.maxShaders, resource_bar_width, item_height);

    draw_resource_bar("  U", "Uniforms", stats->numUniforms, caps->limits.maxUniforms, resource_bar_width, item_height);

    ImGui::Unindent();
    ImGui::Separator();

    ImGui::Text("Rendering:");
    ImGui::Indent();

    draw_resource_bar("  T", "Textures", stats->numTextures, caps->limits.maxTextures, resource_bar_width, item_height);

    draw_resource_bar(" FB",
                      "Frame Buffers",
                      stats->numFrameBuffers,
                      caps->limits.maxFrameBuffers,
                      resource_bar_width,
                      item_height);

    draw_resource_bar(" VD",
                      "Vertex Layouts",
                      stats->numVertexLayouts,
                      caps->limits.maxVertexLayouts,
                      resource_bar_width,
                      item_height);

    draw_resource_bar(" OQ",
                      "Occlusion Queries",
                      stats->numOcclusionQueries,
                      caps->limits.maxOcclusionQueries,
                      resource_bar_width,
                      item_height);

    ImGui::Unindent();
    ImGui::PopFont();
}

} // namespace unravel
