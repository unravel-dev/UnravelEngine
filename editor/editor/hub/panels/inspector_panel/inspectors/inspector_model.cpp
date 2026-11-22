#include "inspector_model.h"
#include "inspector_container_widgets.h"
#include "inspectors.h"
#include "multi_edit.h"
#include "property_override_view.h"

#include <editor/editing/editing_manager.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_style.h>
#include <engine/ecs/scene.h>
#include <engine/rendering/mesh.h>

#include <imgui/imgui_internal.h>
#include <imgui_widgets/utils.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace unravel
{
namespace
{

/// The segment of the model's saved document that holds its weights by target name, as its rows and overrides name it.
constexpr const char* MORPH_WEIGHTS_SEGMENT = "morph_weights";

/// The names of the weights that differ between @p before and @p after: set in one only, or set to other values.
auto get_changed_morph_weights(const std::vector<morph_weight>& before, const std::vector<morph_weight>& after)
    -> std::vector<std::string>
{
    const auto weight_of = [](const std::vector<morph_weight>& weights, const std::string& name) -> const morph_weight*
    {
        const auto found = std::find_if(weights.begin(),
                                        weights.end(),
                                        [&name](const morph_weight& entry)
                                        {
                                            return entry.name == name;
                                        });
        return found != weights.end() ? &*found : nullptr;
    };
    std::vector<std::string> changed;
    for(const auto* list : {&before, &after})
    {
        for(const auto& entry : *list)
        {
            const auto* old_entry = weight_of(before, entry.name);
            const auto* new_entry = weight_of(after, entry.name);
            const bool differs = old_entry == nullptr || new_entry == nullptr || old_entry->weight != new_entry->weight;
            if(differs && std::find(changed.begin(), changed.end(), entry.name) == changed.end())
            {
                changed.push_back(entry.name);
            }
        }
    }
    return changed;
}

/// The model of @p entity when its mesh has a morph target named @p name, else null.
auto find_model_with_target(entt::handle entity, const std::string& name) -> model_component*
{
    auto* model = entity ? entity.try_get<model_component>() : nullptr;
    if(model == nullptr)
    {
        return nullptr;
    }
    const auto targets = model->get_morph_targets();
    return targets && targets->find_target(name) >= 0 ? model : nullptr;
}

/// The weight @p model gives @p target: its own, else the mesh's default.
auto get_shown_morph_weight(const model_component& model, const morph_target& target) -> float
{
    const auto set = model.find_morph_weight(target.name);
    return set ? *set : target.default_weight;
}

/// Whether @p model gives @p target a weight of its own that differs from the mesh's default.
auto is_morph_weight_off_default(const model_component& model, const morph_target& target) -> bool
{
    const auto set = model.find_morph_weight(target.name);
    return set && *set != target.default_weight;
}

/// Whether any of @p edited sets a weight of its own, so Reset All has something to do.
auto has_set_morph_weights(const std::vector<entt::handle>& edited) -> bool
{
    return std::any_of(edited.begin(),
                       edited.end(),
                       [](const entt::handle& entity)
                       {
                           const auto* model = entity ? entity.try_get<model_component>() : nullptr;
                           return model != nullptr && !model->get_morph_weights().empty();
                       });
}

/// Where a target's row goes and what it reads: the part of the mesh a "Part.Shape" name begins with (empty for a
/// name without one), and the shape's name without what every name of its part repeats, in words.
struct morph_row_name
{
    std::string group;
    std::string label;
};

/// The length of what every one of @p names begins with, cut back to just after a separator so no word is split; 0
/// for a single name, or when a name would be left empty.
auto get_shared_morph_prefix(const std::vector<std::string>& names) -> size_t
{
    if(names.size() < 2)
    {
        return 0;
    }
    const auto& first = names.front();
    size_t length = first.size();
    for(const auto& name : names)
    {
        size_t common = 0;
        while(common < length && common < name.size() && name[common] == first[common])
        {
            ++common;
        }
        length = common;
    }
    while(length > 0 && first[length - 1] != '_' && first[length - 1] != '.')
    {
        --length;
    }
    const bool leaves_every_name = std::all_of(names.begin(),
                                               names.end(),
                                               [length](const std::string& name)
                                               {
                                                   return name.size() > length;
                                               });
    return leaves_every_name ? length : 0;
}

/// Each target's row name. VRoid names a face's targets "Face.M_F00_000_00_Fcl_ALL_Joy": the row reads "ALL Joy" under
/// a "Face" header, and the full name is in the row's tooltip. "Key 1" stays "Key 1".
auto make_morph_row_names(const std::vector<morph_target>& targets) -> std::vector<morph_row_name>
{
    std::vector<morph_row_name> rows(targets.size());
    std::vector<std::string> shapes(targets.size());
    std::vector<std::string> groups;
    for(size_t i = 0; i < targets.size(); ++i)
    {
        const auto& name = targets[i].name;
        const auto dot = name.find('.');
        rows[i].group = dot == std::string::npos ? std::string() : name.substr(0, dot);
        shapes[i] = dot == std::string::npos ? name : name.substr(dot + 1);
        if(std::find(groups.begin(), groups.end(), rows[i].group) == groups.end())
        {
            groups.push_back(rows[i].group);
        }
    }
    for(const auto& group : groups)
    {
        std::vector<size_t> members;
        std::vector<std::string> names;
        for(size_t i = 0; i < targets.size(); ++i)
        {
            if(rows[i].group == group)
            {
                members.push_back(i);
                names.push_back(shapes[i]);
            }
        }
        const size_t shared = get_shared_morph_prefix(names);
        for(const auto i : members)
        {
            auto label = shapes[i].substr(shared);
            std::replace(label.begin(), label.end(), '_', ' ');
            rows[i].label = label.empty() ? targets[i].name : label;
        }
    }
    return rows;
}

/// What the Morph Targets row's controls were asked for this frame.
struct morph_header_request
{
    bool is_active_only_toggled{};
    bool is_reset_all{};
};

/// The Morph Targets row's value, right aligned: how many targets move the mesh, the toggle listing only those, and
/// Reset All.
auto draw_morph_header_controls(size_t active, size_t total, bool is_active_only, bool can_reset) -> morph_header_request
{
    morph_header_request request{};
    const float side = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto count = fmt::format("{} of {} active", active, total);
    const float width = ImGui::CalcTextSize(count.c_str()).x + 2.0f * (spacing + side);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - width));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(imgui_style::get_muted_text_color(), "%s", count.c_str());
    const auto draw_button = [side](const container_widgets::icon_button& button)
    {
        ImGui::SameLine();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImRect rect(min, ImVec2(min.x + side, min.y + side));
        ImGui::ItemSize(rect);
        return container_widgets::draw_icon_button(button, rect);
    };
    container_widgets::icon_button active_only{"##morph_active_only",
                                               ICON_MDI_FILTER_VARIANT,
                                               is_active_only ? "Lists every morph target."
                                                              : "Lists only the morph targets whose weight moves the mesh."};
    if(is_active_only)
    {
        active_only.color = ImGui::GetColorU32(imgui_style::get_accent_color());
    }
    request.is_active_only_toggled = draw_button(active_only);
    ImGui::BeginDisabled(!can_reset);
    request.is_reset_all = draw_button({"##morph_reset_all",
                                        ICON_MDI_RESTORE,
                                        "Puts every morph target back to the mesh's default weight, on every selected "
                                        "model."});
    ImGui::EndDisabled();
    return request;
}

/// Marks the targets whose weight differs from the mesh's default, and takes the name of the one whose gutter button or
/// menu asks for the default back.
auto make_morph_weight_marks(const model_component& model, hpp::optional<std::string>& reverted) -> property_override_marks
{
    const std::string prefix = std::string(MORPH_WEIGHTS_SEGMENT) + "/";
    const auto get_name = [prefix](const std::string& path) -> hpp::optional<std::string>
    {
        if(path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0)
        {
            return hpp::nullopt;
        }
        return path.substr(prefix.size());
    };
    property_override_marks marks;
    marks.get_mark = [&model, get_name](const std::string& path)
    {
        const auto name = get_name(path);
        const auto targets = model.get_morph_targets();
        const int index = name && targets ? targets->find_target(*name) : -1;
        const bool is_off_default = index >= 0 && is_morph_weight_off_default(model, targets->targets[size_t(index)]);
        return is_off_default ? override_mark::overridden : override_mark::none;
    };
    marks.revert = [&reverted, get_name](const std::string& path)
    {
        reverted = get_name(path);
    };
    marks.revert_label = "Reset to the Mesh's Default";
    marks.overridden_note = "This model's weight differs from the mesh's default.";
    marks.marker_color = INSTANCE_OVERRIDE_COLOR;
    return marks;
}

} // namespace

void record_morph_weights_change(rtti::context& ctx,
                                 const std::vector<morph_weights_change>& changes,
                                 const std::string& action_name)
{
    struct model_edit
    {
        entt::uhandle entity;
        std::vector<morph_weight> before;
        std::vector<morph_weight> after;
        std::vector<std::string> changed;
    };
    std::vector<model_edit> edits;
    for(const auto& change : changes)
    {
        auto changed = get_changed_morph_weights(change.before, change.after);
        if(!changed.empty())
        {
            edits.push_back({entt::make_uhandle(change.entity), change.before, change.after, std::move(changed)});
        }
    }
    if(edits.empty())
    {
        return;
    }
    auto apply = [edits](bool is_after)
    {
        for(const auto& edit : edits)
        {
            const auto resolved = edit.entity.resolve();
            auto* model = resolved ? resolved.try_get<model_component>() : nullptr;
            if(model == nullptr)
            {
                continue;
            }
            model->set_morph_weights(is_after ? edit.after : edit.before);
            for(const auto& name : edit.changed)
            {
                prefab_override_context::mark_morph_weight_as_changed(resolved, name);
            }
        }
    };
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action(
        action_name,
        [apply]()
        {
            apply(true);
        },
        [apply]()
        {
            apply(false);
        });
}

auto inspector_model_component::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto result = inspect_var_properties(ctx, var, var_proxy, info, custom);
    auto& data = var.cast<model_component&>();
    ImGui::PushID("morph_targets_section");
    draw_morph_targets_section(ctx, data);
    ImGui::PopID();
    return result;
}

void inspector_model_component::draw_morph_targets_section(rtti::context& ctx, model_component& data)
{
    const auto& lod = data.get_model().get_lod(0);
    if(!lod.is_ready())
    {
        return;
    }
    const auto mesh = lod.get(false);
    if(!mesh || !mesh->has_morph_targets())
    {
        return;
    }
    const auto& targets = mesh->get_morph_targets()->targets;
    // With several entities selected, an edit reaches every selected model whose mesh has a target of that name.
    const auto edited = multi_edit::get_targets(data.get_owner());
    const auto names = make_morph_row_names(targets);
    const auto is_moving = [&data](const morph_target& target)
    {
        return std::abs(get_shown_morph_weight(data, target)) > k_morph_weight_threshold;
    };
    const auto active = size_t(std::count_if(targets.begin(), targets.end(), is_moving));
    std::vector<std::string> groups;
    for(const auto& name : names)
    {
        if(std::find(groups.begin(), groups.end(), name.group) == groups.end())
        {
            groups.push_back(name.group);
        }
    }

    auto& prefab_ctx = ctx.get_cached<prefab_override_context>();
    prefab_ctx.push_segment(MORPH_WEIGHTS_SEGMENT, "Morph Targets");
    hpp::optional<std::string> reverted;
    morph_header_request request{};
    {
        property_layout_group group("Morph Targets");
        ImGui::BeginGroup();
        {
            property_layout header;
            header.set_data("Morph Targets",
                            "The mesh's morph targets (blend shapes): shapes its vertices move toward, each by a weight "
                            "from 0, the rest shape, to 1, the full shape. Animation curves and scripts set them too.");
            const bool open = header.push_tree_layout();
            request = draw_morph_header_controls(active, targets.size(), show_active_only_, has_set_morph_weights(edited));
            if(open)
            {
                header.pop_layout();
                const container_widgets::body_frame body;
                property_override_scope marked(ctx, {}, make_morph_weight_marks(data, reverted));
                auto& view = ctx.get_cached<property_override_view>();
                view.push_segment(MORPH_WEIGHTS_SEGMENT);
                bool has_rows = false;
                for(const auto& group_name : groups)
                {
                    bool has_header = false;
                    for(size_t i = 0; i < targets.size(); ++i)
                    {
                        const auto& target = targets[i];
                        if(names[i].group != group_name || (show_active_only_ && !is_moving(target)))
                        {
                            continue;
                        }
                        view.push_segment(target.name);
                        prefab_ctx.push_segment(target.name, names[i].label);
                        if(match_row_filter(ctx, names[i].label).is_shown)
                        {
                            if(groups.size() > 1 && !std::exchange(has_header, true))
                            {
                                draw_property_header(group_name.empty() ? std::string("Other") : group_name, has_rows);
                            }
                            draw_morph_weight_row(ctx, data, target, names[i].label, edited);
                            has_rows = true;
                        }
                        prefab_ctx.pop_segment();
                        view.pop_segment();
                    }
                }
                view.pop_segment();
                if(!has_rows && show_active_only_)
                {
                    container_widgets::draw_empty_hint("No morph target moves the mesh.");
                }
            }
        }
        ImGui::EndGroup();
    }
    prefab_ctx.pop_segment();

    if(request.is_active_only_toggled)
    {
        show_active_only_ = !show_active_only_;
    }
    if(request.is_reset_all)
    {
        std::vector<morph_weights_change> changes;
        for(const auto& selected : edited)
        {
            if(const auto* model = selected ? selected.try_get<model_component>() : nullptr)
            {
                changes.push_back({selected, model->get_morph_weights(), {}});
            }
        }
        record_morph_weights_change(ctx, changes, "Reset Morph Weights");
    }
    else if(reverted)
    {
        std::vector<morph_weights_change> changes;
        for(const auto& selected : edited)
        {
            if(const auto* model = find_model_with_target(selected, *reverted))
            {
                auto after = model->get_morph_weights();
                after.erase(std::remove_if(after.begin(),
                                           after.end(),
                                           [&reverted](const morph_weight& entry)
                                           {
                                               return entry.name == *reverted;
                                           }),
                            after.end());
                changes.push_back({selected, model->get_morph_weights(), std::move(after)});
            }
        }
        record_morph_weights_change(ctx, changes, "Reset Morph Weight");
    }
}

void inspector_model_component::draw_morph_weight_row(rtti::context& ctx,
                                                      model_component& data,
                                                      const morph_target& target,
                                                      const std::string& label,
                                                      const std::vector<entt::handle>& edited)
{
    // Two parts of a mesh may name a shape alike: the row's id is the target's full name.
    ImGui::PushID(target.name.c_str());
    {
        property_layout layout;
        layout.set_data(label,
                        fmt::format("The morph target {}. Its weight moves the mesh toward its shape: 0 is the rest "
                                    "shape and 1 the full shape. Ctrl+click the slider to type a weight past 1 or below "
                                    "0. It moves {} vertices, by up to {:.4g}.",
                                    target.name,
                                    target.delta_count,
                                    target.max_displacement));
        layout.set_mixed_path(ctx.get_cached<prefab_override_context>().get_row_path());
        layout.push_layout();
        float weight = get_shown_morph_weight(data, target);
        const bool changed = ImGui::KnobSliderScalarT("##weight", &weight, 0.0f, 1.0f, "%.3f");
        ImGui::DrawItemActivityOutline();
        if(ImGui::IsItemActivated())
        {
            edit_changes_.clear();
            for(const auto& selected : edited)
            {
                if(const auto* model = find_model_with_target(selected, target.name))
                {
                    edit_changes_.push_back({selected, model->get_morph_weights(), {}});
                }
            }
        }
        if(changed)
        {
            for(const auto& change : edit_changes_)
            {
                if(auto* model = find_model_with_target(change.entity, target.name))
                {
                    model->set_morph_weight(target.name, weight);
                }
            }
        }
        if(ImGui::IsItemDeactivatedAfterEdit())
        {
            for(auto& change : edit_changes_)
            {
                if(const auto* model = change.entity ? change.entity.try_get<model_component>() : nullptr)
                {
                    change.after = model->get_morph_weights();
                }
            }
            record_morph_weights_change(ctx, edit_changes_, "Set Morph Weight");
            edit_changes_.clear();
        }
    }
    ImGui::PopID();
}

} // namespace unravel
