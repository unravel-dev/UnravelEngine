#include "animation_preview.h"

#include <engine/animation/ecs/components/animation_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/events.h>
#include <engine/rendering/ecs/components/model_component.h>

#include <editor/events.h>

#include <algorithm>

namespace unravel
{
namespace
{
/// Frames a preview survives without its tool drawing it (one is the frame it was drawn in).
constexpr uint64_t PREVIEW_GRACE_FRAMES = 2;

/// Every preview that exists, so saves can stop them all; editor tools use them on the main thread only.
auto get_previews() -> std::vector<animation_entity_preview*>&
{
    static std::vector<animation_entity_preview*> previews;
    return previews;
}
} // namespace

void animation_pose_snapshot::capture(entt::handle entity)
{
    clear();
    if(!entity || !entity.valid() || !entity.all_of<transform_component>())
    {
        return;
    }
    owner_ = entity;
    owner_id_ = entt::make_uhandle(entity);
    if(auto* model = entity.try_get<model_component>())
    {
        for(const auto& bone : model->get_armature_entities())
        {
            if(bone && bone.valid() && bone.all_of<transform_component>())
            {
                entries_.push_back({bone, bone.get<transform_component>().get_transform_local()});
            }
        }
    }
    entries_.push_back({entity, entity.get<transform_component>().get_transform_local()});
}

void animation_pose_snapshot::restore()
{
    // A scene reloaded in between may reuse the entity ids for other entities.
    const bool same_owner = owner_ && owner_.valid() && owner_id_.resolve() == owner_;
    if(same_owner)
    {
        for(const auto& entry : entries_)
        {
            if(entry.entity && entry.entity.valid() && entry.entity.all_of<transform_component>())
            {
                entry.entity.get<transform_component>().set_transform_local(entry.local);
            }
        }
    }
    clear();
}

void animation_pose_snapshot::clear()
{
    entries_.clear();
    owner_ = {};
    owner_id_ = {};
}

auto animation_pose_snapshot::is_empty() const -> bool
{
    return entries_.empty();
}

animation_entity_preview::animation_entity_preview()
{
    get_previews().push_back(this);
}

animation_entity_preview::~animation_entity_preview()
{
    auto& previews = get_previews();
    previews.erase(std::remove(previews.begin(), previews.end(), this), previews.end());
}

void animation_entity_preview::stop_all()
{
    for(auto* preview : get_previews())
    {
        preview->stop();
    }
}

auto animation_entity_preview::start(rtti::context& ctx, entt::handle entity) -> animation_component*
{
    stop();
    auto* animation = entity && entity.valid() ? entity.try_get<animation_component>() : nullptr;
    if(animation == nullptr)
    {
        return nullptr;
    }
    // Another preview of the entity puts its own pose back first, so that pose is what this one captures.
    for(auto* other : get_previews())
    {
        if(other != this && other->entity_ == entity)
        {
            other->stop();
        }
    }
    connect(ctx);
    snapshot_.capture(entity);
    entity_ = entity;
    alive_frame_ = frame_;
    return animation;
}

void animation_entity_preview::stop()
{
    if(!entity_)
    {
        return;
    }
    if(entity_.valid())
    {
        if(auto* animation = entity_.try_get<animation_component>())
        {
            auto& player = animation->get_player();
            player.stop();
            for(size_t layer = 0; layer < player.get_layers().size(); ++layer)
            {
                player.clear(layer);
            }
        }
    }
    snapshot_.restore();
    entity_ = {};
}

void animation_entity_preview::keep_alive()
{
    alive_frame_ = frame_;
}

auto animation_entity_preview::is_active() const -> bool
{
    return entity_ && entity_.valid() && entity_.all_of<animation_component>();
}

auto animation_entity_preview::get_entity() const -> entt::handle
{
    return entity_;
}

void animation_entity_preview::connect(rtti::context& ctx)
{
    if(connected_)
    {
        return;
    }
    auto& ev = ctx.get_cached<events>();
    ev.on_frame_end.connect(sentinel_, this, &animation_entity_preview::on_frame_end);
    ctx.get_cached<ui_events>().on_close_project.connect(sentinel_, this, &animation_entity_preview::on_close_project);
    connected_ = true;
}

void animation_entity_preview::on_frame_end(rtti::context& ctx, const frame_times& times)
{
    ++frame_;
    if(entity_ && frame_ - alive_frame_ > PREVIEW_GRACE_FRAMES)
    {
        stop();
    }
}

void animation_entity_preview::on_close_project(rtti::context& ctx)
{
    stop();
}

auto animation_clip_preview::start(rtti::context& ctx, entt::handle entity, const asset_handle<animation_clip>& clip)
    -> bool
{
    stop();
    if(!clip)
    {
        return false;
    }
    auto* animation = preview_.start(ctx, entity);
    if(animation == nullptr)
    {
        return false;
    }
    clip_ = clip;
    auto& player = animation->get_player();
    player.stop();
    animation_play_params params;
    params.duration = animation_play_params::seconds_t(0);
    params.loop = true;
    player.play_clip(0, clip, params);
    player.play();
    player.pause();
    set_progress(0.0f);
    return true;
}

void animation_clip_preview::stop()
{
    preview_.stop();
    clip_ = {};
}

void animation_clip_preview::set_progress(float progress)
{
    if(!is_active())
    {
        return;
    }
    auto& player = preview_.get_entity().get<animation_component>().get_player();
    player.seek(0, std::clamp(progress, 0.0f, 1.0f));
    player.request_pose_refresh();
}

void animation_clip_preview::keep_alive()
{
    preview_.keep_alive();
}

auto animation_clip_preview::is_active() const -> bool
{
    return preview_.is_active();
}

auto animation_clip_preview::get_entity() const -> entt::handle
{
    return preview_.get_entity();
}

auto animation_clip_preview::get_clip() const -> const asset_handle<animation_clip>&
{
    return clip_;
}

} // namespace unravel
