#include "model_component.hpp"

#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/model.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(model_component)
{
    rttr::registration::class_<model_component>("model_component")(rttr::metadata("category", "RENDERING"),
                                                                   rttr::metadata("pretty_name", "Model"))
        .constructor<>()
        .method("component_exists", &component_exists<model_component>)

        .property("enabled", &model_component::is_enabled, &model_component::set_enabled)(
            rttr::metadata("pretty_name", "Enabled"),
            rttr::metadata("tooltip", "Is the model visible?"))
        .property("static", &model_component::is_static, &model_component::set_static)(
            rttr::metadata("pretty_name", "Static"),
            rttr::metadata("tooltip", "Is the model static?"))
        .property("casts_shadow", &model_component::casts_shadow, &model_component::set_casts_shadow)(
            rttr::metadata("pretty_name", "Casts Shadow"),
            rttr::metadata("tooltip", "Is the model casting shadows?"))
        .property("casts_reflection", &model_component::casts_reflection, &model_component::set_casts_reflection)(
            rttr::metadata("pretty_name", "Casts Reflection"),
            rttr::metadata("tooltip", "Is the model participating in reflection generation?"))
        .property("model", &model_component::get_model, &model_component::set_model)(
            rttr::metadata("pretty_name", "Model"));

    // Register model_component class with entt
    entt::meta_factory<model_component>{}
        .type("model_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Model"},
        })
        .func<&component_exists<model_component>>("component_exists"_hs)
        .data<&model_component::set_enabled, &model_component::is_enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Is the model visible?"},
        })
        .data<&model_component::set_static, &model_component::is_static>("static"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "static"},
            entt::attribute{"pretty_name", "Static"},
            entt::attribute{"tooltip", "Is the model static?"},
        })
        .data<&model_component::set_casts_shadow, &model_component::casts_shadow>("casts_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip", "Is the model casting shadows?"},
        })
        .data<&model_component::set_casts_reflection, &model_component::casts_reflection>("casts_reflection"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_reflection"},
            entt::attribute{"pretty_name", "Casts Reflection"},
            entt::attribute{"tooltip", "Is the model participating in reflection generation?"},
        })
        .data<&model_component::set_model, &model_component::get_model>("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
        });
}

SAVE(model_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("static", obj.is_static()));
    try_save(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow()));
    try_save(ar, ser20::make_nvp("casts_reflection", obj.casts_reflection()));
    try_save(ar, ser20::make_nvp("model", obj.get_model()));
}
SAVE_INSTANTIATE(model_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model_component, ser20::oarchive_binary_t);

LOAD(model_component)
{
    bool is_enabled{true};
    if(try_load(ar, ser20::make_nvp("enabled", is_enabled)))
    {
        obj.set_enabled(is_enabled);
    }

    bool is_static{};
    if(try_load(ar, ser20::make_nvp("static", is_static)))
    {
        obj.set_static(is_static);
    }

    bool casts_shadow{};
    if(try_load(ar, ser20::make_nvp("casts_shadow", casts_shadow)))
    {
        obj.set_casts_shadow(casts_shadow);
    }

    bool casts_reflection{};
    if(try_load(ar, ser20::make_nvp("casts_reflection", casts_reflection)))
    {
        obj.set_casts_reflection(casts_reflection);
    }

    auto mod = obj.get_model();
    if(try_load(ar, ser20::make_nvp("model", mod)))
    {
        obj.set_model(mod);
    }
}
LOAD_INSTANTIATE(model_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model_component, ser20::iarchive_binary_t);

REFLECT(bone_component)
{
    rttr::registration::class_<bone_component>("bone_component")(rttr::metadata("category", "RENDERING"),
                                                                 rttr::metadata("pretty_name", "Bone"))
        .constructor<>()
        .method("component_exists", &component_exists<bone_component>)
        .property_readonly("bone_index", &bone_component::bone_index)(
            rttr::metadata("pretty_name", "Bone Index"),
            rttr::metadata("tooltip", "The bone index this object represents."));

    // Register bone_component class with entt
    entt::meta_factory<bone_component>{}
        .type("bone_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Bone"},
        })
        .func<&component_exists<bone_component>>("component_exists"_hs)
        .data<nullptr, &bone_component::bone_index>("bone_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_index"},
            entt::attribute{"pretty_name", "Bone Index"},
            entt::attribute{"tooltip", "The bone index this object represents."},
        });
}

SAVE(bone_component)
{
    try_save(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
SAVE_INSTANTIATE(bone_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(bone_component, ser20::oarchive_binary_t);

LOAD(bone_component)
{
    try_load(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
LOAD_INSTANTIATE(bone_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(bone_component, ser20::iarchive_binary_t);

REFLECT(submesh_component)
{
    rttr::registration::class_<submesh_component>("submesh_component")(rttr::metadata("category", "RENDERING"),
                                                                       rttr::metadata("pretty_name", "Submesh"))
        .constructor<>()
        .method("component_exists", &component_exists<submesh_component>)
        .property_readonly("submeshes", &submesh_component::submeshes)(
            rttr::metadata("pretty_name", "Submeshes"),
            rttr::metadata("tooltip", "Submeshes affected by this node."));

    // Register submesh_component class with entt
    entt::meta_factory<submesh_component>{}
        .type("submesh_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Submesh"},
        })
        .func<&component_exists<submesh_component>>("component_exists"_hs)
        .data<nullptr, &submesh_component::submeshes>("submeshes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submeshes"},
            entt::attribute{"pretty_name", "Submeshes"},
            entt::attribute{"tooltip", "Submeshes affected by this node."},
        });
}

SAVE(submesh_component)
{
    try_save(ar, ser20::make_nvp("submeshes", obj.submeshes));
}
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_binary_t);

LOAD(submesh_component)
{
    try_load(ar, ser20::make_nvp("submeshes", obj.submeshes));
}
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_binary_t);

} // namespace unravel
