#include "gizmos_menu.h"
#include "../panel_toolbar.h"
#include "../visualization_widgets.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>
#include <editor/imgui/screen_card.h>
#include <editor/system/project_manager.h>

#include <engine/meta/ecs/components/all_components.h>
#include <engine/rendering/camera.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/tooltips.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <functional>
#include <span>
#include <string>
#include <tuple>

namespace unravel::gizmos_menu
{
namespace
{
using screen_card::to_pixels;

// Sizes are in units of the font size.
constexpr float POPUP_WIDTH = 23.5f;
constexpr float ROW_HEIGHT = 1.75f;
constexpr float HOVER_FILL_ROUNDING = 0.3f;
constexpr float DISCLOSURE_WIDTH = 1.15f;
constexpr float TYPE_ICON_WIDTH = 1.45f;
constexpr float COLUMN_WIDTH = 4.1f;
constexpr float COUNT_GAP = 0.45f;
constexpr float NO_FLAG_MARK_WIDTH = 0.55f;
constexpr float COLUMN_HEADER_HEIGHT = 3.75f;
constexpr float COLUMN_HEADER_BOTTOM_GAP = 0.25f;
constexpr float DETAILS_BOTTOM_GAP = 0.3f;
constexpr float TABLE_FRAME_PADDING_Y = 0.15f;
constexpr float OPTION_FRAME_PADDING_Y = 0.22f;
constexpr float MARKER_RADIUS = 1.1f;
constexpr float MARKER_THICKNESS = 0.13f;
constexpr float MARKER_SHADOW_THICKNESS = 0.27f;
constexpr float SMALL_TEXT_SCALE = 0.8f;
constexpr float NO_OBJECTS_ALPHA = 0.45f;
constexpr ImU32 HOVER_FILL_COLOR = IM_COL32(255, 255, 255, 16);
constexpr ImU32 MARKER_COLOR = IM_COL32(255, 255, 255, 240);
constexpr ImU32 MARKER_SHADOW_COLOR = IM_COL32(0, 0, 0, 170);
// The ranges of the sliders.
constexpr float ICON_SIZE_MIN = 0.1f;
constexpr float ICON_SIZE_MAX = 1.0f;
constexpr float ICON_OPACITY_MIN = 0.0f;
constexpr float ICON_OPACITY_MAX = 1.0f;
constexpr float WIREFRAME_THICKNESS_MIN = 0.5f;
constexpr float WIREFRAME_THICKNESS_MAX = 5.0f;

using object_visitor = std::function<void(entt::entity)>;
using count_objects_fn = auto (*)(entt::registry&) -> size_t;
using visit_objects_fn = void (*)(entt::registry&, const object_visitor&);

template<typename... Components>
struct component_pack
{
};

/// Walks the objects holding any of the components. An object holding several of them is seen once, at the first:
/// each view leaves out the components the views before it walked.
template<typename... Earlier, typename Component, typename... Later, typename Visitor>
void walk_objects(entt::registry& registry,
                  component_pack<Earlier...> /*earlier*/,
                  component_pack<Component, Later...> /*remaining*/,
                  Visitor&& visitor)
{
    for(const entt::entity object : registry.view<Component>(entt::exclude<Earlier...>))
    {
        visitor(object);
    }
    if constexpr(sizeof...(Later) > 0)
    {
        walk_objects(registry, component_pack<Earlier..., Component>{}, component_pack<Later...>{}, visitor);
    }
}

template<typename... Components>
auto count_objects(entt::registry& registry) -> size_t
{
    // Walked, because a view of a component with stable pointers has no exact size.
    size_t count = 0;
    const auto add_object = [&count](entt::entity /*object*/)
    {
        ++count;
    };
    walk_objects(registry, component_pack<>{}, component_pack<Components...>{}, add_object);
    return count;
}

template<typename... Components>
void visit_objects(entt::registry& registry, const object_visitor& visitor)
{
    walk_objects(registry, component_pack<>{}, component_pack<Components...>{}, visitor);
}

/// The objects of a row, as plain functions so that the table can stay a constant.
struct object_query
{
    count_objects_fn count;
    visit_objects_fn visit;
};

template<typename... Components>
constexpr object_query OBJECTS{&count_objects<Components...>, &visit_objects<Components...>};

/// A setting under a row, drawn while the row's gizmo is on.
struct detail_option
{
    const char* label;
    const char* tooltip;
    bool gizmos_data::*flag;
};

/// A type of object: a row of the table. A null flag means the type has no icon, or no gizmo.
struct row_desc
{
    const char* icon;
    const char* name;
    object_query objects;
    const char* gizmo_tooltip{nullptr};
    bool billboard_gizmos::*icon_flag{nullptr};
    bool gizmos_data::*gizmo_flag{nullptr};
    std::span<const detail_option> details{};
    bool is_open_by_default{false};
};

constexpr auto EMITTER_DETAILS = std::to_array<detail_option>({
    {"Bounds", "The bounds of the particles.", &gizmos_data::show_particle_emitter_bounds},
    {"Shape", "The shape particles are emitted from.", &gizmos_data::show_particle_emitter_shape},
    {"Direction", "The direction particles leave in.", &gizmos_data::show_particle_emitter_direction},
});

constexpr auto MODEL_DETAILS = std::to_array<detail_option>({
    {"Submesh Bounds", "The bounds of every submesh, colored by its LOD.", &gizmos_data::show_model_submesh_bounds},
});

constexpr auto ROWS = std::to_array<row_desc>({
    {.icon = ICON_MDI_CAMERA,
     .name = "Camera",
     .objects = OBJECTS<camera_component>,
     .gizmo_tooltip = "The camera's frustum.",
     .icon_flag = &billboard_gizmos::show_camera,
     .gizmo_flag = &gizmos_data::show_camera},
    {.icon = ICON_MDI_LIGHTBULB,
     .name = "Light",
     .objects = OBJECTS<light_component>,
     .gizmo_tooltip = "The light's range and cone.",
     .icon_flag = &billboard_gizmos::show_light,
     .gizmo_flag = &gizmos_data::show_light},
    {.icon = ICON_MDI_REFLECT_HORIZONTAL,
     .name = "Reflection Probe",
     .objects = OBJECTS<reflection_probe_component>,
     .gizmo_tooltip = "The probe's capture volume.",
     .icon_flag = &billboard_gizmos::show_reflection_probe,
     .gizmo_flag = &gizmos_data::show_reflection_probe},
    {.icon = ICON_MDI_VOLUME_HIGH,
     .name = "Audio Source",
     .objects = OBJECTS<audio_source_component>,
     .icon_flag = &billboard_gizmos::show_audio_source},
    {.icon = ICON_MDI_CREATION,
     .name = "Particle Emitter",
     .objects = OBJECTS<particle_emitter_component>,
     .gizmo_tooltip = "The emitter's bounds, shape and direction.",
     .icon_flag = &billboard_gizmos::show_particle_emitter,
     .gizmo_flag = &gizmos_data::show_particle_emitter,
     .details = EMITTER_DETAILS},
    {.icon = ICON_MDI_SHAPE,
     .name = "Model",
     .objects = OBJECTS<model_component>,
     .gizmo_tooltip = "The model's world bounds, colored by the current LOD.",
     .gizmo_flag = &gizmos_data::show_model_bounds,
     .details = MODEL_DETAILS,
     .is_open_by_default = true},
    {.icon = ICON_MDI_VIEW_AGENDA,
     .name = "Volume",
     .objects = OBJECTS<volume_component, fog_volume_component>,
     .gizmo_tooltip = "The extents of post-process and fog volumes.",
     .gizmo_flag = &gizmos_data::show_volume},
    {.icon = ICON_MDI_TEXT,
     .name = "Text",
     .objects = OBJECTS<text_component>,
     .gizmo_tooltip = "The text's bounds.",
     .gizmo_flag = &gizmos_data::show_text},
    {.icon = ICON_MDI_ATOM,
     .name = "Physics & Rigs",
     .objects = OBJECTS<physics_component,
                        character_controller_component,
                        two_bone_ik_component,
                        chain_ik_component,
                        look_at_ik_component,
                        foot_ik_component,
                        spring_bone_component>,
     .gizmo_tooltip = "Physics shapes and the physics world's debug lines, character controllers, IK and spring bones.",
     .gizmo_flag = &gizmos_data::show_component_gizmos},
});

static_assert(std::all_of(ROWS.begin(),
                          ROWS.end(),
                          [](const row_desc& row)
                          {
                              return row.details.empty() || row.gizmo_flag != nullptr;
                          }),
              "The details of a row are drawn while its gizmo is on, so the row needs a gizmo.");

static_assert(size_t(std::count_if(ROWS.begin(),
                                   ROWS.end(),
                                   [](const row_desc& row)
                                   {
                                       return row.icon_flag != nullptr;
                                   })) == std::tuple_size_v<billboard_components>,
              "Every component with an icon needs a row.");

auto get_icon_flag(editing_manager& em, const row_desc& row) -> bool*
{
    return row.icon_flag != nullptr ? &(em.billboard_data.*row.icon_flag) : nullptr;
}

auto get_gizmo_flag(editing_manager& em, const row_desc& row) -> bool*
{
    return row.gizmo_flag != nullptr ? &(em.gizmos.*row.gizmo_flag) : nullptr;
}

/// A column of the table: the flag every row holds in it, and how its header reads.
struct column_desc
{
    const char* name;
    const char* scope;
    const char* tooltip;
    bool* (*get_flag)(editing_manager&, const row_desc&);
};

constexpr column_desc ICONS_COLUMN{"Icons",
                                   "all objects",
                                   "Icons draw for every object in the scene. Click to turn the column on or off.",
                                   &get_icon_flag};

constexpr column_desc GIZMOS_COLUMN{"Gizmos",
                                    "selected",
                                    "Gizmos draw for the selected objects. Click to turn the column on or off.",
                                    &get_gizmo_flag};

/// Where the parts of the table's rows sit, in screen coordinates.
struct table_layout
{
    float left;
    float width;
    float name_left;
    float columns_left;
    float icons_center;
    float gizmos_center;
};

auto make_table_layout() -> table_layout
{
    table_layout layout{};
    layout.left = ImGui::GetCursorScreenPos().x;
    layout.width = ImGui::GetContentRegionAvail().x;
    const float right = layout.left + layout.width;
    const float column_width = to_pixels(COLUMN_WIDTH);
    layout.name_left = layout.left + to_pixels(DISCLOSURE_WIDTH + TYPE_ICON_WIDTH);
    layout.columns_left = right - column_width * 2.0f;
    layout.icons_center = right - column_width * 1.5f;
    layout.gizmos_center = right - column_width * 0.5f;
    return layout;
}

/// What the rows of the table draw from.
struct table_context
{
    editing_manager& em;
    scene* scn;
    state& menu_state;
    table_layout layout;
};

void add_small_text(const ImVec2& pos, ImU32 color, const char* text)
{
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * SMALL_TEXT_SCALE, pos, color, text);
}

auto calc_small_text_width(const char* text) -> float
{
    return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * SMALL_TEXT_SCALE, FLT_MAX, 0.0f, text).x;
}

/// Fills the rectangle while the pointer is over it, and returns whether it is. The pointer stays over the rectangle
/// on the items inside it too, so this cannot be the state of one item.
auto draw_hover_fill(const ImVec2& min, const ImVec2& max) -> bool
{
    if(!ImGui::IsWindowHovered() || !ImGui::IsMouseHoveringRect(min, max))
    {
        return false;
    }
    ImGui::GetWindowDrawList()->AddRectFilled(min,
                                              max,
                                              ImGui::GetColorU32(HOVER_FILL_COLOR),
                                              to_pixels(HOVER_FILL_ROUNDING));
    return true;
}

auto is_column_all_on(editing_manager& em, const column_desc& column) -> bool
{
    return std::all_of(ROWS.begin(),
                       ROWS.end(),
                       [&](const row_desc& row)
                       {
                           const bool* flag = column.get_flag(em, row);
                           return flag == nullptr || *flag;
                       });
}

auto is_column_any_on(editing_manager& em, const column_desc& column) -> bool
{
    return std::any_of(ROWS.begin(),
                       ROWS.end(),
                       [&](const row_desc& row)
                       {
                           const bool* flag = column.get_flag(em, row);
                           return flag != nullptr && *flag;
                       });
}

void set_column(editing_manager& em, const column_desc& column, bool is_on)
{
    for(const auto& row : ROWS)
    {
        if(bool* flag = column.get_flag(em, row))
        {
            *flag = is_on;
        }
    }
}

void draw_column_labels(const column_desc& column, float center_x, float top)
{
    ImGui::PushFont(ImGui::Font::Bold);
    const float name_x = ImFloor(center_x - ImGui::CalcTextSize(column.name).x * 0.5f);
    ImGui::GetWindowDrawList()->AddText(ImVec2(name_x, top), ImGui::GetColorU32(ImGuiCol_Text), column.name);
    const float scope_y = top + ImGui::GetTextLineHeight();
    ImGui::PopFont();
    add_small_text(ImVec2(ImFloor(center_x - calc_small_text_width(column.scope) * 0.5f), scope_y),
                   imgui_style::get_muted_text_color_u32(),
                   column.scope);
}

/// A checkbox showing the column: on, off, or mixed while only some of its rows are on. Returns true on the click.
auto draw_column_box(editing_manager& em, const column_desc& column, const ImVec2& pos) -> bool
{
    ImGui::SetCursorScreenPos(pos);
    bool is_on = is_column_all_on(em, column);
    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, !is_on && is_column_any_on(em, column));
    const bool is_pressed = ImGui::Checkbox("##box", &is_on);
    ImGui::PopItemFlag();
    ImGui::SetItemTooltipEx("%s", column.tooltip);
    return is_pressed;
}

/// The name, the scope and the box. A click anywhere on it flips the whole column.
void draw_column_header(editing_manager& em, const column_desc& column, float center_x, float top)
{
    ImGui::PushID(column.name);
    const ImVec2 size(to_pixels(COLUMN_WIDTH), to_pixels(COLUMN_HEADER_HEIGHT));
    const ImVec2 min(ImFloor(center_x - size.x * 0.5f), top);
    ImGui::SetCursorScreenPos(min);
    // The box lies on top of this button and takes the clicks on itself.
    ImGui::SetNextItemAllowOverlap();
    const bool is_header_pressed = ImGui::InvisibleButton("##header", size);
    ImGui::SetItemTooltipEx("%s", column.tooltip);
    draw_hover_fill(min, min + size);
    draw_column_labels(column, center_x, top);
    const float box_size = ImGui::GetFrameHeight();
    const ImVec2 box_pos(ImFloor(center_x - box_size * 0.5f), top + size.y - box_size);
    const bool is_box_pressed = draw_column_box(em, column, box_pos);
    if(is_header_pressed || is_box_pressed)
    {
        set_column(em, column, !is_column_all_on(em, column));
    }
    ImGui::PopID();
}

void draw_table_header(editing_manager& em, const table_layout& layout)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = to_pixels(COLUMN_HEADER_HEIGHT);
    const float small_height = ImGui::GetFontSize() * SMALL_TEXT_SCALE;
    const float hint_y = ImFloor(min.y + height - (ImGui::GetFrameHeight() + small_height) * 0.5f);
    add_small_text(ImVec2(layout.left, hint_y), imgui_style::get_muted_text_color_u32(), "Toggle a whole column");
    draw_column_header(em, ICONS_COLUMN, layout.icons_center, min.y);
    draw_column_header(em, GIZMOS_COLUMN, layout.gizmos_center, min.y);
    // The header as one item of its size, for the layout after it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(layout.width, height + to_pixels(COLUMN_HEADER_BOTTOM_GAP)));
    ImGui::Separator();
}

/// Turns on the row's icon and gizmo and turns off those of every other row.
void show_only_row(editing_manager& em, const row_desc& shown)
{
    for(const auto& row : ROWS)
    {
        const bool is_shown = &row == &shown;
        if(bool* flag = get_icon_flag(em, row))
        {
            *flag = is_shown;
        }
        if(bool* flag = get_gizmo_flag(em, row))
        {
            *flag = is_shown;
        }
    }
}

/// The checkbox of a cell, or a dash where the type has no such flag.
void draw_cell(bool* flag, const char* id, const char* tooltip, float center_x, float center_y)
{
    if(flag == nullptr)
    {
        const float half_width = ImFloor(to_pixels(NO_FLAG_MARK_WIDTH) * 0.5f);
        const float y = ImFloor(center_y);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(center_x - half_width, y),
                                            ImVec2(center_x + half_width, y),
                                            imgui_style::get_muted_text_color_u32());
        return;
    }
    const float half_size = ImGui::GetFrameHeight() * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(ImFloor(center_x - half_size), ImFloor(center_y - half_size)));
    ImGui::Checkbox(id, flag);
    if(tooltip != nullptr)
    {
        ImGui::SetItemTooltipEx("%s", tooltip);
    }
}

/// The caret that folds the details of a row. Returns whether they are open.
auto draw_disclosure(const row_desc& row, const ImVec2& min, float height) -> bool
{
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("##is_open");
    bool is_open = storage->GetBool(key, row.is_open_by_default);
    const ImVec2 size(to_pixels(DISCLOSURE_WIDTH), height);
    ImGui::SetCursorScreenPos(min);
    if(ImGui::InvisibleButton("##disclosure", size))
    {
        is_open = !is_open;
        storage->SetBool(key, is_open);
    }
    const ImU32 color = ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_Text)
                                               : imgui_style::get_muted_text_color_u32();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              min + size * 0.5f,
                              is_open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT,
                              color);
    return is_open;
}

/// The button under a row's label, so that the label takes the Alt+click and the tooltip.
void draw_row_label_button(const table_context& table, const row_desc& row, size_t count, float top)
{
    const float left = table.layout.left + to_pixels(DISCLOSURE_WIDTH);
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    const ImVec2 size(table.layout.columns_left - left, to_pixels(ROW_HEIGHT));
    if(ImGui::InvisibleButton("##label", size) && ImGui::GetIO().KeyAlt)
    {
        show_only_row(table.em, row);
    }
    ImGui::SetItemTooltipEx("%s",
                            count > 0 ? "Alt+click to show only this type." : "No objects of this type in the scene.");
}

void draw_row_label(const row_desc& row, size_t count, const table_layout& layout, float center_y)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float alpha = count > 0 ? 1.0f : NO_OBJECTS_ALPHA;
    ImVec4 muted = imgui_style::get_muted_text_color();
    muted.w *= alpha;
    const float icon_x = layout.left + to_pixels(DISCLOSURE_WIDTH + TYPE_ICON_WIDTH * 0.5f);
    ImGui::RenderIconCentered(draw_list, ImVec2(icon_x, center_y), row.icon, ImGui::GetColorU32(muted));
    const float text_y = ImFloor(center_y - ImGui::GetTextLineHeight() * 0.5f);
    draw_list->AddText(ImVec2(layout.name_left, text_y), ImGui::GetColorU32(ImGuiCol_Text, alpha), row.name);
    const std::string count_text = std::to_string(count);
    const float count_x = layout.name_left + ImGui::CalcTextSize(row.name).x + to_pixels(COUNT_GAP);
    const float small_height = ImGui::GetFontSize() * SMALL_TEXT_SCALE;
    add_small_text(ImVec2(count_x, ImFloor(center_y - small_height * 0.5f)),
                   ImGui::GetColorU32(muted),
                   count_text.c_str());
}

void draw_row_details(editing_manager& em, const row_desc& row, const table_layout& layout)
{
    ImGui::BeginDisabled(!*get_gizmo_flag(em, row));
    ImGui::SetCursorScreenPos(ImVec2(layout.name_left, ImGui::GetCursorScreenPos().y));
    bool is_first = true;
    for(const auto& detail : row.details)
    {
        if(!is_first)
        {
            ImGui::SameLine();
        }
        is_first = false;
        ImGui::Checkbox(detail.label, &(em.gizmos.*detail.flag));
        ImGui::SetItemTooltipEx("%s", detail.tooltip);
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0.0f, to_pixels(DETAILS_BOTTOM_GAP)));
}

void draw_row(const table_context& table, size_t index)
{
    const row_desc& row = ROWS[index];
    ImGui::PushID(row.name);
    const ImVec2 min(table.layout.left, ImGui::GetCursorScreenPos().y);
    const ImVec2 size(table.layout.width, to_pixels(ROW_HEIGHT));
    const float center_y = min.y + size.y * 0.5f;
    if(draw_hover_fill(min, min + size) && table.em.show_gizmos)
    {
        table.menu_state.hovered_row = index;
    }
    const bool is_open = !row.details.empty() && draw_disclosure(row, min, size.y);
    const size_t count = table.scn != nullptr ? row.objects.count(*table.scn->registry) : 0;
    draw_row_label_button(table, row, count, min.y);
    draw_row_label(row, count, table.layout, center_y);
    draw_cell(get_icon_flag(table.em, row), "##icon", nullptr, table.layout.icons_center, center_y);
    draw_cell(get_gizmo_flag(table.em, row), "##gizmo", row.gizmo_tooltip, table.layout.gizmos_center, center_y);
    // The row as one item of its size, for the layout after it.
    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(size);
    if(is_open)
    {
        draw_row_details(table.em, row, table.layout);
    }
    ImGui::PopID();
}

void draw_table(rtti::context& ctx, editing_manager& em, state& menu_state)
{
    const table_context table{em, em.get_active_scene(ctx), menu_state, make_table_layout()};
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    draw_table_header(em, table.layout);
    for(size_t index = 0; index < ROWS.size(); ++index)
    {
        draw_row(table, index);
    }
    ImGui::PopStyleVar();
}

void draw_icon_appearance(editing_manager& em)
{
    ImGui::Separator();
    visualization_widgets::draw_section_header("Icon Appearance");
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float half_width = ImFloor((ImGui::GetContentRegionAvail().x - spacing) * 0.5f);
    ImGui::SetNextItemWidth(half_width);
    ImGui::KnobSliderScalarT("##icon_size", &em.billboard_data.size, ICON_SIZE_MIN, ICON_SIZE_MAX, "Size %.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::KnobSliderScalarT("##icon_opacity",
                             &em.billboard_data.opacity,
                             ICON_OPACITY_MIN,
                             ICON_OPACITY_MAX,
                             "Opacity %.2f");
    ImGui::Checkbox("Hide Behind Objects", &em.billboard_data.depth_aware);
    ImGui::SetItemTooltipEx("%s", "Hide icons behind geometry. Off draws them on top of everything.");
}

void draw_selection_options(editing_manager& em)
{
    ImGui::Separator();
    visualization_widgets::draw_section_header("Selection");
    const float half_width = ImFloor(ImGui::GetContentRegionAvail().x * 0.5f);
    const float left = ImGui::GetCursorPosX();
    ImGui::Checkbox("Outline", &em.gizmos.show_selection_outline);
    ImGui::SetItemTooltipEx("%s", "Outline the selected objects.");
    ImGui::SameLine(left + half_width);
    ImGui::Checkbox("Wireframe", &em.gizmos.show_selection_wireframe);
    ImGui::SetItemTooltipEx("%s", "Draw a vertex-pulling wireframe overlay on top of the selected entity's mesh.");
    if(!em.gizmos.show_selection_wireframe)
    {
        return;
    }
    ImGui::ColorEdit4("##wireframe_color",
                      math::value_ptr(em.gizmos.selection_wireframe_color),
                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::KnobSliderScalarT("##wireframe_thickness",
                             &em.gizmos.selection_wireframe_thickness,
                             WIREFRAME_THICKNESS_MIN,
                             WIREFRAME_THICKNESS_MAX,
                             "Thickness %.2f px");
}

auto is_at_defaults(const editing_manager& em) -> bool
{
    return em.get_gizmo_setup() == gizmo_setup{};
}

/// The master switch, the title and the reset button at the other end of the line.
void draw_header(editing_manager& em)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float right = min.x + ImGui::GetContentRegionAvail().x;
    visualization_widgets::draw_switch("##show_gizmos", em.show_gizmos);
    ImGui::SetItemTooltipEx("%s", "Show or hide every icon and gizmo.");
    const float title_x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::SetCursorScreenPos(ImVec2(title_x, min.y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted("Gizmos");
    ImGui::PopFont();
    const char* reset_text = ICON_MDI_RESTORE " Reset";
    const float reset_width = ImGui::CalcTextSize(reset_text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorScreenPos(ImVec2(right - reset_width, min.y));
    ImGui::BeginDisabled(is_at_defaults(em));
    if(ImGui::Button(reset_text))
    {
        em.apply_gizmo_setup({});
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltipEx("%s", "Back to the default icons and gizmos, with the master switch on.");
}

void draw_popup(rtti::context& ctx, editing_manager& em, state& menu_state)
{
    draw_header(em);
    ImGui::BeginDisabled(!em.show_gizmos);
    visualization_widgets::push_field_style();
    const float padding_x = ImGui::GetStyle().FramePadding.x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, to_pixels(TABLE_FRAME_PADDING_Y)));
    draw_table(ctx, em, menu_state);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding_x, to_pixels(OPTION_FRAME_PADDING_Y)));
    draw_icon_appearance(em);
    draw_selection_options(em);
    ImGui::PopStyleVar();
    visualization_widgets::pop_field_style();
    ImGui::EndDisabled();
}

void draw_settings_dropdown(rtti::context& ctx, editing_manager& em, state& menu_state, float popup_max_height)
{
    const bool is_changed = !is_at_defaults(em);
    const ImU32 tint = is_changed ? imgui_style::get_changed_value_color_u32() : 0;
    const char* tooltip = is_changed ? "Gizmos Settings - changed from the defaults" : "Gizmos Settings";
    ImGui::SetNextWindowSize(ImVec2(to_pixels(POPUP_WIDTH), 0.0f));
    ImGui::SetNextWindowSizeConstraints({}, {FLT_MAX, popup_max_height});
    if(!panel_toolbar::begin_dropdown("##gizmos_settings", nullptr, tooltip, tint))
    {
        return;
    }
    draw_popup(ctx, em, menu_state);
    panel_toolbar::end_dropdown();
}

void draw_marker(const ImVec2& center)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const float radius = to_pixels(MARKER_RADIUS);
    draw_list->AddCircle(center, radius, MARKER_SHADOW_COLOR, 0, to_pixels(MARKER_SHADOW_THICKNESS));
    draw_list->AddCircle(center, radius, MARKER_COLOR, 0, ImMax(1.0f, to_pixels(MARKER_THICKNESS)));
}

} // namespace

void draw_toolbar_controls(rtti::context& ctx, editing_manager& em, state& menu_state, float popup_max_height)
{
    const gizmo_setup setup_before = em.get_gizmo_setup();
    if(panel_toolbar::toggle("##gizmos", ICON_MDI_SELECTION_MARKER, em.show_gizmos, "Show / Hide all icons and gizmos"))
    {
        em.show_gizmos = !em.show_gizmos;
    }
    draw_settings_dropdown(ctx, em, menu_state, popup_max_height);
    // Only a change made here becomes the project's setting, field by field; what a tool changed for a capture ends
    // with the session.
    const gizmo_setup setup = em.get_gizmo_setup();
    if(setup != setup_before)
    {
        auto& saved = ctx.get_cached<project_manager>().get_project_editor_settings().gizmos;
        saved = merge_gizmo_change(saved, setup_before, setup);
    }
}

void draw_find_markers(rtti::context& ctx,
                       state& menu_state,
                       const camera& cam,
                       const ImVec2& image_min,
                       const ImVec2& image_max)
{
    if(!menu_state.hovered_row)
    {
        return;
    }
    const row_desc& row = ROWS[*menu_state.hovered_row];
    // Taken here and not reset by the toolbar, so that a bar which stops drawing cannot leave rings on screen.
    menu_state.hovered_row.reset();
    scene* scn = ctx.get_cached<editing_manager>().get_active_scene(ctx);
    if(scn == nullptr)
    {
        return;
    }
    const math::frustum& frustum = cam.get_frustum();
    // The camera's viewport starts at the image, but a forced render size makes it larger than the image showing it.
    const usize32_t& viewport_size = cam.get_viewport_size();
    const ImVec2 scale((image_max.x - image_min.x) / float(viewport_size.width),
                       (image_max.y - image_min.y) / float(viewport_size.height));
    const auto mark_object = [&](entt::entity object)
    {
        const auto* transform = scn->registry->try_get<transform_component>(object);
        if(transform == nullptr || !frustum.test_point(transform->get_position_global()))
        {
            return;
        }
        const math::vec3 point = cam.world_to_viewport(transform->get_position_global());
        draw_marker(ImVec2(image_min.x + (point.x - image_min.x) * scale.x,
                           image_min.y + (point.y - image_min.y) * scale.y));
    };
    // By reference, so that the visitor does not allocate.
    row.objects.visit(*scn->registry, std::ref(mark_object));
}

} // namespace unravel::gizmos_menu
