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

struct dd_2d_raii
{
    std::vector<std::function<void()>> callbacks;
};

struct gizmo : crtp_meta_type<gizmo>
{
    virtual ~gizmo() = default;

    virtual void draw(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd, dd_2d_raii& dd_2d) = 0;
    virtual void draw_billboard(rtti::context& ctx, entt::meta_any& var, const camera& cam, gfx::dd_raii& dd) = 0;

    template<typename T>
    static void create_and_register(const entt::meta_type& inspected_type,
                                    std::unordered_map<entt::id_type, std::shared_ptr<gizmo>>& type_map)
    {
        type_map[inspected_type.info().index()] = std::make_shared<T>();
    }
};

REFLECT_INLINE(gizmo)
{
    entt::meta_factory<gizmo>{}.type("gizmo"_hs);
}
#define GIZMO_REFLECT(gizmo_renderer_type, inspected_type)                                                             \
    REFLECT_INLINE(gizmo_renderer_type)                                                                                \
    {                                                                                                                  \
        entt::meta_factory<gizmo_renderer_type>{}                                                                      \
            .type(entt::hashed_string{#gizmo_renderer_type})                                                           \
            .custom<entt::attributes>(                                                                                 \
                entt::attributes{entt::attribute{"inspected_type", entt::resolve<inspected_type>()}})                  \
            .base<gizmo>()                                                                                             \
            .func<&gizmo::create_and_register<gizmo_renderer_type>>("create_and_register"_hs);                         \
    }

} // namespace unravel
