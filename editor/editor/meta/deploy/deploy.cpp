#include "deploy.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/array.hpp>

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
            entt::attribute{"tooltip", "Choose the deploy location."},
        })
        .data<&deploy_settings::deploy_dependencies>("deploy_dependencies"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_dependencies"},
            entt::attribute{"pretty_name", "Deploy Dependencies"},
            entt::attribute{"tooltip", "This takes some time and if already done should't be necessary."},
        })
        .data<&deploy_settings::deploy_and_run>("deploy_and_run"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_and_run"},
            entt::attribute{"pretty_name", "Deploy & Run"},
            entt::attribute{"tooltip", "Run the application after the deploy."},
        })
        .data<&deploy_settings::bake_prefab_nesting>("bake_prefab_nesting"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bake_prefab_nesting"},
            entt::attribute{"pretty_name", "Bake Prefab Nesting"},
            entt::attribute{"tooltip",
                            "Resolve nested prefab instances once, at deploy time, so the game\n"
                            "does not refresh them on every load.\n\n"
                            "The deploy rewrites this marker on every run, so what it says is\n"
                            "always what this build produced.\n\n"
                            "Off: the marker is cleared and the game refreshes nested instances\n"
                            "as it loads them - slower, but it cannot serve stale content."},
        });
}

SAVE(deploy_settings)
{
    try_save(ar, ser20::make_nvp("deploy_location", obj.deploy_location.generic_string()));
    try_save(ar, ser20::make_nvp("deploy_dependencies", obj.deploy_dependencies));
    try_save(ar, ser20::make_nvp("deploy_and_run", obj.deploy_and_run));
    try_save(ar, ser20::make_nvp("bake_prefab_nesting", obj.bake_prefab_nesting));
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
    try_load(ar, ser20::make_nvp("bake_prefab_nesting", obj.bake_prefab_nesting));
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
