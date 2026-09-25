#include "imgui_style.h"
#include <imgui/imgui.h>

namespace imgui_style
{
namespace
{
ImVec4 g_accent_color{0.26f, 0.59f, 0.98f, 1.0f};
} // namespace

auto get_accent_color() -> ImVec4
{
    return g_accent_color;
}

namespace
{
/// How far the panels mute their secondary text.
constexpr float MUTED_TEXT_ALPHA = 0.55f;
} // namespace

auto get_muted_text_color() -> ImVec4
{
    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    color.w *= MUTED_TEXT_ALPHA;
    return color;
}

auto get_muted_text_color_u32() -> ImU32
{
    return ImGui::GetColorU32(ImGuiCol_Text, MUTED_TEXT_ALPHA);
}

namespace
{
//-----------------------------------------------------------------------------
/// <summary>
/// The colors a theme is made of. apply_palette() lays them onto the ImGui colors the same way
/// for every theme, so the themes differ in hue and weight but never in which color marks what.
/// From dark to light the greys go border, chrome, field, popup, window, panel, header, button.
/// </summary>
//-----------------------------------------------------------------------------
struct theme_palette
{
    /// Tab strips, menu bar, title bars, empty dock space.
    ImColor chrome;
    /// Window body; the selected tab takes it too.
    ImColor window;
    /// Panels inside a window, table headers.
    ImColor panel;
    /// Foldout headers.
    ImColor header;
    /// Menus and popups, a shade under the window.
    ImColor popup;
    /// Input fields, darker than the window they sit on.
    ImColor field;
    ImColor field_hovered;
    ImColor field_active;
    ImColor button;
    ImColor button_hovered;
    ImColor button_pressed;
    /// Thin marks: check marks, slider grabs, the line over the focused tab.
    ImColor accent;
    /// The marks under the mouse, links, drop targets, the navigation cursor.
    ImColor accent_hovered;
    /// Translucent fill of selections and active items, and the accent the editor's own widgets
    /// take (imgui_style::get_accent_color()). It stays deep: large areas are painted with it, and
    /// panel_toolbar puts white text on it as long as its color is not a light one.
    ImColor accent_fill;
    /// Faint accent of hovered headers and resize grips.
    ImColor accent_tint;
    ImColor text;
    ImColor text_muted;
    /// Window, frame and separator lines.
    ImColor border;
    /// Separators under the mouse, inner table lines, the line over the selected tab of an
    /// unfocused window.
    ImColor border_light;
    ImColor scrollbar_grab;
    ImColor scrollbar_grab_hovered;
    ImColor scrollbar_grab_active;
};

//-----------------------------------------------------------------------------
/// <summary>
/// The sizes and shapes a theme sets. Every theme sets all of them, so none depends on the theme
/// before it. The defaults are Unravel Dark's, and the other themes list what they change: the
/// shapes and sizes follow the panel toolbars, soft corners, controls about 1.7 font sizes tall,
/// room between them. Docked windows stay square; whatever floats is rounded.
/// </summary>
//-----------------------------------------------------------------------------
struct theme_metrics
{
    ImVec2 window_padding{8.0f, 8.0f};
    ImVec2 frame_padding{8.0f, 5.0f};
    ImVec2 cell_padding{5.0f, 3.0f};
    ImVec2 item_spacing{6.0f, 4.0f};
    ImVec2 item_inner_spacing{5.0f, 4.0f};
    ImVec2 separator_text_padding{12.0f, 4.0f};
    float indent_spacing{16.0f};
    float scrollbar_size{12.0f};
    float grab_min_size{10.0f};
    float window_rounding{0.0f};
    float child_rounding{5.0f};
    float frame_rounding{4.0f};
    float popup_rounding{7.0f};
    float scrollbar_rounding{6.0f};
    float grab_rounding{4.0f};
    float tab_rounding{4.0f};
    float window_border_size{1.0f};
    float child_border_size{1.0f};
    float popup_border_size{1.0f};
    float frame_border_size{0.0f};
    float tab_border_size{0.0f};
    float tab_bar_border_size{1.0f};
    float tab_bar_overline_size{2.0f};
    float separator_text_border_size{1.0f};
    ImGuiDir window_menu_button_position{ImGuiDir_None};
};

/// A theme as the Style window offers it.
struct theme_desc
{
    const char* name{};
    theme_palette palette{};
    theme_metrics metrics{};
};

/// Faint white on every other table row.
constexpr float TABLE_ROW_ALT_ALPHA = 0.02f;
/// How far a modal darkens what is behind it.
constexpr float MODAL_DIM_ALPHA = 0.65f;

/// The greys and the blue of Unity's dark (Pro) skin, at the sizes of the panel toolbars.
constexpr theme_desc UNRAVEL_DARK_THEME{
    .name = "Unravel Dark",
    .palette = {
        .chrome = ImColor(40, 40, 40),       // #282828
        .window = ImColor(56, 56, 56),       // #383838
        .panel = ImColor(62, 62, 62),        // #3E3E3E
        .header = ImColor(72, 72, 72),
        .popup = ImColor(46, 46, 46),
        .field = ImColor(42, 42, 42),        // #2A2A2A
        .field_hovered = ImColor(48, 48, 48),
        .field_active = ImColor(54, 54, 54),
        .button = ImColor(88, 88, 88),       // #585858
        .button_hovered = ImColor(103, 103, 103), // #676767
        .button_pressed = ImColor(70, 96, 124),   // #46607C
        .accent = ImColor(58, 121, 187),     // #3A79BB
        .accent_hovered = ImColor(78, 141, 207),
        // Over the dark toolbar cards it lands on Unity's selection blue (#2C5D87).
        .accent_fill = ImColor(58, 121, 187, 180),
        .accent_tint = ImColor(58, 121, 187, 60),
        .text = ImColor(210, 210, 210),      // #D2D2D2
        .text_muted = ImColor(128, 128, 128),
        .border = ImColor(35, 35, 35),       // #232323
        .border_light = ImColor(80, 80, 80),
        .scrollbar_grab = ImColor(95, 95, 95), // #5F5F5F
        .scrollbar_grab_hovered = ImColor(110, 110, 110),
        .scrollbar_grab_active = ImColor(125, 125, 125),
    },
};

/// The greys and the orange of the Photoshop style by Derydoca (ImThemes): tight controls, square
/// tabs, and buttons that stay see-through, only lighter than the panel they sit on.
constexpr theme_desc GRAPHITE_THEME{
    .name = "Graphite",
    .palette = {
        .chrome = ImColor(24, 24, 24),
        .window = ImColor(45, 45, 45),
        .panel = ImColor(49, 49, 49),
        .header = ImColor(62, 62, 62),
        .popup = ImColor(38, 38, 38),
        .field = ImColor(34, 34, 34),
        .field_hovered = ImColor(40, 40, 40),
        .field_active = ImColor(47, 47, 47),
        .button = ImColor(255, 255, 255, 20),
        .button_hovered = ImColor(255, 255, 255, 40),
        .button_pressed = ImColor(255, 255, 255, 64),
        .accent = ImColor(222, 112, 42),
        .accent_hovered = ImColor(240, 138, 70),
        .accent_fill = ImColor(196, 94, 30, 150),
        .accent_tint = ImColor(222, 112, 42, 50),
        .text = ImColor(224, 224, 224),
        .text_muted = ImColor(127, 127, 127),
        .border = ImColor(28, 28, 28),
        .border_light = ImColor(67, 67, 67),
        .scrollbar_grab = ImColor(70, 70, 70),
        .scrollbar_grab_hovered = ImColor(90, 90, 90),
        .scrollbar_grab_active = ImColor(110, 110, 110),
    },
    .metrics = {
        .frame_padding = ImVec2(4.0f, 3.0f),
        .cell_padding = ImVec2(4.0f, 2.0f),
        .item_spacing = ImVec2(8.0f, 4.0f),
        .item_inner_spacing = ImVec2(4.0f, 4.0f),
        .indent_spacing = 21.0f,
        .scrollbar_size = 13.0f,
        .grab_min_size = 7.0f,
        .window_rounding = 4.0f,
        .child_rounding = 4.0f,
        .frame_rounding = 2.0f,
        .popup_rounding = 2.0f,
        .scrollbar_rounding = 12.0f,
        .grab_rounding = 0.0f,
        .tab_rounding = 0.0f,
        .frame_border_size = 1.0f,
        .tab_border_size = 1.0f,
        .window_menu_button_position = ImGuiDir_Left,
    },
};

/// Charcoal with near black fields and a golden accent, after the Hazel engine's editor.
constexpr theme_desc CHARCOAL_THEME{
    .name = "Charcoal",
    .palette = {
        .chrome = ImColor(21, 21, 21),
        .window = ImColor(36, 36, 36),
        .panel = ImColor(40, 40, 40),
        .header = ImColor(47, 47, 47),
        .popup = ImColor(30, 30, 30),
        .field = ImColor(15, 15, 15),
        .field_hovered = ImColor(21, 21, 21),
        .field_active = ImColor(27, 27, 27),
        .button = ImColor(56, 56, 56),
        .button_hovered = ImColor(70, 70, 70),
        .button_pressed = ImColor(84, 68, 44),
        .accent = ImColor(220, 166, 72),
        .accent_hovered = ImColor(236, 188, 100),
        .accent_fill = ImColor(176, 130, 52, 140),
        .accent_tint = ImColor(220, 166, 72, 45),
        .text = ImColor(230, 230, 230),
        .text_muted = ImColor(128, 128, 128),
        .border = ImColor(26, 26, 26),
        .border_light = ImColor(60, 60, 60),
        .scrollbar_grab = ImColor(79, 79, 79),
        .scrollbar_grab_hovered = ImColor(104, 104, 104),
        .scrollbar_grab_active = ImColor(130, 130, 130),
    },
    .metrics = {
        .indent_spacing = 11.0f,
        .frame_rounding = 2.5f,
        .frame_border_size = 1.0f,
    },
};

/// Slate greys with a teal accent, and roomy controls.
constexpr theme_desc SLATE_THEME{
    .name = "Slate",
    .palette = {
        .chrome = ImColor(17, 21, 27),
        .window = ImColor(26, 31, 39),
        .panel = ImColor(30, 36, 45),
        .header = ImColor(40, 48, 60),
        .popup = ImColor(21, 26, 33),
        .field = ImColor(18, 22, 28),
        .field_hovered = ImColor(23, 28, 35),
        .field_active = ImColor(28, 34, 42),
        .button = ImColor(50, 60, 74),
        .button_hovered = ImColor(62, 74, 91),
        .button_pressed = ImColor(34, 94, 98),
        .accent = ImColor(52, 160, 150),
        .accent_hovered = ImColor(78, 184, 174),
        .accent_fill = ImColor(52, 160, 150, 140),
        .accent_tint = ImColor(52, 160, 150, 50),
        .text = ImColor(218, 224, 230),
        .text_muted = ImColor(130, 140, 152),
        .border = ImColor(12, 15, 20),
        .border_light = ImColor(54, 64, 78),
        .scrollbar_grab = ImColor(58, 69, 84),
        .scrollbar_grab_hovered = ImColor(72, 85, 102),
        .scrollbar_grab_active = ImColor(88, 102, 120),
    },
    .metrics = {
        .window_padding = ImVec2(12.0f, 12.0f),
        .frame_padding = ImVec2(10.0f, 6.0f),
        .cell_padding = ImVec2(8.0f, 4.0f),
        .item_spacing = ImVec2(8.0f, 6.0f),
        .item_inner_spacing = ImVec2(6.0f, 4.0f),
        .indent_spacing = 20.0f,
        .scrollbar_size = 16.0f,
        .grab_min_size = 12.0f,
        .window_rounding = 6.0f,
        .child_rounding = 4.0f,
        .popup_rounding = 6.0f,
        .scrollbar_rounding = 8.0f,
        .frame_border_size = 1.0f,
    },
};

/// Navy greys with a sky blue accent.
constexpr theme_desc MIDNIGHT_THEME{
    .name = "Midnight",
    .palette = {
        .chrome = ImColor(16, 21, 30),
        .window = ImColor(24, 32, 45),
        .panel = ImColor(27, 36, 50),
        .header = ImColor(36, 48, 66),
        .popup = ImColor(20, 27, 38),
        .field = ImColor(17, 23, 33),
        .field_hovered = ImColor(21, 28, 40),
        .field_active = ImColor(25, 33, 47),
        .button = ImColor(46, 60, 80),
        .button_hovered = ImColor(58, 75, 99),
        .button_pressed = ImColor(30, 80, 120),
        .accent = ImColor(42, 146, 206),
        .accent_hovered = ImColor(78, 170, 226),
        .accent_fill = ImColor(42, 146, 206, 150),
        .accent_tint = ImColor(42, 146, 206, 55),
        .text = ImColor(214, 224, 236),
        .text_muted = ImColor(122, 136, 156),
        .border = ImColor(10, 14, 21),
        .border_light = ImColor(48, 62, 82),
        .scrollbar_grab = ImColor(52, 67, 88),
        .scrollbar_grab_hovered = ImColor(64, 82, 107),
        .scrollbar_grab_active = ImColor(78, 98, 126),
    },
    .metrics = {
        .window_padding = ImVec2(11.0f, 11.0f),
        .frame_padding = ImVec2(9.0f, 5.0f),
        .cell_padding = ImVec2(7.0f, 4.0f),
        .item_spacing = ImVec2(7.0f, 5.0f),
        .indent_spacing = 18.0f,
        .window_rounding = 6.0f,
        .child_rounding = 4.0f,
        .popup_rounding = 6.0f,
        .scrollbar_rounding = 8.0f,
        .frame_border_size = 1.0f,
    },
};

/// Violet greys with a lavender accent, and round, roomy controls.
constexpr theme_desc AMETHYST_THEME{
    .name = "Amethyst",
    .palette = {
        .chrome = ImColor(22, 17, 28),
        .window = ImColor(32, 25, 40),
        .panel = ImColor(36, 28, 45),
        .header = ImColor(47, 37, 58),
        .popup = ImColor(27, 21, 34),
        .field = ImColor(23, 18, 29),
        .field_hovered = ImColor(28, 22, 35),
        .field_active = ImColor(33, 26, 41),
        .button = ImColor(61, 49, 75),
        .button_hovered = ImColor(75, 61, 92),
        .button_pressed = ImColor(80, 60, 122),
        .accent = ImColor(140, 104, 212),
        .accent_hovered = ImColor(162, 130, 230),
        .accent_fill = ImColor(140, 104, 212, 150),
        .accent_tint = ImColor(140, 104, 212, 55),
        .text = ImColor(226, 220, 236),
        .text_muted = ImColor(138, 128, 152),
        .border = ImColor(14, 10, 18),
        .border_light = ImColor(64, 52, 78),
        .scrollbar_grab = ImColor(70, 58, 86),
        .scrollbar_grab_hovered = ImColor(84, 70, 102),
        .scrollbar_grab_active = ImColor(100, 84, 120),
    },
    .metrics = {
        .window_padding = ImVec2(12.0f, 12.0f),
        .frame_padding = ImVec2(10.0f, 6.0f),
        .cell_padding = ImVec2(8.0f, 4.0f),
        .item_spacing = ImVec2(8.0f, 6.0f),
        .item_inner_spacing = ImVec2(6.0f, 4.0f),
        .indent_spacing = 18.0f,
        .window_rounding = 8.0f,
        .child_rounding = 6.0f,
        .frame_rounding = 6.0f,
        .popup_rounding = 8.0f,
        .scrollbar_rounding = 10.0f,
        .grab_rounding = 6.0f,
        .tab_rounding = 6.0f,
        .frame_border_size = 1.0f,
    },
};

/// Nearly neutral greys with a leaf green accent, and tight corners.
constexpr theme_desc SAGE_THEME{
    .name = "Sage",
    .palette = {
        .chrome = ImColor(20, 22, 20),
        .window = ImColor(30, 32, 30),
        .panel = ImColor(34, 36, 34),
        .header = ImColor(44, 47, 44),
        .popup = ImColor(25, 27, 25),
        .field = ImColor(21, 23, 21),
        .field_hovered = ImColor(26, 28, 26),
        .field_active = ImColor(31, 33, 31),
        .button = ImColor(56, 59, 56),
        .button_hovered = ImColor(69, 73, 69),
        .button_pressed = ImColor(50, 88, 56),
        .accent = ImColor(72, 152, 82),
        .accent_hovered = ImColor(96, 176, 104),
        .accent_fill = ImColor(72, 152, 82, 140),
        .accent_tint = ImColor(72, 152, 82, 50),
        .text = ImColor(218, 222, 218),
        .text_muted = ImColor(126, 130, 126),
        .border = ImColor(14, 16, 14),
        .border_light = ImColor(58, 62, 58),
        .scrollbar_grab = ImColor(64, 68, 64),
        .scrollbar_grab_hovered = ImColor(78, 82, 78),
        .scrollbar_grab_active = ImColor(94, 98, 94),
    },
    .metrics = {
        .frame_padding = ImVec2(8.0f, 4.0f),
        .cell_padding = ImVec2(6.0f, 3.0f),
        .item_inner_spacing = ImVec2(4.0f, 3.0f),
        .window_rounding = 2.0f,
        .child_rounding = 1.0f,
        .frame_rounding = 2.0f,
        .popup_rounding = 2.0f,
        .scrollbar_rounding = 4.0f,
        .grab_rounding = 2.0f,
        .tab_rounding = 1.0f,
        .frame_border_size = 1.0f,
    },
};

/// Brown greys with an amber accent. Amber is a bright hue, so its fill is a darker, thinner
/// amber than the marks: at full strength it glares from every active button.
constexpr theme_desc AMBER_THEME{
    .name = "Amber",
    .palette = {
        .chrome = ImColor(27, 22, 17),
        .window = ImColor(38, 31, 24),
        .panel = ImColor(42, 34, 26),
        .header = ImColor(54, 44, 34),
        .popup = ImColor(32, 26, 20),
        .field = ImColor(28, 23, 18),
        .field_hovered = ImColor(34, 28, 22),
        .field_active = ImColor(40, 33, 26),
        .button = ImColor(68, 55, 42),
        .button_hovered = ImColor(82, 67, 51),
        .button_pressed = ImColor(100, 72, 36),
        .accent = ImColor(212, 146, 58),
        .accent_hovered = ImColor(232, 168, 84),
        .accent_fill = ImColor(190, 128, 46, 140),
        .accent_tint = ImColor(212, 146, 58, 50),
        .text = ImColor(226, 216, 202),
        .text_muted = ImColor(150, 138, 122),
        .border = ImColor(20, 16, 12),
        .border_light = ImColor(72, 60, 46),
        .scrollbar_grab = ImColor(78, 64, 49),
        .scrollbar_grab_hovered = ImColor(94, 78, 60),
        .scrollbar_grab_active = ImColor(110, 92, 71),
    },
    .metrics = {
        .window_padding = ImVec2(10.0f, 10.0f),
        .frame_padding = ImVec2(9.0f, 5.0f),
        .cell_padding = ImVec2(7.0f, 4.0f),
        .item_spacing = ImVec2(7.0f, 5.0f),
        .indent_spacing = 17.0f,
        .window_rounding = 4.0f,
        .child_rounding = 3.0f,
        .frame_rounding = 3.0f,
        .popup_rounding = 4.0f,
        .grab_rounding = 3.0f,
        .tab_rounding = 3.0f,
        .frame_border_size = 1.0f,
    },
};

/// Neutral greys with a deep red accent, and compact controls.
constexpr theme_desc CRIMSON_THEME{
    .name = "Crimson",
    .palette = {
        .chrome = ImColor(16, 16, 16),
        .window = ImColor(38, 38, 38),
        .panel = ImColor(42, 42, 42),
        .header = ImColor(52, 52, 52),
        .popup = ImColor(28, 28, 28),
        .field = ImColor(24, 24, 24),
        .field_hovered = ImColor(30, 30, 30),
        .field_active = ImColor(36, 36, 36),
        .button = ImColor(59, 59, 59),
        .button_hovered = ImColor(72, 72, 72),
        .button_pressed = ImColor(108, 44, 44),
        .accent = ImColor(184, 66, 66),
        .accent_hovered = ImColor(208, 94, 94),
        .accent_fill = ImColor(150, 48, 48, 170),
        .accent_tint = ImColor(184, 66, 66, 50),
        .text = ImColor(224, 224, 224),
        .text_muted = ImColor(128, 128, 128),
        .border = ImColor(8, 8, 8),
        .border_light = ImColor(58, 58, 58),
        .scrollbar_grab = ImColor(64, 64, 64),
        .scrollbar_grab_hovered = ImColor(82, 82, 82),
        .scrollbar_grab_active = ImColor(100, 100, 100),
    },
    .metrics = {
        .frame_padding = ImVec2(8.0f, 2.0f),
        .cell_padding = ImVec2(9.0f, 2.0f),
        .frame_rounding = 3.0f,
        .popup_rounding = 3.0f,
        .tab_rounding = 3.0f,
        .frame_border_size = 1.0f,
    },
};

/// Every theme once. With no default case, the compiler flags a theme added to the enum only.
auto get_theme_desc(theme value) -> const theme_desc&
{
    switch(value)
    {
        case theme::graphite:
            return GRAPHITE_THEME;
        case theme::charcoal:
            return CHARCOAL_THEME;
        case theme::slate:
            return SLATE_THEME;
        case theme::midnight:
            return MIDNIGHT_THEME;
        case theme::amethyst:
            return AMETHYST_THEME;
        case theme::sage:
            return SAGE_THEME;
        case theme::amber:
            return AMBER_THEME;
        case theme::crimson:
            return CRIMSON_THEME;
        case theme::unravel_dark:
        case theme::count:
            break;
    }
    return UNRAVEL_DARK_THEME;
}

/// Replaces every ImGui color with the palette's, and makes its fill the editor's accent.
void apply_palette(const theme_palette& palette)
{
    // From the stock colors, so nothing the previous theme set survives the switch.
    ImGui::StyleColorsDark();
    ImVec4* colors = ImGui::GetStyle().Colors;
    const ImVec4 clear{0.0f, 0.0f, 0.0f, 0.0f};
    colors[ImGuiCol_WindowBg] = palette.window;
    colors[ImGuiCol_ChildBg] = palette.panel;
    colors[ImGuiCol_PopupBg] = palette.popup;
    colors[ImGuiCol_Border] = palette.border;
    colors[ImGuiCol_BorderShadow] = clear;
    colors[ImGuiCol_MenuBarBg] = palette.chrome;
    colors[ImGuiCol_Text] = palette.text;
    colors[ImGuiCol_TextDisabled] = palette.text_muted;
    colors[ImGuiCol_TextSelectedBg] = palette.accent_fill;
    colors[ImGuiCol_TextLink] = palette.accent_hovered;
    colors[ImGuiCol_Button] = palette.button;
    colors[ImGuiCol_ButtonHovered] = palette.button_hovered;
    colors[ImGuiCol_ButtonActive] = palette.button_pressed;
    colors[ImGuiCol_Header] = palette.header;
    colors[ImGuiCol_HeaderHovered] = palette.accent_tint;
    colors[ImGuiCol_HeaderActive] = palette.accent_fill;
    colors[ImGuiCol_FrameBg] = palette.field;
    colors[ImGuiCol_FrameBgHovered] = palette.field_hovered;
    colors[ImGuiCol_FrameBgActive] = palette.field_active;
    colors[ImGuiCol_TitleBg] = palette.chrome;
    colors[ImGuiCol_TitleBgActive] = palette.chrome;
    colors[ImGuiCol_TitleBgCollapsed] = palette.chrome;
    // The selected tab takes the color of the window it belongs to, and the accent line on top
    // tells which window has the focus. The others sink into the strip.
    colors[ImGuiCol_Tab] = palette.chrome;
    colors[ImGuiCol_TabHovered] = palette.field_hovered;
    colors[ImGuiCol_TabSelected] = palette.window;
    colors[ImGuiCol_TabSelectedOverline] = palette.accent;
    colors[ImGuiCol_TabDimmed] = palette.chrome;
    colors[ImGuiCol_TabDimmedSelected] = palette.window;
    colors[ImGuiCol_TabDimmedSelectedOverline] = palette.border_light;
    colors[ImGuiCol_ScrollbarBg] = clear;
    colors[ImGuiCol_ScrollbarGrab] = palette.scrollbar_grab;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette.scrollbar_grab_hovered;
    colors[ImGuiCol_ScrollbarGrabActive] = palette.scrollbar_grab_active;
    colors[ImGuiCol_CheckMark] = palette.accent;
    colors[ImGuiCol_SliderGrab] = palette.accent;
    colors[ImGuiCol_SliderGrabActive] = palette.accent_hovered;
    colors[ImGuiCol_Separator] = palette.border;
    colors[ImGuiCol_SeparatorHovered] = palette.border_light;
    colors[ImGuiCol_SeparatorActive] = palette.accent;
    colors[ImGuiCol_ResizeGrip] = clear;
    colors[ImGuiCol_ResizeGripHovered] = palette.accent_tint;
    colors[ImGuiCol_ResizeGripActive] = palette.accent;
    colors[ImGuiCol_TableHeaderBg] = palette.panel;
    colors[ImGuiCol_TableBorderStrong] = palette.border;
    colors[ImGuiCol_TableBorderLight] = palette.border_light;
    colors[ImGuiCol_TableRowBg] = clear;
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, TABLE_ROW_ALT_ALPHA);
    colors[ImGuiCol_DockingPreview] = palette.accent_fill;
    colors[ImGuiCol_DockingEmptyBg] = palette.chrome;
    colors[ImGuiCol_DragDropTarget] = palette.accent_hovered;
    colors[ImGuiCol_NavCursor] = palette.accent_hovered;
    colors[ImGuiCol_PlotLines] = palette.accent;
    colors[ImGuiCol_PlotLinesHovered] = palette.accent_hovered;
    colors[ImGuiCol_PlotHistogram] = palette.accent;
    colors[ImGuiCol_PlotHistogramHovered] = palette.accent_hovered;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, MODAL_DIM_ALPHA);
    g_accent_color = palette.accent_fill;
}

/// Replaces every size and shape a theme sets with the metrics'.
void apply_metrics(const theme_metrics& metrics)
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = metrics.window_padding;
    style.FramePadding = metrics.frame_padding;
    style.CellPadding = metrics.cell_padding;
    style.ItemSpacing = metrics.item_spacing;
    style.ItemInnerSpacing = metrics.item_inner_spacing;
    style.SeparatorTextPadding = metrics.separator_text_padding;
    style.IndentSpacing = metrics.indent_spacing;
    style.ScrollbarSize = metrics.scrollbar_size;
    style.GrabMinSize = metrics.grab_min_size;
    style.WindowRounding = metrics.window_rounding;
    style.ChildRounding = metrics.child_rounding;
    style.FrameRounding = metrics.frame_rounding;
    style.PopupRounding = metrics.popup_rounding;
    style.ScrollbarRounding = metrics.scrollbar_rounding;
    style.GrabRounding = metrics.grab_rounding;
    style.TabRounding = metrics.tab_rounding;
    style.WindowBorderSize = metrics.window_border_size;
    style.ChildBorderSize = metrics.child_border_size;
    style.PopupBorderSize = metrics.popup_border_size;
    style.FrameBorderSize = metrics.frame_border_size;
    style.TabBorderSize = metrics.tab_border_size;
    style.TabBarBorderSize = metrics.tab_bar_border_size;
    style.TabBarOverlineSize = metrics.tab_bar_overline_size;
    style.SeparatorTextBorderSize = metrics.separator_text_border_size;
    style.WindowMenuButtonPosition = metrics.window_menu_button_position;
}
} // namespace

auto get_theme_name(theme value) -> const char*
{
    return get_theme_desc(value).name;
}

void set_theme(theme value)
{
    const theme_desc& desc = get_theme_desc(value);
    apply_palette(desc.palette);
    apply_metrics(desc.metrics);
    // Windows pulled out of the main viewport become OS windows; square and opaque, they look the
    // same as the ones inside.
    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

} // namespace imgui_style
