#include "reflection_probe_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/reflection_probe.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(reflection_probe_component)
{

    rttr::registration::class_<reflection_probe_component>("reflection_probe_component")(
        rttr::metadata("category", "LIGHTING"),
        rttr::metadata("pretty_name", "Reflection Probe"))
        .constructor<>()
        .method("component_exists", &component_exists<reflection_probe_component>)

        .property("probe", &reflection_probe_component::get_probe, &reflection_probe_component::set_probe)(
            rttr::metadata("pretty_name", "Probe"))
        .property("faces_per_frame", &reflection_probe_component::get_faces_per_frame, &reflection_probe_component::set_faces_per_frame)(
            rttr::metadata("pretty_name", "Faces Per Frame"),
            rttr::metadata("min", 1),
            rttr::metadata("max", 6))
        .property("apply_prefilter", &reflection_probe_component::get_apply_prefilter, &reflection_probe_component::set_apply_prefilter)(
            rttr::metadata("pretty_name", "Apply Prefilter"),
            rttr::metadata("tooltip", "Enables prefiltering which improves quality but may impact performance"));

    // Register reflection_probe_component class with entt
    entt::meta_factory<reflection_probe_component>{}
        .type("reflection_probe_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_probe_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Reflection Probe"},
        })
        .func<&component_exists<reflection_probe_component>>("component_exists"_hs)
        .data<&reflection_probe_component::set_probe, &reflection_probe_component::get_probe>("probe"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe"},
            entt::attribute{"pretty_name", "Probe"},
        })
        .data<&reflection_probe_component::set_faces_per_frame, &reflection_probe_component::get_faces_per_frame>("faces_per_frame"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "faces_per_frame"},
            entt::attribute{"pretty_name", "Faces Per Frame"},
            entt::attribute{"min", 1},
            entt::attribute{"max", 6},
        })
        .data<&reflection_probe_component::set_apply_prefilter, &reflection_probe_component::get_apply_prefilter>("apply_prefilter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "apply_prefilter"},
            entt::attribute{"pretty_name", "Apply Prefilter"},
            entt::attribute{"tooltip", "Enables prefiltering which improves quality but may impact performance"},
        });
}

SAVE(reflection_probe_component)
{
    try_save(ar, ser20::make_nvp("probe", obj.get_probe()));
    try_save(ar, ser20::make_nvp("faces_per_frame", obj.get_faces_per_frame()));
    try_save(ar, ser20::make_nvp("apply_prefilter", obj.get_apply_prefilter()));
}
SAVE_INSTANTIATE(reflection_probe_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(reflection_probe_component, ser20::oarchive_binary_t);

LOAD(reflection_probe_component)
{
    reflection_probe probe;
    if(try_load(ar, ser20::make_nvp("probe", probe)))
    {
        obj.set_probe(probe);
    }

    size_t faces_per_frame = 1;
    if(try_load(ar, ser20::make_nvp("faces_per_frame", faces_per_frame)))
    {
        obj.set_faces_per_frame(faces_per_frame);
    }

    bool apply_prefilter = false;
    if(try_load(ar, ser20::make_nvp("apply_prefilter", apply_prefilter)))
    {
        obj.set_apply_prefilter(apply_prefilter);
    }
}
LOAD_INSTANTIATE(reflection_probe_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(reflection_probe_component, ser20::iarchive_binary_t);
} // namespace unravel
