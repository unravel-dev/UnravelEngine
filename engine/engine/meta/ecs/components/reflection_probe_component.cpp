#include "reflection_probe_component.hpp"
#include <engine/meta/core/math/vector.hpp>
#include <engine/meta/rendering/reflection_probe.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(reflection_probe_component)
{
    auto realtime_predicate_entt = entt::property_predicate<bool>(
        [](const entt::meta_any& obj)
        {
            auto data = obj.try_cast<reflection_probe_component>();
            return data != nullptr && data->get_update_mode() == probe_update_mode::realtime;
        });

    entt::meta_factory<reflection_probe_component>{}
        .type("reflection_probe_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reflection_probe_component"},
            entt::attribute{"category", "LIGHTING"},
            entt::attribute{"pretty_name", "Reflection Probe"},
        })
        .func<&component_meta<reflection_probe_component>::exists>("component_exists"_hs)
        .func<&component_meta<reflection_probe_component>::add>("component_add"_hs)
        .func<&component_meta<reflection_probe_component>::remove>("component_remove"_hs)
        .func<&component_meta<reflection_probe_component>::save>("component_save"_hs)
        .func<&component_meta<reflection_probe_component>::load>("component_load"_hs)
        .data<&reflection_probe_component::set_probe, &reflection_probe_component::get_probe>("probe"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "probe"},
            entt::attribute{"pretty_name", "Probe"},
        })
        .data<&reflection_probe_component::set_update_mode, &reflection_probe_component::get_update_mode>("update_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "update_mode"},
            entt::attribute{"pretty_name", "Update Mode"},
            entt::attribute{"group", "Update"},
            entt::attribute{"tooltip",
                "On Demand: never refreshes until explicitly requested (runtime default, cheapest)."
                "\nOnce: bakes a single time on load or after edits, then stops."
                "\nRealtime: refreshes continuously, time-sliced by Faces Per Frame and Update Interval."},
        })
        .data<&reflection_probe_component::set_update_interval, &reflection_probe_component::get_update_interval>("update_interval"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "update_interval"},
            entt::attribute{"pretty_name", "Update Interval (s)"},
            entt::attribute{"group", "Update"},
            entt::attribute{"tooltip",
                "Seconds between refreshes when Update Mode is Realtime. 0 means every available frame."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"predicate", realtime_predicate_entt},
        })
        .data<&reflection_probe_component::set_faces_per_frame, &reflection_probe_component::get_faces_per_frame>("faces_per_frame"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "faces_per_frame"},
            entt::attribute{"pretty_name", "Faces Per Frame"},
            entt::attribute{"group", "Update"},
            entt::attribute{"tooltip",
                "Number of cubemap faces to refresh per frame while a bake is in progress. "
                "Higher values finish bakes faster but cost more per frame. "
                "Only applies while the probe is dirty; probes at rest cost nothing."},
            entt::attribute{"min", 1},
            entt::attribute{"max", 6},
        })
        .data<&reflection_probe_component::set_apply_prefilter, &reflection_probe_component::get_apply_prefilter>("apply_prefilter"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "apply_prefilter"},
            entt::attribute{"pretty_name", "Apply Prefilter"},
            entt::attribute{"group", "Update"},
            entt::attribute{"tooltip", "Enables prefiltering which improves quality but may impact performance"},
        })
        .data<&reflection_probe_component::set_resolution, &reflection_probe_component::get_resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"group", "Capture"},
            entt::attribute{"tooltip", "Cubemap face resolution in pixels. Higher values improve quality but increase bake cost and memory."},
        })
        .data<&reflection_probe_component::set_capture_sky, &reflection_probe_component::get_capture_sky>("capture_sky"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "capture_sky"},
            entt::attribute{"pretty_name", "Capture Sky"},
            entt::attribute{"group", "Capture"},
            entt::attribute{"tooltip",
                "When enabled, the atmospheric sky pass is rendered into the cubemap. "
                "Disable for interior or local probes that should only reflect nearby geometry."},
        })
        .data<&reflection_probe_component::set_capture_shadows, &reflection_probe_component::get_capture_shadows>("capture_shadows"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "capture_shadows"},
            entt::attribute{"pretty_name", "Capture Shadows"},
            entt::attribute{"group", "Capture"},
            entt::attribute{"tooltip",
                "When enabled, shadow maps are built and sampled during cubemap capture. "
                "Disable to reduce bake cost when shadows are not needed in reflections."},
        });
}

SAVE(reflection_probe_component)
{
    try_save(ar, ser20::make_nvp("probe", obj.get_probe()));
    try_save(ar, ser20::make_nvp("update_mode", obj.get_update_mode()));
    try_save(ar, ser20::make_nvp("update_interval", obj.get_update_interval()));
    try_save(ar, ser20::make_nvp("faces_per_frame", obj.get_faces_per_frame()));
    try_save(ar, ser20::make_nvp("apply_prefilter", obj.get_apply_prefilter()));
    try_save(ar, ser20::make_nvp("resolution", obj.get_resolution()));
    try_save(ar, ser20::make_nvp("capture_sky", obj.get_capture_sky()));
    try_save(ar, ser20::make_nvp("capture_shadows", obj.get_capture_shadows()));
}
SAVE_INSTANTIATE(reflection_probe_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(reflection_probe_component, ser20::oarchive_binary_t);

LOAD(reflection_probe_component)
{
    // Load update_mode before the probe so that set_probe's on_demand gate sees the correct mode.
    probe_update_mode update_mode = obj.get_update_mode();
    if(try_load(ar, ser20::make_nvp("update_mode", update_mode)))
    {
        obj.set_update_mode(update_mode);
    }

    float update_interval = obj.get_update_interval();
    if(try_load(ar, ser20::make_nvp("update_interval", update_interval)))
    {
        obj.set_update_interval(update_interval);
    }

    reflection_probe probe;
    if(try_load(ar, ser20::make_nvp("probe", probe)))
    {
        obj.set_probe(probe);
    }

    size_t faces_per_frame = obj.get_faces_per_frame();
    if(try_load(ar, ser20::make_nvp("faces_per_frame", faces_per_frame)))
    {
        obj.set_faces_per_frame(faces_per_frame);
    }

    bool apply_prefilter = false;
    if(try_load(ar, ser20::make_nvp("apply_prefilter", apply_prefilter)))
    {
        obj.set_apply_prefilter(apply_prefilter);
    }

    probe_resolution resolution = obj.get_resolution();
    if(try_load(ar, ser20::make_nvp("resolution", resolution)))
    {
        obj.set_resolution(resolution);
    }

    bool capture_sky = obj.get_capture_sky();
    if(try_load(ar, ser20::make_nvp("capture_sky", capture_sky)))
    {
        obj.set_capture_sky(capture_sky);
    }

    bool capture_shadows = obj.get_capture_shadows();
    if(try_load(ar, ser20::make_nvp("capture_shadows", capture_shadows)))
    {
        obj.set_capture_shadows(capture_shadows);
    }
}
LOAD_INSTANTIATE(reflection_probe_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(reflection_probe_component, ser20::iarchive_binary_t);
} // namespace unravel
