#include "inspector_coretypes.h"
#include <engine/assets/asset_manager.h>
#include <engine/engine.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <filedialog/filedialog.h>
#include <limits>
#include <string_utils/utils.h>

#include <imgui/imgui_internal.h>

namespace unravel
{

namespace
{

auto get_wrap_marker() -> std::string
{
    return "\n\r";
}

auto wrap_text_two_passes(text_component& tc, const std::string& raw, float width_px, float height_px) -> std::string
{
    // common setup
    float w_m = tc.px_to_meters(width_px);
    float h_m = tc.px_to_meters(height_px);
    tc.set_area({w_m, h_m});
    tc.set_is_rich_text(false);

    // 1) First pass: extract “logical” lines by disabling wrapping
    tc.set_overflow_type(text_component::overflow_type::none);
    tc.set_text(raw);
    auto orig = tc.get_lines();

    std::string out;

    // 2) Now for each original line, re-wrap under word-wrap
    tc.set_overflow_type(text_component::overflow_type::word);

    for(size_t i = 0; i < orig.size(); ++i)
    {
        auto& para = orig[i];

        // wrap *that* paragraph
        tc.set_text(para.line);
        auto wrapped = tc.get_lines();
        for(size_t j = 0; j < wrapped.size(); ++j)
        {
            out += wrapped[j].line;
            // insert your wrap-marker between soft-wrapped lines
            if(j + 1 < wrapped.size())
            {
                out += get_wrap_marker();
            }
        }

        // preserve the *one* hard newline that was between paragraphs
        if(i + 1 < orig.size())
        {
            out += '\n';
        }
    }

    return out;
}

auto wrap_text(const std::string& input, float width, float height) -> std::string
{
    text_component tc;

    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    tc.set_font(font::default_regular());
    tc.set_font_size(ImGui::GetFontSize());
    tc.set_is_rich_text(false);

    return wrap_text_two_passes(tc, input, width, height);
}

auto unwrap_text(const std::string& input) -> std::string
{
    return string_utils::replace(input, get_wrap_marker(), "");
}

// A wrapper around ImGui::InputTextMultiline that preserves user newlines
// but only wraps on our custom "\n\r" markers.
template<size_t BuffSize = 256>
auto InputTextWidgetMultilineWrapped(const char* label, std::string& raw_buf, ImGuiInputTextFlags flags = 0) -> bool
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    float h = (true ? ImGui::GetFontSize() * 8.0f : label_size.y);
    ImVec2 frame_size =
        ImGui::CalcItemSize(ImVec2(0.0f, 0.0f),
                            ImGui::CalcItemWidth(),
                            h + style.FramePadding.y * 2.0f); // Arbitrary default of 8 lines high for multi-line
    bool changed = false;

    // 1) If we’re not currently editing, regenerate display_buf from raw_buf:
    auto display_buf = wrap_text(raw_buf, (frame_size.x - style.ScrollbarSize), frame_size.y);

    // 2) Show InputTextMultiline on display_buf:
    ImGui::SetNextItemWidth(frame_size.x);
    if(ImGui::InputTextWidget<BuffSize>(label, display_buf, true, flags))
    {
        raw_buf = unwrap_text(display_buf);
        changed = true;
    }

    return changed;
}

enum class SizeUnit
{
    B,  // bytes
    KB, // kilobytes
    MB, // megabytes
    GB  // gigabytes
};
static SizeUnit parseUnit(const std::string& unitStr)
{
    // Convert to lowercase (optional) to handle upper/lower differences
    // or handle them explicitly if you prefer:
    std::string lower;
    lower.reserve(unitStr.size());
    for(char c : unitStr)
    {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }

    if(lower == "b")
        return SizeUnit::B;
    if(lower == "kb")
        return SizeUnit::KB;
    if(lower == "mb")
        return SizeUnit::MB;
    if(lower == "gb")
        return SizeUnit::GB;

    return SizeUnit::B;
}

bool display_size_unit(const char* label,
                       float value,                 // The variable we store in the "input" unit
                       const std::string& inUnitStr // e.g. "b"
)
{
    // 1) Convert the input string to our enum.
    SizeUnit inUnit = parseUnit(inUnitStr);

    // 2) Define the scaling factors for each unit -> bytes.
    static const float factors[] = {
        1.0,                     // B
        1024.0,                  // KB
        1024.0 * 1024.0,         // MB
        1024.0 * 1024.0 * 1024.0 // GB
    };

    // 3) Convert from inUnit -> bytes.
    float bytes = value * factors[static_cast<int>(inUnit)];

    // 4) Determine which unit to use for "largest" display.
    //    We'll go from GB down to B, picking the first that fits >=1.
    //    Alternatively, check from small to large. Either approach works.
    float outValue = bytes;
    std::string suffix = "%.1f B";

    if(bytes >= factors[3]) // >= 1 GB
    {
        outValue = bytes / factors[3];
        suffix = "%.1f GB";
    }
    else if(bytes >= factors[2]) // >= 1 MB
    {
        outValue = bytes / factors[2];
        suffix = "%.1f MB";
    }
    else if(bytes >= factors[1]) // >= 1 KB
    {
        outValue = bytes / factors[1];
        suffix = "%.1f KB";
    }

    ImGui::LabelText(label, suffix.c_str(), outValue);

    return false; // so the caller knows if the value changed
}
} // namespace
template<typename T>
auto inspect_scalar(rtti::context& ctx,
                    rttr::variant& var,
                    const var_info& info,
                    const inspector::meta_getter& get_metadata,
                    const char* format = nullptr) -> inspect_result
{
    inspect_result result{};

    auto& data = var.get_value<T>();
    {
        T min{std::numeric_limits<T>::min()};
        T max{std::numeric_limits<T>::max()};
        float step{0.5};
        bool has_min{};
        bool has_max{};

        auto min_var = get_metadata("min");
        if(min_var && min_var.can_convert<T>())
        {
            min = min_var.convert<T>();
            has_min = true;
        }

        auto max_var = get_metadata("max");
        if(max_var && max_var.can_convert<T>())
        {
            max = max_var.convert<T>();
            has_max = true;
        }

        auto step_var = get_metadata("step");
        if(step_var && step_var.can_convert<float>())
        {
            step = step_var.convert<float>();
        }

        auto format_var = get_metadata("format");
        if(format_var && format_var.can_convert<std::string>())
        {
            auto fmt = format_var.convert<std::string>();
            if(fmt == "size")
            {
                auto storage_data = static_cast<float>(data);

                std::string data_format = get_metadata("format").convert<std::string>();
                if(data_format.empty())
                {
                    data_format = "B";
                }

                result.changed = display_size_unit("##", storage_data, data_format);
                ImGui::ActiveItemWrapMousePos();

                result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::DrawItemActivityOutline();
                return result;
            }
        }

        bool is_range = has_min && has_max;

        if(is_range)
        {
            if(std::is_floating_point_v<T>)
            {
                if(format == nullptr)
                {
                    if(step < 0.001)
                    {
                        format = "%.4f";
                    }
                    if(step < 0.0001)
                    {
                        format = "%.5f";
                    }
                }
            }

            result.changed = ImGui::SliderScalarT("##", &data, min, max, format);
            result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DrawItemActivityOutline();
        }
        else
        {
            if(min_var)
            {
                max = std::numeric_limits<T>::max();
            }

            result.changed = ImGui::DragScalarT("##", &data, step, min, max, format);
            ImGui::ActiveItemWrapMousePos();

            result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DrawItemActivityOutline();
        }
    }

    return result;
}

auto inspector_bool::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<bool>();
    inspect_result result{};

    {
        ImGui::BeginDisabled(info.read_only);

        result.changed = ImGui::Checkbox("##", &data);
        result.edit_finished = result.changed;

        ImGui::EndDisabled();
        ImGui::DrawItemActivityOutline();
    }

    return result;
}

auto inspector_float::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<float>(ctx, var, info, get_metadata);
}

auto inspector_double::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<double>(ctx, var, info, get_metadata);
}

auto inspector_int8::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<int8_t>(ctx, var, info, get_metadata);
}

auto inspector_int16::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<int16_t>(ctx, var, info, get_metadata);
}

auto inspector_int32::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<int32_t>(ctx, var, info, get_metadata);
}

auto inspector_int64::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<int64_t>(ctx, var, info, get_metadata);
}

auto inspector_uint8::inspect(rtti::context& ctx,
                              rttr::variant& var,
                              const var_info& info,
                              const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<uint8_t>(ctx, var, info, get_metadata);
}

auto inspector_uint16::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<uint16_t>(ctx, var, info, get_metadata);
}

auto inspector_uint32::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<uint32_t>(ctx, var, info, get_metadata);
}

auto inspector_uint64::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    return inspect_scalar<uint64_t>(ctx, var, info, get_metadata);
}

auto inspector_string::inspect(rtti::context& ctx,
                               rttr::variant& var,
                               const var_info& info,
                               const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<std::string>();

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;

    if(info.read_only)
    {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }

    bool multiline = false;

    auto multiline_var = get_metadata("multiline");
    if(multiline_var)
    {
        multiline = multiline_var.get_value<bool>();
    }

    bool wrap = false;

    auto wrap_var = get_metadata("wrap");
    if(wrap_var)
    {
        wrap = wrap_var.get_value<bool>();
    }

    inspect_result result{};

    if(multiline)
    {
        if(wrap)
        {
            result.changed |= InputTextWidgetMultilineWrapped<4096>("##", data, flags);
        }
        else
        {
            result.changed |= ImGui::InputTextWidget<4096>("##", data, multiline, flags);
        }
    }
    else
    {
        result.changed |= ImGui::InputTextWidget<128>("##", data, multiline, flags);
    }
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    std::string example;
    auto example_var = get_metadata("example");
    if(example_var)
    {
        example = example_var.get_value<std::string>();
    }

    if(!example.empty())
    {
        if(ImGui::Button(ICON_MDI_NOTE_TEXT))
        {
            data = example;
            result.changed = true;
            result.edit_finished = true;
        }

        ImGui::SetItemTooltipEx("Example Text.");
    }

    ImGui::DrawItemActivityOutline();
    return result;
}

auto inspector_path::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<fs::path>();

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;

    if(info.read_only)
    {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }

    inspect_result result{};

    std::string picked = data.generic_string();

    if(!info.read_only)
    {
        std::string type = "directory";
        auto type_var = get_metadata("type");
        if(type_var)
        {
            type = type_var.get_value<std::string>();
        }

        if(type == "file")
        {
            if(ImGui::Button(ICON_MDI_FILE_SEARCH))
            {
                if(native::open_file_dialog(picked, {}))
                {
                    data = picked;
                    picked = data.generic_string();
                    result.changed = true;
                    result.edit_finished |= true;
                }
            }
            ImGui::SetItemTooltipEx("Pick a file...");
        }
        else
        {
            if(ImGui::Button(ICON_MDI_FOLDER_OPEN))
            {
                if(native::pick_folder_dialog(picked))
                {
                    data = picked;
                    picked = data.generic_string();
                    result.changed = true;
                    result.edit_finished |= true;
                }
            }
            ImGui::SetItemTooltipEx("Pick a location...");
        }
        ImGui::SameLine();
    }

    result.changed |= ImGui::InputTextWidget<256>("##", picked, false, flags);
    result.edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    if(result.edit_finished)
    {
        data = picked;
        data.make_preferred();
        result.changed = true;
    }

    ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_duration_sec_float::inspect(rtti::context& ctx,
                                           rttr::variant& var,
                                           const var_info& info,
                                           const inspector::meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<std::chrono::duration<float>>();
    auto count = data.count();
    rttr::variant v = count;

    auto result = inspect_scalar<float>(ctx, v, info, get_metadata, "%.3fs");
    if(result.changed)
    {
        count = v.get_value<float>();
        var = std::chrono::duration<float>(count);
    }

    return result;
}

auto inspector_duration_sec_double::inspect(rtti::context& ctx,
                                            rttr::variant& var,
                                            const var_info& info,
                                            const inspector::meta_getter& get_metadata) -> inspect_result
{
    auto data = var.get_value<std::chrono::duration<double>>();
    auto count = data.count();
    rttr::variant v = count;
    auto result = inspect_scalar<double>(ctx, v, info, get_metadata, "%.f3s");
    if(result.changed)
    {
        count = v.get_value<double>();
        var = std::chrono::duration<double>(count);
    }

    return result;
}

auto inspector_uuid::inspect(rtti::context& ctx,
                             rttr::variant& var,
                             const var_info& info,
                             const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<hpp::uuid>();

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;

    if(info.read_only)
    {
        flags |= ImGuiInputTextFlags_ReadOnly;
    }

    auto str = hpp::to_string(data);

    inspect_result result{};

    result.changed = ImGui::InputTextWidget<128>("##", str, false, flags);
    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DrawItemActivityOutline();

    if(result.edit_finished)
    {
        auto parse_result = hpp::uuid::from_string(str);
        if(parse_result)
        {
            data = parse_result.value();
            result.changed = true;
            return result;
        }
    }

    result = {};
    return result;
}
} // namespace unravel
