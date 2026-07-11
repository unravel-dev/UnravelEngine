#pragma once

#include <engine/engine_export.h>

#include <context/context.hpp>
#include <entt/entt.hpp>
#include <seq/seq_action.h>
#include <seq/seq_common.h>

namespace unravel
{

struct scene;

struct splash_scene_state
{
    seq::seq_id_t action = 0;
    seq::seq_action pending_sequence;
    entt::handle ui_entity;
    bool setup_failed = false;
};

namespace splash_scene
{

[[nodiscard]] auto get_document_key(rtti::context& ctx) -> std::string;

[[nodiscard]] auto has_content(rtti::context& ctx) -> bool;

void setup(rtti::context& ctx, scene& scn, splash_scene_state& state);

void update(rtti::context& ctx, splash_scene_state& state);

[[nodiscard]] auto is_finished(const splash_scene_state& state) -> bool;

void teardown(splash_scene_state& state);

} // namespace splash_scene

} // namespace unravel
