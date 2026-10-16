#pragma once

#include "animation_clip_timeline.h"
#include "inspector.h"
#include <engine/assets/asset_handle.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/scene.h>
#include <audiopp/source.h>

namespace gfx
{
struct texture;
}

namespace unravel
{
class mesh;
class material;
struct physics_material;
struct animation_clip;
struct animation_controller;
struct audio_clip;
struct font;
struct prefab;
struct scene_prefab;
struct ui_tree;
struct style_sheet;

struct inspector_asset_handle : public crtp_meta_type<inspector_asset_handle, inspector>
{
    ImGuiTextFilter filter;
};

struct inspector_asset_handle_texture : public crtp_meta_type<inspector_asset_handle_texture, inspector_asset_handle>
{
    void draw_image(const asset_handle<gfx::texture>& data, ImVec2 size);
    auto inspect_as_property(rtti::context& ctx, asset_handle<gfx::texture>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

    asset_handle<gfx::texture> inspected_asset_;
    std::shared_ptr<texture_importer_meta> importer_;
    int inspected_mip_{};
    uintptr_t inspected_version_{};

};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_texture, asset_handle<gfx::texture>)

struct inspector_asset_handle_material : public crtp_meta_type<inspector_asset_handle_material, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<material>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

private:
    /// For a model's default material: a banner naming the model it comes from and whether imports still rewrite
    /// it, with Reset to Imported once it was edited.
    void inspect_import_origin(rtti::context& ctx, const asset_handle<material>& data);
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_material, asset_handle<material>)

struct inspector_shared_material : public crtp_meta_type<inspector_shared_material, inspector>
{
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_shared_material, std::shared_ptr<material>)


struct inspector_asset_handle_mesh : public crtp_meta_type<inspector_asset_handle_mesh, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<mesh>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

    asset_handle<mesh> inspected_asset_;
    std::shared_ptr<mesh_importer_meta> importer_;
    uintptr_t inspected_version_{};
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_mesh, asset_handle<mesh>)

struct inspector_asset_handle_animation : public crtp_meta_type<inspector_asset_handle_animation, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<animation_clip>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
    /// Edits nobody applied or reverted are applied rather than lost with the selection.
    void end_inspection(rtti::context& ctx) override;

    asset_handle<animation_clip> inspected_asset_;
    /// The import settings being edited, and as of the last finished edit (the base of the next
    /// undo step). Undo steps reach them through weak references: once Apply or Revert drops the
    /// staged copy, its steps do nothing.
    std::shared_ptr<animation_importer_meta> importer_;
    std::shared_ptr<animation_importer_meta> committed_;
    uintptr_t inspected_version_{};
    /// Events and curves of the import settings: timeline, scrubbing preview, editors.
    animation_clip_timeline timeline_;

private:
    void apply_import_settings(rtti::context& ctx);
    /// The finished edits differ from the settings the asset was imported with.
    auto has_unapplied_edits(rtti::context& ctx) const -> bool;
    void drop_staged_edits();
    void record_import_edit(rtti::context& ctx);
    void draw_unapplied_bar(rtti::context& ctx);
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_animation, asset_handle<animation_clip>)

struct inspector_asset_handle_prefab : public crtp_meta_type<inspector_asset_handle_prefab, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<prefab>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

private:
    auto get_prefab_entity(rtti::context& ctx, const asset_handle<prefab>& prefab) -> entt::handle;

    scene inspected_scene_{"inspector_asset_handle_prefab"};
    uintptr_t inspected_version_{};

    /// The cached authoring root, keyed by handle: it is detached from its instance link, so
    /// there is no prefab_component to find it by.
    entt::handle inspected_root_{};
    hpp::uuid inspected_uid_{};


};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_prefab, asset_handle<prefab>)

struct inspector_asset_handle_scene_prefab : public crtp_meta_type<inspector_asset_handle_scene_prefab, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<scene_prefab>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_scene_prefab, asset_handle<scene_prefab>)

struct inspector_asset_handle_physics_material : public crtp_meta_type<inspector_asset_handle_physics_material, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<physics_material>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_physics_material, asset_handle<physics_material>)

struct inspector_asset_handle_animation_controller
    : public crtp_meta_type<inspector_asset_handle_animation_controller, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<animation_controller>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_animation_controller, asset_handle<animation_controller>)

struct inspector_asset_handle_audio_clip : public crtp_meta_type<inspector_asset_handle_audio_clip, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<audio_clip>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;

    asset_handle<audio_clip> inspected_asset_;
    std::shared_ptr<audio_importer_meta> importer_;
    uintptr_t inspected_version_{};

private:
    void inspect_clip(const std::shared_ptr<audio_clip>& clip);
    std::shared_ptr<audio::source> source_; ///< The audio source object.
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_audio_clip, asset_handle<audio_clip>)


struct inspector_asset_handle_font : public crtp_meta_type<inspector_asset_handle_font, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<font>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_font, asset_handle<font>)

struct inspector_asset_handle_ui_tree : public crtp_meta_type<inspector_asset_handle_ui_tree, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<ui_tree>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_ui_tree, asset_handle<ui_tree>)

struct inspector_asset_handle_style_sheet : public crtp_meta_type<inspector_asset_handle_style_sheet, inspector_asset_handle>
{
    auto inspect_as_property(rtti::context& ctx, asset_handle<style_sheet>& data) -> inspect_result;
    auto inspect(rtti::context& ctx, entt::meta_any& var, const meta_any_proxy& var_proxy, const var_info& info, const entt::meta_custom& custom) -> inspect_result override;
};
REFLECT_INSPECTOR_INLINE(inspector_asset_handle_style_sheet, asset_handle<style_sheet>)


} // namespace unravel
