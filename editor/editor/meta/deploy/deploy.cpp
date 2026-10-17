#include "deploy.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/array.hpp>
#include <serialization/types/string.hpp>

namespace unravel
{
REFLECT(deploy_settings)
{
    entt::meta_factory<deploy_settings>{}
        .type("deploy_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Deploy Options"},
        })
        .data<&deploy_settings::deploy_location>("deploy_location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_location"},
            entt::attribute{"pretty_name", "Deploy Location"},
            entt::attribute{"tooltip", "The folder the game is built into."},
        })
        .data<&deploy_settings::deploy_dependencies>("deploy_dependencies"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_dependencies"},
            entt::attribute{"pretty_name", "Deploy Dependencies"},
            entt::attribute{"tooltip", "Copy the player executable and its libraries as well.\nUnchanged files are skipped."},
        })
        .data<&deploy_settings::deploy_cooked_assets>("deploy_cooked_assets"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_cooked_assets"},
            entt::attribute{"pretty_name", "Deploy Cooked Assets"},
            entt::attribute{"tooltip",
                            "Resolve nested prefab instances once, at deploy time, so the game\n"
                            "does not refresh them on every load.\n\n"
                            "The deploy rewrites this marker on every run, so what it says is\n"
                            "always what this build produced.\n\n"
                            "Off: the marker is cleared and the game refreshes nested instances\n"
                            "as it loads them - slower, but it cannot serve stale content."},
        })
        .data<&deploy_settings::deploy_debug_symbols>("deploy_debug_symbols"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_debug_symbols"},
            entt::attribute{"pretty_name", "Deploy Debug Symbols"},
            entt::attribute{"tooltip",
                            "Ship the scripts' debug symbols (.pdb) and docs (.xml).\n\n"
                            "The game runs without them; with them, script exceptions\n"
                            "name the file and line they came from."},
        })
        .data<&deploy_settings::deploy_packed_assets>("deploy_packed_assets"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_packed_assets"},
            entt::attribute{"pretty_name", "Pack Assets"},
            entt::attribute{"tooltip",
                            "Put the compiled assets of the engine and of the game into one\n"
                            "compressed file each (content.pak) instead of thousands of loose files.\n\n"
                            "Off: the files ship loose, easier to inspect. The game reads either."},
        })
        .data<&deploy_settings::deploy_patch_release>("deploy_patch_release"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_patch_release"},
            entt::attribute{"pretty_name", "Patch Against Release"},
            entt::attribute{"tooltip",
                            "A release kept with Keep as Release (its name, the product version\n"
                            "it was kept at). Its packs ship unchanged and content_p1.pak holds\n"
                            "every change since, so players who have it download only the patch.\n\n"
                            "None: a full build."},
        })
        .data<&deploy_settings::deploy_and_run>("deploy_and_run"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_and_run"},
            entt::attribute{"pretty_name", "Run After Deploy"},
            entt::attribute{"tooltip", "Start the game once the deploy is done."},
        });
}

SAVE(deploy_settings)
{
    try_save(ar, ser20::make_nvp("deploy_location", obj.deploy_location.generic_string()));
    try_save(ar, ser20::make_nvp("deploy_dependencies", obj.deploy_dependencies));
    try_save(ar, ser20::make_nvp("deploy_and_run", obj.deploy_and_run));
    try_save(ar, ser20::make_nvp("deploy_cooked_assets", obj.deploy_cooked_assets));
    try_save(ar, ser20::make_nvp("deploy_debug_symbols", obj.deploy_debug_symbols));
    try_save(ar, ser20::make_nvp("deploy_packed_assets", obj.deploy_packed_assets));
    try_save(ar, ser20::make_nvp("deploy_patch_release", obj.deploy_patch_release));
}
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_binary_t);

LOAD(deploy_settings)
{
    std::string deploy_location;
    if(try_load(ar, ser20::make_nvp("deploy_location", deploy_location)))
    {
        obj.deploy_location = deploy_location;
    }

    try_load(ar, ser20::make_nvp("deploy_dependencies", obj.deploy_dependencies));
    try_load(ar, ser20::make_nvp("deploy_and_run", obj.deploy_and_run));
    try_load(ar, ser20::make_nvp("deploy_cooked_assets", obj.deploy_cooked_assets));
    try_load(ar, ser20::make_nvp("deploy_debug_symbols", obj.deploy_debug_symbols));
    try_load(ar, ser20::make_nvp("deploy_packed_assets", obj.deploy_packed_assets));
    try_load(ar, ser20::make_nvp("deploy_patch_release", obj.deploy_patch_release));
}
LOAD_INSTANTIATE(deploy_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(deploy_settings, ser20::iarchive_binary_t);


void save_to_file(const std::string& absolute_path, const deploy_settings& obj)
{
    std::ofstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

void save_to_file_bin(const std::string& absolute_path, const deploy_settings& obj)
{
    std::ofstream stream(absolute_path, std::ios::binary);
    if(stream.good())
    {
        ser20::oarchive_binary_t ar(stream);
        try_save(ar, ser20::make_nvp("settings", obj));
    }
}

auto load_from_file(const std::string& absolute_path, deploy_settings& obj) -> bool
{
    std::ifstream stream(absolute_path);
    if(stream.good())
    {
        auto ar = ser20::create_iarchive_associative(stream);
        return try_load(ar, ser20::make_nvp("settings", obj));
    }

    return false;
}

auto load_from_file_bin(const std::string& absolute_path, deploy_settings& obj) -> bool
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
