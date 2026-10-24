#include "property_override_view.h"

#include <editor/imgui/integration/imgui.h>
#include <editor/imgui/integration/imgui_style.h>

#include <algorithm>

namespace unravel
{
namespace
{
constexpr char OVERRIDE_PATH_SEPARATOR = '/';
/// How many style colors a styled property pushes.
constexpr int OVERRIDE_STYLE_COLORS = 2;
/// The owned overrides' color, as a prefab instance's overrides are drawn.
constexpr ImVec4 OVERRIDE_OWNED_COLOR{1.0f, 1.0f, 1.0f, 1.0f};
/// The shadowing overrides' color: the header's preview choice is drawn in it.
constexpr ImVec4 OVERRIDE_SHADOWING_COLOR{1.0f, 183.0f / 255.0f, 77.0f / 255.0f, 1.0f};
} // namespace

auto property_override_view::is_active() const -> bool
{
    return static_cast<bool>(marks_.get_mark);
}

void property_override_view::push_segment(const std::string& segment)
{
    if(!is_active())
    {
        return;
    }
    path_ends_.push_back(path_.size());
    if(!path_.empty())
    {
        path_ += OVERRIDE_PATH_SEPARATOR;
    }
    path_ += segment;
    const auto mark = marks_.get_mark(path_);
    // A property in a marked group gets its own style back when it is not marked itself.
    const bool is_in_marked_group = std::any_of(segment_marks_.begin(),
                                                segment_marks_.end(),
                                                [](override_mark parent)
                                                {
                                                    return parent != override_mark::none;
                                                });
    const bool is_styled = mark != override_mark::none || is_in_marked_group;
    if(is_styled)
    {
        push_style(mark);
    }
    segment_marks_.push_back(mark);
    is_styled_.push_back(is_styled);
}

void property_override_view::push_style(override_mark mark)
{
    const bool is_owned = marks_.style == override_style::owned;
    const bool is_bold = mark == override_mark::contains || (mark == override_mark::overridden && is_owned);
    if(is_bold)
    {
        ImGui::PushFont(ImGui::Font::ExtraBold);
    }
    else
    {
        // The font the scope began in, at the size in use.
        ImGui::PushFont(plain_font_, 0.0f);
    }
    if(mark == override_mark::overridden)
    {
        const ImVec4 color = is_owned ? OVERRIDE_OWNED_COLOR : OVERRIDE_SHADOWING_COLOR;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushStyleColor(ImGuiCol_CheckMark, color);
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, plain_text_color_);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, plain_check_color_);
}

void property_override_view::pop_segment()
{
    if(!is_active() || path_ends_.empty())
    {
        return;
    }
    if(is_styled_.back())
    {
        ImGui::PopStyleColor(OVERRIDE_STYLE_COLORS);
        ImGui::PopFont();
    }
    is_styled_.pop_back();
    segment_marks_.pop_back();
    path_.resize(path_ends_.back());
    path_ends_.pop_back();
}

auto property_override_view::get_current_mark() const -> override_mark
{
    return is_active() && !segment_marks_.empty() ? segment_marks_.back() : override_mark::none;
}

auto property_override_view::get_revert_label() const -> const std::string&
{
    static const std::string none;
    return marks_.revert && get_current_mark() != override_mark::none ? marks_.revert_label : none;
}

void property_override_view::revert_current() const
{
    if(marks_.revert && get_current_mark() != override_mark::none)
    {
        marks_.revert(path_);
    }
}

auto property_override_view::get_marker_color() const -> ImVec4
{
    if(marks_.marker_color.w > 0.0f)
    {
        return marks_.marker_color;
    }
    if(marks_.style == override_style::shadowing)
    {
        return OVERRIDE_SHADOWING_COLOR;
    }
    // The accent is a translucent fill; a mark is drawn solid.
    ImVec4 accent = imgui_style::get_accent_color();
    accent.w = 1.0f;
    return accent;
}

auto property_override_view::is_current_shared() const -> bool
{
    return is_active() && marks_.is_shared && !segment_marks_.empty() && marks_.is_shared(path_);
}

auto property_override_view::get_tooltip_note() const -> const std::string&
{
    static const std::string none;
    if(is_current_shared())
    {
        return marks_.shared_note;
    }
    switch(get_current_mark())
    {
        case override_mark::overridden:
            return marks_.overridden_note;
        case override_mark::contains:
            return marks_.contains_note;
        case override_mark::none:
        default:
            return none;
    }
}

property_override_plain_scope::property_override_plain_scope(const property_override_view& view)
    : is_pushed_(view.is_active())
{
    if(!is_pushed_)
    {
        return;
    }
    ImGui::PushFont(view.plain_font_, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, view.plain_text_color_);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, view.plain_check_color_);
}

property_override_plain_scope::~property_override_plain_scope()
{
    if(is_pushed_)
    {
        ImGui::PopStyleColor(OVERRIDE_STYLE_COLORS);
        ImGui::PopFont();
    }
}

property_override_scope::property_override_scope(rtti::context& ctx, const std::string& root, property_override_marks marks)
    : view_(ctx.get_cached<property_override_view>())
    , previous_(std::move(view_))
{
    view_ = {};
    view_.marks_ = std::move(marks);
    view_.path_ = root;
    view_.plain_font_ = ImGui::GetFont();
    view_.plain_text_color_ = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    view_.plain_check_color_ = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
}

property_override_scope::~property_override_scope()
{
    view_ = std::move(previous_);
}

} // namespace unravel
