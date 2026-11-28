#include "inspector_particle_emitter.h"
#include "editor/hub/panels/inspector_panel/inspectors/inspector.h"
#include "inspectors.h"

#include "material_graph_edit.h"

#include <editor/assets/asset_actions.h>
#include <engine/assets/asset_manager.h>
#include <engine/play_mode.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <engine/rendering/particles/ps/particle_gpu.h>
#include <engine/rendering/shader_buffer.h>
#include <logging/logging.h>

namespace unravel
{
namespace
{

/// Status colors: stopped, paused, playing, and the warning of a full emitter.
const ImVec4 k_stopped_color(0.8f, 0.4f, 0.4f, 1.0f);
const ImVec4 k_paused_color(0.8f, 0.8f, 0.4f, 1.0f);
const ImVec4 k_playing_color(0.4f, 0.8f, 0.4f, 1.0f);
const ImVec4 k_full_color(0.9f, 0.6f, 0.3f, 1.0f);

/// Whether the playback buttons drive the emitters in the entity's children too. One choice for every emitter
/// inspected: an effect is usually a root emitter with its parts below it.
bool g_with_child_emitters = true;

/// A playback button: disabled when it would do nothing. True when it was pressed.
auto draw_playback_button(const char* label, bool is_enabled, const char* tooltip) -> bool
{
    ImGui::BeginDisabled(!is_enabled);
    const bool is_pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", tooltip);
    ImGui::SameLine();
    return is_pressed;
}

void draw_status(const particle_emitter_component& data)
{
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    if(!data.is_playing())
    {
        ImGui::TextColored(k_stopped_color, "Stopped");
    }
    else if(data.is_paused())
    {
        ImGui::TextColored(k_paused_color, "Paused");
    }
    else
    {
        ImGui::TextColored(k_playing_color, "Playing");
    }
    // Live particles against the capacity: a full emitter skips the particles it cannot hold.
    const uint32_t live = data.get_num_particles();
    const uint32_t capacity = data.get_max_particles();
    ImGui::SameLine();
    ImGui::Text("Particles: %u / %u", live, capacity);
    if(capacity > 0 && live >= capacity)
    {
        ImGui::SameLine();
        ImGui::TextColored(k_full_color, "(full)");
        ImGui::SetItemTooltip("The emitter is at its capacity and skips new particles. Raise Max Particles or lower the rates.");
    }
}

/// The rows the emitter's inspector puts notes on, by the names of their properties.
constexpr const char* EMITTER_MATERIAL_ROW = "render_material";
constexpr const char* EMITTER_COLLISION_ROW = "collision";
constexpr const char* EMITTER_COLLIDE_WITH_ROW = "collide_with";
constexpr const char* EMITTER_AMBIENT_LIGHT_ROW = "ambient_light";

/// Turns Particle Use on in the graph of @p asset, as one undo step. A particle draw leaves a material fewer textures
/// than a mesh's, and every user of the material would lose the ones past that: a graph that does not compile with
/// Particle Use stays as it is, and the log says so. True when the material was saved with it.
auto turn_particle_use_on(rtti::context& ctx, const asset_handle<material>& asset, const graph_material& mat) -> bool
{
    auto edited = mat.get_graph();
    if(!set_material_graph_particle_use(edited, true))
    {
        return false;
    }
    // Errors the graph has without it are its own, and the Material Graph window's to show.
    if(compile_material_graph(edited).has_errors() && !compile_material_graph(mat.get_graph()).has_errors())
    {
        APPLOG_ERROR("Particle Use was not turned on for {}: with it the material does not compile, since a particle "
                     "draw leaves a material fewer textures than a mesh does. Open the material in the Material Graph "
                     "window, turn Particle Use on there and follow the errors it shows.",
                     asset.id());
        return false;
    }
    record_graph_material_edit(ctx, asset, mat.get_graph(), std::move(edited));
    return true;
}

/// The material the Material row's button saved with Particle Use on, until it loads again with it: the saved file
/// has it and the loaded material does not yet, so the row says so in place of the button, which would save the same
/// edit a second time. A save that brings no such material by then (an undo, a failed compile) leaves the row as
/// it finds it.
struct particle_use_request
{
    hpp::uuid material;
    double expires_at{};
};
particle_use_request g_particle_use_request;
/// Seconds a material takes at the most to compile and load again after its save.
constexpr double PARTICLE_USE_RELOAD_SECONDS = 20.0;

/// Whether @p asset is the material whose Particle Use was just turned on and that has not loaded again yet.
auto awaits_particle_use(const asset_handle<material>& asset) -> bool
{
    return !g_particle_use_request.material.is_nil() && g_particle_use_request.material == asset.uid() &&
           ImGui::GetTime() < g_particle_use_request.expires_at;
}

/// Whether @p asset is one of the open project's: the ones the editor may write.
auto is_project_material(const asset_handle<material>& asset) -> bool
{
    return asset_actions::is_project_asset(asset.id());
}

/// What the Material row says of a material the emitter's sprites, trails and ribbons cannot draw.
auto get_held_material_text(particle_material_problem problem) -> const char*
{
    switch(problem)
    {
        case particle_material_problem::not_surface:
            return "This is not a Surface material, so the particles draw the engine's plain particle material in its "
                   "place.";
        case particle_material_problem::not_forward:
            return "This material draws in the G-buffer, where particles do not draw. Give its output the Blend Mode "
                   "Translucent, Premultiplied, Additive or Modulate and turn Particle Use on. Until then the particles "
                   "draw the engine's plain particle material.";
        case particle_material_problem::particle_use_off:
            return "This material's output has Particle Use off, so the particles draw the engine's plain particle "
                   "material in its place.";
        case particle_material_problem::no_shader:
            return "This material has no shader for this renderer yet, so the particles draw the engine's plain "
                   "particle material in its place. It is still compiling, or its compile failed and the log and the "
                   "Material Graph window say why.";
        case particle_material_problem::none:
        default:
            return "";
    }
}

/// What the picker says of a material offered to the Material row: that it draws particles, or why it does not.
auto get_offered_material_text(particle_material_problem problem, bool is_project_asset) -> const char*
{
    switch(problem)
    {
        case particle_material_problem::none:
            return "It draws particles.";
        case particle_material_problem::not_surface:
            return "It is not a Surface material, so particles cannot draw it.";
        case particle_material_problem::not_forward:
            return "It draws in the G-buffer, where particles do not draw. Its output needs the Blend Mode Translucent, "
                   "Premultiplied, Additive or Modulate, and Particle Use on.";
        case particle_material_problem::particle_use_off:
            return is_project_asset ? "Its output has Particle Use off, so particles cannot draw it yet. Pick it and the "
                                      "Material row offers to turn Particle Use on."
                                    : "Its output has Particle Use off, so particles cannot draw it.";
        case particle_material_problem::no_shader:
        default:
            return "It has no shader for this renderer yet, so particles cannot draw it. It is still compiling, or its "
                   "compile failed.";
    }
}

/// Whether the material with @p key suits the Material row of an emitter that draws sprites, trails or ribbons.
auto get_particle_material_fit(rtti::context& ctx, const std::string& key) -> asset_fit
{
    const auto asset = ctx.get_cached<asset_manager>().get_asset<material>(key);
    const auto loaded = asset.try_get();
    if(!loaded)
    {
        return {};
    }
    if(asset_handle<material>::is_empty(loaded))
    {
        return {asset_fit::state::unfit, "It could not be loaded."};
    }
    const auto problem = particle_emitter_component::find_material_problem(*loaded);
    return {problem == particle_material_problem::none ? asset_fit::state::fits : asset_fit::state::unfit,
            get_offered_material_text(problem, is_project_material(asset))};
}

/// The Material row of an emitter that draws sprites, trails or ribbons: its picker shows the materials that draw
/// particles apart from the rest, and a material the particles cannot draw is said at the row, with the fix where
/// there is one: Particle Use on the material's output, which a material of the project that draws in the translucency
/// pass takes.
void add_material_notes(rtti::context& ctx, const particle_emitter_component& data, row_notes& notes)
{
    if(data.get_render_mode() == ps_soa::render_mode::mesh)
    {
        return;
    }
    notes.set_fit_rule(EMITTER_MATERIAL_ROW,
                       {"Particles",
                        [&ctx](const std::string& key)
                        {
                            return get_particle_material_fit(ctx, key);
                        }});
    if(!data.has_unusable_material())
    {
        return;
    }
    const auto asset = data.get_render_material();
    const auto loaded = asset.try_get();
    if(!loaded)
    {
        return;
    }
    const auto problem = particle_emitter_component::find_material_problem(*loaded);
    if(problem != particle_material_problem::particle_use_off)
    {
        g_particle_use_request = {};
        notes.add(EMITTER_MATERIAL_ROW, {true, get_held_material_text(problem)});
        return;
    }
    if(awaits_particle_use(asset))
    {
        notes.add(EMITTER_MATERIAL_ROW,
                  {false, "The material was saved with Particle Use on. The particles draw it once it has compiled."});
        return;
    }
    row_note note;
    note.text = get_held_material_text(problem);
    if(is_project_material(asset))
    {
        note.fix_label = "Turn Particle Use On";
        note.fix_tooltip = "Saves the material with Particle Use on. It recompiles and the particles then draw it.";
        note.fix = [&ctx, asset]()
        {
            const auto held = asset.try_get();
            const auto* graph = held ? held->safe_cast<graph_material>() : nullptr;
            if(graph != nullptr && turn_particle_use_on(ctx, asset, *graph))
            {
                g_particle_use_request = {asset.uid(), ImGui::GetTime() + PARTICLE_USE_RELOAD_SECONDS};
            }
        };
    }
    notes.add(EMITTER_MATERIAL_ROW, std::move(note));
}

/// Why the particles of an emitter that collides with scene surfaces touch nothing here; null when they do.
auto find_scene_collision_gap(const particle_emitter_component& data) -> const char*
{
    if(data.get_render_mode() == ps_soa::render_mode::mesh)
    {
        return "Mesh particles do not touch scene surfaces, so this emitter's Collision does nothing.";
    }
    if(ps_soa::get_quality().scene_collision == ps_soa::scene_collision::off)
    {
        return "The project's Graphics > Particles > Scene Collision is Off, so the particles pass through every surface.";
    }
    if(!ps_soa::can_collide_with_scene())
    {
        return "This renderer does not collide particles with scene surfaces, so the particles pass through every surface.";
    }
    return nullptr;
}

/// Says at the Collision rows when the emitter's collision does less than they ask for: nothing in Local simulation
/// space, planes alone against physics colliders outside the game, where no physics world exists, and what a contact
/// with a scene surface leaves out.
void add_collision_notes(rtti::context& ctx, const particle_emitter_component& data, row_notes& notes)
{
    if(data.get_collision() == ps_soa::collision_mode::none)
    {
        return;
    }
    if(data.get_simulation_space() == ps_soa::simulation_space::local)
    {
        notes.add(EMITTER_COLLISION_ROW, {true, "Particles collide in World simulation space only, so this emitter's Collision does nothing."});
        return;
    }
    if(data.get_collide_with() == ps_soa::collision_source::scene_surfaces)
    {
        if(const char* gap = find_scene_collision_gap(data))
        {
            notes.add(EMITTER_COLLIDE_WITH_ROW, {true, gap});
            return;
        }
        notes.add(EMITTER_COLLIDE_WITH_ROW,
                  {false,
                   "The particles touch the opaque surfaces a camera draws while it shows them. The game is told of no "
                   "contact: none sends an event, leaves a decal or fires a sub-emitter, and deaths, particle lights and "
                   "the particle count follow the paths without the contacts."});
        return;
    }
    if(data.get_collide_with() == ps_soa::collision_source::physics_colliders && !ctx.get_cached<play_mode>().is_active())
    {
        notes.add(EMITTER_COLLIDE_WITH_ROW,
                  {false, "Physics colliders are touched while the game plays. Here the particles collide with the planes alone."});
    }
}

/// Why the particles of an emitter whose Ambient Light is Global Illumination keep the sky's light in every view, or
/// null: what the emitter's material and the renderer decide. Whether a view's Global Illumination runs and keeps
/// probes around the emitter is that view's (the statistics' Lit By GI counts the emitters it lit), and a project
/// whose particles are all unlit says so with its own setting.
auto find_gi_ambient_gap(const particle_emitter_component& data) -> const char*
{
    const graph_material* material = data.get_particle_material();
    if(material == nullptr)
    {
        return nullptr;
    }
    if(!fits_material_graph_particle_gi(uint32_t(material->get_textures().size()), material->get_scene_textures()))
    {
        return "The material reads more than two textures or the Scene Color, so the particles keep the sky's light.";
    }
    if(ps_soa::get_particle_data_storage() != shader_buffer_storage::buffer)
    {
        return "This renderer cannot give particles the Global Illumination's light. They keep the sky's light.";
    }
    return nullptr;
}

/// Says at the Ambient Light row when it asks for the Global Illumination's light and the particles cannot take it.
void add_ambient_light_notes(const particle_emitter_component& data, row_notes& notes)
{
    const bool asks = data.get_ambient_light() == ps_soa::particle_ambient::global_illumination &&
                      data.get_render_mode() != ps_soa::render_mode::mesh && data.draws_lit_material() &&
                      data.get_lighting() != ps_soa::particle_lighting::unlit;
    if(const char* gap = asks ? find_gi_ambient_gap(data) : nullptr)
    {
        notes.add(EMITTER_AMBIENT_LIGHT_ROW, {true, gap});
    }
}

/// Play, Pause / Resume, Stop and Clear in one row, on this emitter and, with the toggle, its child emitters.
auto draw_playback(particle_emitter_component& data) -> bool
{
    const bool is_playing = data.is_playing();
    const bool is_paused = data.is_paused();
    const bool with_children = g_with_child_emitters;
    bool is_changed = false;
    if(draw_playback_button("Play", !is_playing || is_paused, "Start emitting, from the start of the cycle when stopped."))
    {
        data.play();
        if(with_children)
        {
            data.play_child_emitters();
        }
        is_changed = true;
    }
    if(is_paused)
    {
        if(draw_playback_button("Resume", true, "Continue from where the particles froze."))
        {
            data.resume();
            if(with_children)
            {
                data.resume_child_emitters();
            }
            is_changed = true;
        }
    }
    else if(draw_playback_button("Pause", is_playing, "Freeze the particles where they are. They stay visible."))
    {
        data.pause();
        if(with_children)
        {
            data.pause_child_emitters();
        }
        is_changed = true;
    }
    if(draw_playback_button("Stop", is_playing, "Stop emitting. The particles alive live out their lives."))
    {
        data.stop();
        if(with_children)
        {
            data.stop_child_emitters();
        }
        is_changed = true;
    }
    if(draw_playback_button("Clear", true, "Stop emitting and remove every particle at once."))
    {
        data.stop_and_reset();
        if(with_children)
        {
            data.stop_and_reset_child_emitters();
        }
        is_changed = true;
    }
    ImGui::Checkbox("With Child Emitters", &g_with_child_emitters);
    ImGui::SetItemTooltip("The buttons drive every emitter in the entity's children too (the hierarchy below it). "
                          "Sub emitters fire on their own.");
    return is_changed;
}

} // namespace

auto inspector_particle_emitter_component::inspect(rtti::context& ctx,
                                                   entt::meta_any& var,
                                                   const meta_any_proxy& var_proxy,
                                                   const var_info& info,
                                                   const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result;
    auto& data = var.cast<particle_emitter_component&>();
    // What the emitter's rows say about their values, each at its row: the summary under the status names the rows
    // that warn and scrolls to them.
    row_notes notes(var.base().data());
    add_material_notes(ctx, data, notes);
    add_collision_notes(ctx, data, notes);
    add_ambient_light_notes(data, notes);
    draw_status(data);
    result.changed |= draw_playback(data);
    notes.draw_summary(var.type());
    ImGui::Spacing();
    result |= inspect_var_properties(ctx, var, var_proxy, info, custom);
    return result;
}
} // namespace unravel
