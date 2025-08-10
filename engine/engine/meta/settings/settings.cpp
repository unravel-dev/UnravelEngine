#include "settings.hpp"

#include <fstream>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/array.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/input/input.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>

namespace unravel
{

REFLECT_INLINE(settings::app_settings)
{
    rttr::registration::class_<settings::app_settings>("app_settings")(rttr::metadata("pretty_name", "Application"))
        .constructor<>()()
        .property("company", &settings::app_settings::company)(rttr::metadata("pretty_name", "Company"),
                                                               rttr::metadata("tooltip", "Missing..."))
        .property("product", &settings::app_settings::product)(rttr::metadata("pretty_name", "Product"),
                                                               rttr::metadata("tooltip", "Missing..."))
        .property("version", &settings::app_settings::version)(rttr::metadata("pretty_name", "Version"),
                                                               rttr::metadata("tooltip", "Missing..."));
}

SAVE_INLINE(settings::app_settings)
{
    try_save(ar, ser20::make_nvp("company", obj.company));
    try_save(ar, ser20::make_nvp("product", obj.product));
    try_save(ar, ser20::make_nvp("version", obj.version));
}

LOAD_INLINE(settings::app_settings)
{
    try_load(ar, ser20::make_nvp("company", obj.company));
    try_load(ar, ser20::make_nvp("product", obj.product));
    try_load(ar, ser20::make_nvp("version", obj.version));
}


REFLECT_INLINE(settings::asset_settings::texture_importer_settings)
{
    rttr::registration::class_<settings::asset_settings::texture_importer_settings>("texture_importer_settings")(rttr::metadata("pretty_name", "Texture Importer Settings"))
        .constructor<>()()
        .property("default_max_size", &settings::asset_settings::texture_importer_settings::default_max_size)(
            rttr::metadata("pretty_name", "Default Max Size"),
            rttr::metadata("tooltip",
                           "The default maximum size for textures."))
        .property("default_compression", &settings::asset_settings::texture_importer_settings::default_compression)(
            rttr::metadata("pretty_name", "Default Compression"),
            rttr::metadata(
                "tooltip",
                "The default compression for textures."));
}

SAVE_INLINE(settings::asset_settings::texture_importer_settings)
{
    try_save(ar, ser20::make_nvp("default_max_size", obj.default_max_size));
    try_save(ar, ser20::make_nvp("default_compression", obj.default_compression));
}

LOAD_INLINE(settings::asset_settings::texture_importer_settings)
{
    try_load(ar, ser20::make_nvp("default_max_size", obj.default_max_size));
    try_load(ar, ser20::make_nvp("default_compression", obj.default_compression));
}

SAVE_INLINE(settings::asset_settings)
{
    try_save(ar, ser20::make_nvp("texture", obj.texture));
}

LOAD_INLINE(settings::asset_settings)
{
    try_load(ar, ser20::make_nvp("texture", obj.texture));
}


REFLECT_INLINE(settings::graphics_settings)
{
    rttr::registration::class_<settings::graphics_settings>("graphics_settings")(
        rttr::metadata("pretty_name", "Graphics"))
        .constructor<>()();
}

SAVE_INLINE(settings::graphics_settings)
{
    // try_save(ar, ser20::make_nvp("company", obj.company));
    // try_save(ar, ser20::make_nvp("product", obj.product));
    // try_save(ar, ser20::make_nvp("version", obj.version));
}

LOAD_INLINE(settings::graphics_settings)
{
    // try_load(ar, ser20::make_nvp("company", obj.company));
    // try_load(ar, ser20::make_nvp("product", obj.product));
    // try_load(ar, ser20::make_nvp("version", obj.version));
}

REFLECT_INLINE(settings::standalone_settings)
{
    rttr::registration::class_<settings::standalone_settings>("standalone_settings")(
        rttr::metadata("pretty_name", "Standalone"))
        .constructor<>()()
        .property("startup_scene",
                  &settings::standalone_settings::startup_scene)(rttr::metadata("pretty_name", "Startup Scene"),
                                                                 rttr::metadata("tooltip", "The scene to load first."));
}

SAVE_INLINE(settings::standalone_settings)
{
    try_save(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
}

LOAD_INLINE(settings::standalone_settings)
{
    try_load(ar, ser20::make_nvp("startup_scene", obj.startup_scene));
}

REFLECT_INLINE(settings::time_settings)
{
    rttr::registration::class_<settings::time_settings>("time_settings")(rttr::metadata("pretty_name", "Time"))
        .constructor<>()()
        .property("fixed_timestep", &settings::time_settings::fixed_timestep)(
            rttr::metadata("pretty_name", "Fixed Timestep"),
            rttr::metadata("step", 0.001f),
            rttr::metadata("tooltip",
                           "A framerate-idependent interval which dictates when physics calculations and FixedUpdate "
                           "events are performed."))
        .property("max_fixed_steps", &settings::time_settings::max_fixed_steps)(
            rttr::metadata("pretty_name", "Max Fixed Steps"),
            rttr::metadata(
                "tooltip",
                "A cap for framerate-idependent worst case scenario. No more than this much fixed updates per frame."));
}

SAVE_INLINE(settings::time_settings)
{
    try_save(ar, ser20::make_nvp("fixed_timestep", obj.fixed_timestep));
    try_save(ar, ser20::make_nvp("max_fixed_steps", obj.max_fixed_steps));
}

LOAD_INLINE(settings::time_settings)
{
    try_load(ar, ser20::make_nvp("fixed_timestep", obj.fixed_timestep));
    try_load(ar, ser20::make_nvp("max_fixed_steps", obj.max_fixed_steps));
}

REFLECT_INLINE(settings::layer_settings)
{
    rttr::registration::class_<settings::layer_settings>("layer_settings")(rttr::metadata("pretty_name", "Layer"))
        .constructor<>()()
        .property("layers", &settings::layer_settings::layers)(rttr::metadata("pretty_name", "Layers"),
                                                               rttr::metadata("readonly_count", get_reserved_layers().size()),
                                                               rttr::metadata("tooltip", ""));
}

SAVE_INLINE(settings::layer_settings)
{
    try_save(ar, ser20::make_nvp("layers", obj.layers));
}

LOAD_INLINE(settings::layer_settings)
{
    try_load(ar, ser20::make_nvp("layers", obj.layers));
}

SAVE_INLINE(settings::input_settings)
{
    try_save(ar, ser20::make_nvp("actions", obj.actions));
}

LOAD_INLINE(settings::input_settings)
{
    try_load(ar, ser20::make_nvp("actions", obj.actions));
}


SAVE_INLINE(settings::resolution_settings::resolution)
{
    try_save(ar, ser20::make_nvp("name", obj.name));
    try_save(ar, ser20::make_nvp("width", obj.width));
    try_save(ar, ser20::make_nvp("height", obj.height));
    try_save(ar, ser20::make_nvp("aspect", obj.aspect));
}

LOAD_INLINE(settings::resolution_settings::resolution)
{
    try_load(ar, ser20::make_nvp("name", obj.name));
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("height", obj.height));
    try_load(ar, ser20::make_nvp("aspect", obj.aspect));
}

SAVE_INLINE(settings::resolution_settings)
{
    try_save(ar, ser20::make_nvp("resolutions", obj.resolutions));
}

LOAD_INLINE(settings::resolution_settings)
{
    try_load(ar, ser20::make_nvp("resolutions", obj.resolutions));
}

REFLECT_INLINE(settings::resolution_settings::resolution)
{
    rttr::registration::class_<settings::resolution_settings::resolution>("resolution")(
        rttr::metadata("pretty_name", "Resolution"))
        .constructor<>()()
        .property("name", &settings::resolution_settings::resolution::name)(
            rttr::metadata("pretty_name", "Name"),
            rttr::metadata("tooltip", "Display name for this resolution"))
        .property("width", &settings::resolution_settings::resolution::width)(
            rttr::metadata("pretty_name", "Width"),
            rttr::metadata("min", 0),
            rttr::metadata("tooltip", "Width in pixels (0 for free aspect)"))
        .property("height", &settings::resolution_settings::resolution::height)(
            rttr::metadata("pretty_name", "Height"),
            rttr::metadata("min", 0),
            rttr::metadata("tooltip", "Height in pixels (0 for free aspect)"))
        .property("aspect", &settings::resolution_settings::resolution::aspect)(
            rttr::metadata("pretty_name", "Aspect Ratio"),
            rttr::metadata("min", 0.0f),
            rttr::metadata("tooltip", "Aspect ratio (0 for free aspect)"));
}

REFLECT_INLINE(settings::resolution_settings)
{
    rttr::registration::class_<settings::resolution_settings>("resolution_settings")(
        rttr::metadata("pretty_name", "Resolution Settings"))
        .constructor<>()()
        .property("resolutions", &settings::resolution_settings::resolutions)(
            rttr::metadata("pretty_name", "Resolutions"),
            rttr::metadata("tooltip", "List of available resolutions"));
}

REFLECT(settings)
{
    rttr::registration::class_<settings>("settings")(rttr::metadata("pretty_name", "Settings"))
        .constructor<>()()
        .property("app", &settings::app)(rttr::metadata("pretty_name", "Application"),
                                         rttr::metadata("tooltip", "Missing..."))
        .property("graphics", &settings::graphics)(rttr::metadata("pretty_name", "Graphics"),
                                                   rttr::metadata("tooltip", "Missing..."))
        .property("standalone", &settings::standalone)(rttr::metadata("pretty_name", "Standalone"),
                                                       rttr::metadata("tooltip", "Missing..."))
        .property("resolution", &settings::resolution)(rttr::metadata("pretty_name", "Resolution"),
                                                       rttr::metadata("tooltip", "Resolution settings for the project"));
}

SAVE(settings)
{
    try_save(ar, ser20::make_nvp("app", obj.app));
    try_save(ar, ser20::make_nvp("assets", obj.assets));
    try_save(ar, ser20::make_nvp("graphics", obj.graphics));
    try_save(ar, ser20::make_nvp("standalone", obj.standalone));
    try_save(ar, ser20::make_nvp("layer", obj.layer));
    try_save(ar, ser20::make_nvp("input", obj.input));
    try_save(ar, ser20::make_nvp("time", obj.time));
    try_save(ar, ser20::make_nvp("resolutions", obj.resolution.resolutions));
}
SAVE_INSTANTIATE(settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(settings, ser20::oarchive_binary_t);

LOAD(settings)
{
    try_load(ar, ser20::make_nvp("app", obj.app));
    try_load(ar, ser20::make_nvp("assets", obj.assets));
    try_load(ar, ser20::make_nvp("graphics", obj.graphics));
    try_load(ar, ser20::make_nvp("standalone", obj.standalone));
    try_load(ar, ser20::make_nvp("layer", obj.layer));
    try_load(ar, ser20::make_nvp("input", obj.input));
    try_load(ar, ser20::make_nvp("time", obj.time));
    try_load(ar, ser20::make_nvp("resolutions", obj.resolution.resolutions));
}
LOAD_INSTANTIATE(settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(settings, ser20::iarchive_binary_t);

void save_to_file(const std::string& absolute_path, const settings& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const settings& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

auto load_from_file(const std::string& absolute_path, settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        return try_load(ar, ser20::make_nvp("settings", obj));
    }

    return false;
}

auto load_from_file_bin(const std::string& absolute_path, settings& obj) -> bool
{
    std::ifstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::iarchive_binary_t ar(stream);
        return try_load(ar, ser20::make_nvp("settings", obj));
    }

    return false;
}

} // namespace unravel
