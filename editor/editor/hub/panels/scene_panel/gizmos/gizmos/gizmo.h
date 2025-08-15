#pragma once

#include "entt/meta/factory.hpp"
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/debugdraw.h>
#include <reflection/reflection.h>
#include <reflection/registration.h>

namespace unravel
{
class camera;

struct gizmo : crtp_meta_type<gizmo>
{
    REFLECTABLEV(gizmo)

    virtual ~gizmo() = default;

    virtual void draw(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd) = 0;
    virtual void draw_billboard(rtti::context& ctx, rttr::variant& var, const camera& cam, gfx::dd_raii& dd) = 0;

    template<typename T>
    auto create() -> std::shared_ptr<T>
    {
        return std::make_shared<T>();
    }
};

REFLECT_INLINE(gizmo)
{
    rttr::registration::class_<gizmo>("gizmo");

    entt::meta_factory<gizmo>{}.type("gizmo"_hs);
}
#define GIZMO_REFLECT(gizmo_renderer_type, inspected_type)                                                             \
    REFLECT_INLINE(gizmo_renderer_type)                                                                                \
    {                                                                                                                  \
        rttr::registration::class_<gizmo_renderer_type>(#gizmo_renderer_type)(                                         \
            rttr::metadata("inspected_type", rttr::type::get<inspected_type>()))                                       \
            .constructor<>()(rttr::policy::ctor::as_std_shared_ptr);                                                   \
        entt::meta_factory<gizmo_renderer_type>{}                                                                      \
            .type(entt::hashed_string{#gizmo_renderer_type})                                                           \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .func<&gizmo::create<gizmo_renderer_type>>("create"_hs);                                                   \
    }

} // namespace unravel
