#include "inspector_curve.h"
#include "inspector_curve_editor.h"

#include <editor/imgui/integration/imgui.h>

namespace unravel
{
namespace
{
/// Builds the editor's view of a curve out of what curve_traits says about its value type.
template<typename T>
auto make_curve_view(math::curve<T>& curve, const entt::meta_custom& custom) -> curve_editor::curve_view
{
    using traits = math::curve_traits<T>;

    curve_editor::curve_view view;
    view.is_color = traits::is_color;
    view.channels.reserve(traits::channel_count);
    for(size_t channel = 0; channel < traits::channel_count; ++channel)
    {
        curve_editor::channel_view channel_view;
        channel_view.channel = &curve.get_channel(channel);
        channel_view.name = traits::channel_name(channel);
        const math::color channel_color = traits::channel_color(channel);
        channel_view.color = IM_COL32(int(channel_color.value.r * 255.0f),
                                      int(channel_color.value.g * 255.0f),
                                      int(channel_color.value.b * 255.0f),
                                      int(channel_color.value.a * 255.0f));
        view.channels.push_back(channel_view);
    }

    // The limits and the step the property itself declares, the same ones the per-key editors of
    // the gradient widget used to honour.
    const auto& min_attribute = entt::get_attribute(custom, "min");
    const auto& max_attribute = entt::get_attribute(custom, "max");
    if(min_attribute && max_attribute)
    {
        view.has_value_limits = true;
        view.value_min = entt::get_attribute_as<float>(custom, "min");
        view.value_max = entt::get_attribute_as<float>(custom, "max");
    }
    view.value_step = entt::get_attribute_as<float>(custom, "step");
    view.title = entt::get_attribute_as<std::string>(custom, "pretty_name");
    return view;
}
} // namespace

template<typename T>
auto inspector_curve<T>::inspect(rtti::context& /*ctx*/,
                                 entt::meta_any& var,
                                 const meta_any_proxy& /*var_proxy*/,
                                 const var_info& info,
                                 const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<math::curve<T>&>();

    // get_channel hands out a mutable channel and drops the bake, so a curve edited here is
    // re-baked by whichever setter the property writes through.
    auto view = make_curve_view(data, custom);
    bool linked = data.is_linked();
    view.is_linked = &linked;

    ImGui::PushReadonly(info.read_only);
    const auto edit = curve_editor::draw("##curve", view);
    ImGui::PopReadonly();

    if(linked != data.is_linked())
    {
        data.set_linked(linked);
    }

    inspect_result result{};
    result.changed = edit.changed;
    result.edit_finished = edit.edit_finished;
    return result;
}

template struct inspector_curve<float>;
template struct inspector_curve<math::vec2>;
template struct inspector_curve<math::vec3>;
template struct inspector_curve<math::color>;
template struct inspector_curve<frange_t>;

} // namespace unravel
