#include "inspector_basetypes.h"
#include "imgui_widgets/utils.h"

namespace unravel
{
template<typename T>
auto inspect_range_scalar(rtti::context& ctx,
                          rttr::variant& var,
                          const var_info& info,
                          const inspector::meta_getter& get_metadata,
                          const char* fmt0 = "Min:",
                          const char* fmt1 = "Max:") -> inspect_result
{
    auto& data = var.get_value<range<T>>();

    T min{};
    T max{};

    T* min_ptr{};
    T* max_ptr{};
    auto min_var = get_metadata("min");
    if(min_var && min_var.can_convert<T>())
    {
        min = min_var.convert<T>();
        min_ptr = &min;
    }

    auto max_var = get_metadata("max");
    if(max_var && max_var.can_convert<T>())
    {

        max = max_var.convert<T>();
        max_ptr = &max;
    }
    const auto formatted0 = fmt::format("{}{}", fmt0, ImGui::GetDataPrintFormat<T>());
    const auto formatted1 = fmt::format("{}{}", fmt0, ImGui::GetDataPrintFormat<T>());

    std::array<const char*, 2> formats = {{formatted0.c_str(), formatted1.c_str()}};

    inspect_result result{};
    result.changed = ImGui::DragMultiFormatScalarN("##",
                                                   ImGui::GetDataType<T>(),
                                                   &data.min,
                                                   2,
                                                   0.5f,
                                                   min_ptr,
                                                   max_ptr,
                                                   formats.data());
    ImGui::ActiveItemWrapMousePos();

    result.edit_finished = ImGui::IsItemDeactivatedAfterEdit();

    //ImGui::DrawItemActivityOutline();

    return result;
}

auto inspector_range_float::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<float>(ctx, var, info, get_metadata);
}

auto inspector_range_double::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<double>(ctx, var, info, get_metadata);
}

auto inspector_range_int8::inspect(rtti::context& ctx,
                                   rttr::variant& var,
                                   const var_info& info,
                                   const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int8_t>(ctx, var, info, get_metadata);
}

auto inspector_range_int16::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int16_t>(ctx, var, info, get_metadata);
}

auto inspector_range_int32::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int32_t>(ctx, var, info, get_metadata);
}

auto inspector_range_int64::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int64_t>(ctx, var, info, get_metadata);
}

auto inspector_range_uint8::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint8_t>(ctx, var, info, get_metadata);
}

auto inspector_range_uint16::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint16_t>(ctx, var, info, get_metadata);
}

auto inspector_range_uint32::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint32_t>(ctx, var, info, get_metadata);
}

auto inspector_range_uint64::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint64_t>(ctx, var, info, get_metadata);
}




auto inspector_size_float::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<float>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_double::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<double>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_int8::inspect(rtti::context& ctx,
                                   rttr::variant& var,
                                   const var_info& info,
                                   const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int8_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_int16::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int16_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_int32::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int32_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_int64::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<int64_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_uint8::inspect(rtti::context& ctx,
                                    rttr::variant& var,
                                    const var_info& info,
                                    const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint8_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_uint16::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint16_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_uint32::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint32_t>(ctx, var, info, get_metadata, "W:", "H:");
}

auto inspector_size_uint64::inspect(rtti::context& ctx,
                                     rttr::variant& var,
                                     const var_info& info,
                                     const meta_getter& get_metadata) -> inspect_result
{
    return inspect_range_scalar<uint64_t>(ctx, var, info, get_metadata, "W:", "H:");
}

} // namespace unravel
