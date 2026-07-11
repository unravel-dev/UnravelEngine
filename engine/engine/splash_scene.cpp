#include "splash_scene.h"

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/settings/settings.h>
#include <engine/ui/ecs/components/ui_document_component.h>
#include <engine/ui/ui_tree.h>
#include <logging/logging.h>
#include <seq/seq.h>

#include <RmlUi/Core/Animation.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/StyleTypes.h>
#include <RmlUi/Core/Tween.h>

#include <algorithm>
#include <chrono>

namespace unravel
{
namespace
{
constexpr const char* default_splash_document = "engine:/data/ui/splash.rhtml";
constexpr const char* splash_seq_scope = "splash_scene";
constexpr const char* made_with_logo = "engine:/data/logos/made_with.png";
constexpr float made_with_duration_sec = 5.0f;

struct splash_logo_slot
{
    std::string texture_path;
    float duration_sec = 0.0f;
};

auto to_duration(float seconds) -> seq::duration_t
{
    return std::chrono::duration_cast<seq::duration_t>(delta_t(std::max(0.0f, seconds)));
}

auto collect_logo_slots(rtti::context& ctx) -> std::vector<splash_logo_slot>
{
    std::vector<splash_logo_slot> slots;
    if(!ctx.has<settings>())
    {
        return slots;
    }

    const auto& splash_settings = ctx.get<settings>().splash;
    slots.reserve(splash_settings.logos.size() + 1);

    
    if(splash_settings.show_made_with)
    {
        slots.push_back({made_with_logo, made_with_duration_sec});
    }

    for(const auto& entry : splash_settings.logos)
    {
        if(!entry.logo)
        {
            continue;
        }
        slots.push_back({entry.logo.id(), std::max(0.0f, entry.duration_sec)});
    }

    return slots;
}

auto get_splash_logo_element(entt::handle ui_entity) -> Rml::Element*
{
    if(!ui_entity || !ui_entity.all_of<ui_document_component>())
    {
        return nullptr;
    }
    auto& ui_comp = ui_entity.get<ui_document_component>();
    if(!ui_comp.document)
    {
        return nullptr;
    }
    return ui_comp.document->GetElementById("splash-logo");
}

void set_logo_animation(Rml::Element* logo_el, const char* keyframe_name, float duration_sec)
{
    if(!logo_el || duration_sec <= 0.0f)
    {
        return;
    }

    Rml::Animation animation;
    animation.duration = duration_sec;
    animation.tween = Rml::Tween(Rml::Tween::Cubic, Rml::Tween::Out);
    animation.name = keyframe_name;
    animation.num_iterations = 1;

    Rml::AnimationList animations;
    animations.push_back(animation);
    logo_el->SetProperty(Rml::PropertyId::Animation, Rml::Property(animations, Rml::Unit::ANIMATION));
}

void set_logo_visible(entt::handle ui_entity, bool visible)
{
    if(!ui_entity)
    {
        return;
    }
    if(!ui_entity.all_of<ui_document_component>())
    {
        return;
    }
    auto* logo_el = get_splash_logo_element(ui_entity);
    if(!logo_el)
    {
        return;
    }
    if(visible)
    {
        logo_el->SetClass("visible", true);
        logo_el->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
    }
    else
    {
        logo_el->SetClass("visible", false);
        logo_el->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::None));
    }
}

void show_logo(entt::handle ui_entity, const std::string& texture_path, float duration_sec)
{
    if(!ui_entity || texture_path.empty() || duration_sec <= 0.0f)
    {
        return;
    }
    auto* logo_el = get_splash_logo_element(ui_entity);
    if(!logo_el)
    {
        return;
    }
    logo_el->SetAttribute("src", texture_path);
    logo_el->SetClass("visible", false);
    logo_el->SetProperty(Rml::PropertyId::Display, Rml::Property(Rml::Style::Display::Block));
    logo_el->SetClass("visible", true);
    set_logo_animation(logo_el, "splash-logo-show", duration_sec);
}

void fade_out_logo(entt::handle ui_entity, float duration_sec)
{
    if(!ui_entity || duration_sec <= 0.0f)
    {
        return;
    }
    auto* logo_el = get_splash_logo_element(ui_entity);
    if(!logo_el)
    {
        return;
    }
    set_logo_animation(logo_el, "splash-logo-fade-out", duration_sec);
}

auto build_logo_sequence(entt::handle ui_entity, const std::vector<splash_logo_slot>& slots, float fade_in_sec, float fade_out_sec)
    -> seq::seq_action
{
    std::vector<seq::seq_action> actions;
    actions.reserve(slots.size() * 2 + 2);

    if(fade_in_sec > 0.0f)
    {
        actions.push_back(seq::delay(to_duration(fade_in_sec)));
    }

    for(size_t i = 0; i < slots.size(); ++i)
    {
        const auto& slot = slots[i];
        auto show_action = seq::delay(to_duration(slot.duration_sec));
        show_action.on_begin.connect([ui_entity, path = slot.texture_path, duration = slot.duration_sec]()
        {
            show_logo(ui_entity, path, duration);
        });
        if(fade_out_sec <= 0.0f && i + 1 == slots.size())
        {
            show_action.on_end.connect([ui_entity]()
            {
                set_logo_visible(ui_entity, false);
            });
        }
        actions.push_back(show_action);
    }

    if(fade_out_sec > 0.0f)
    {
        auto hide_action = seq::delay(to_duration(fade_out_sec));
        hide_action.on_begin.connect([ui_entity, duration = fade_out_sec]()
        {
            fade_out_logo(ui_entity, duration);
        });
        hide_action.on_end.connect([ui_entity]()
        {
            set_logo_visible(ui_entity, false);
        });
        actions.push_back(hide_action);
    }

    return seq::sequence(actions);
}

} // namespace

auto splash_scene::get_document_key(rtti::context& ctx) -> std::string
{
    (void)ctx;
    return default_splash_document;
}

auto splash_scene::has_content(rtti::context& ctx) -> bool
{
    return !collect_logo_slots(ctx).empty();
}

void splash_scene::setup(rtti::context& ctx, scene& scn, splash_scene_state& state)
{
    teardown(state);
    state.setup_failed = false;
    state.ui_entity = {};

    const auto slots = collect_logo_slots(ctx);
    if(slots.empty())
    {
        state.setup_failed = true;
        return;
    }

    float fade_in_sec = 0.5f;
    float fade_out_sec = 0.5f;
    if(ctx.has<settings>())
    {
        const auto& splash_settings = ctx.get<settings>().splash;
        fade_in_sec = std::max(0.0f, splash_settings.fade_in_sec);
        fade_out_sec = std::max(0.0f, splash_settings.fade_out_sec);
    }

    scn.unload();

    auto& am = ctx.get_cached<asset_manager>();
    const auto document_key = get_document_key(ctx);
    auto document_asset = am.get_asset<ui_tree>(document_key);
    if(!document_asset)
    {
        APPLOG_ERROR("Failed to load splash UI document asset: {}", document_key);
        state.setup_failed = true;
        return;
    }

    defaults::create_camera_entity(ctx, scn, "Splash Camera");

    state.ui_entity = defaults::create_ui_document_entity(ctx, scn, "Splash UI");

    state.ui_entity.get<transform_component>().set_active(true);

    auto& ui_comp = state.ui_entity.get<ui_document_component>();
    ui_comp.asset = document_asset;
    ui_comp.render_mode = ui_render_mode::screen_space_overlay;

    set_logo_visible(state.ui_entity, false);

    state.pending_sequence = build_logo_sequence(state.ui_entity, slots, fade_in_sec, fade_out_sec);
}

void splash_scene::update(rtti::context& ctx, splash_scene_state& state)
{
    (void)ctx;
    if(state.setup_failed || state.action != 0 || !state.pending_sequence.is_valid())
    {
        return;
    }
    if(!state.ui_entity || !state.ui_entity.all_of<ui_document_component>())
    {
        return;
    }
    if(!state.ui_entity.get<ui_document_component>().is_loaded())
    {
        return;
    }

    set_logo_visible(state.ui_entity, false);
    state.action = seq::start(std::move(state.pending_sequence), splash_seq_scope);
    state.pending_sequence = {};
}

void splash_scene::teardown(splash_scene_state& state)
{
    if(state.action != 0)
    {
        seq::stop(state.action);
        state.action = 0;
    }
    state.pending_sequence = {};
    state.ui_entity = {};
    state.setup_failed = false;
}

auto splash_scene::is_finished(const splash_scene_state& state) -> bool
{
    if(state.setup_failed)
    {
        return true;
    }
    if(state.action != 0)
    {
        return seq::is_finished(state.action);
    }
    if(state.pending_sequence.is_valid())
    {
        return false;
    }
    return true;
}

} // namespace unravel
