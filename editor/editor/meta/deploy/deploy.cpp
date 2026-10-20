#include "deploy.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/array.hpp>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{
REFLECT(deploy_settings)
{
    entt::meta_factory<deploy_platform>{}
        .type("deploy_platform"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_platform"},
            entt::attribute{"pretty_name", "Platform"},
        })
        .data<deploy_platform::desktop>("desktop"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "desktop"},
            entt::attribute{"pretty_name", "Desktop"},
        })
        .data<deploy_platform::android>("android"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android"},
            entt::attribute{"pretty_name", "Android"},
        });

    entt::meta_factory<android_orientation>{}
        .type("android_orientation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_orientation"},
            entt::attribute{"pretty_name", "Orientation"},
        })
        .data<android_orientation::landscape>("landscape"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "landscape"},
            entt::attribute{"pretty_name", "Landscape"},
        })
        .data<android_orientation::portrait>("portrait"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "portrait"},
            entt::attribute{"pretty_name", "Portrait"},
        })
        .data<android_orientation::automatic>("automatic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "automatic"},
            entt::attribute{"pretty_name", "Automatic"},
        });

    entt::meta_factory<deploy_settings>{}
        .type("deploy_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Deploy Options"},
        })
        .data<&deploy_settings::deploy_target>("deploy_target"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_target"},
            entt::attribute{"pretty_name", "Platform"},
            entt::attribute{"tooltip",
                            "What the game is built for.\n\n"
                            "Desktop: this computer's player executable beside the data.\n"
                            "Android: an installable package (.apk) built from the editor's\n"
                            "Android player."},
        })
        .data<&deploy_settings::deploy_location>("deploy_location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_location"},
            entt::attribute{"pretty_name", "Deploy Location"},
            entt::attribute{"tooltip",
                            "The folder the game is built into. Each platform builds into a folder\n"
                            "of its own in it: desktop for this computer, android for the package.\n\n"
                            "A relative path is in the project's folder, such as .deploy, which the\n"
                            "deploy creates. A folder picked inside the project is kept relative, so\n"
                            "the setting moves with the project."},
            entt::attribute{"relative_to", std::string(PROJECT_ROOT_KEY)},
            entt::attribute{"hint", "A folder, or a path in the project such as .deploy"},
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
                            "The game runs without them. With them, script exceptions\n"
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
            entt::attribute{"tooltip",
                            "Start the game once the deploy is done.\n"
                            "Android: install it on the chosen device and start it there."},
        })
        .data<&deploy_settings::deploy_run_arguments>("deploy_run_arguments"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "deploy_run_arguments"},
            entt::attribute{"pretty_name", "Run Arguments"},
            entt::attribute{"tooltip",
                            "Command line options the game starts with after the deploy, separated by spaces.\n"
                            "--gfx-stats shows the graphics statistics over the game.\n"
                            "--novsync turns off vsync.\n\n"
                            "In Android debug packages a four-finger tap switches the\n"
                            "graphics statistics on and off."},
        })
        .data<&deploy_settings::android_device>("android_device"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_device"},
            entt::attribute{"pretty_name", "Device"},
            entt::attribute{"tooltip",
                            "The device Deploy and Run installs the game on and starts it.\n\n"
                            "Connect a phone over USB with USB debugging on, or pair it for\n"
                            "wireless debugging, then refresh the list."},
        })
        .data<&deploy_settings::android_application_id>("android_application_id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_application_id"},
            entt::attribute{"pretty_name", "Application Id"},
            entt::attribute{"tooltip",
                            "The game's id on Android devices and in stores, such as com.company.game.\n"
                            "Empty: com.<company>.<product> from the project settings.\n\n"
                            "Changing it later installs a separate app."},
        })
        .data<&deploy_settings::android_version_code>("android_version_code"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_version_code"},
            entt::attribute{"pretty_name", "Version Code"},
            entt::attribute{"tooltip",
                            "Android's version number of the game.\n"
                            "Stores need a larger one for every release they get."},
            entt::attribute{"min", 1},
        })
        .data<&deploy_settings::android_screen_orientation>("android_screen_orientation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_screen_orientation"},
            entt::attribute{"pretty_name", "Orientation"},
            entt::attribute{"tooltip",
                            "How the game turns with the phone.\n\n"
                            "Landscape and Portrait allow both directions of that orientation.\n"
                            "Automatic follows the phone."},
        })
        .data<&deploy_settings::android_icon>("android_icon"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_icon"},
            entt::attribute{"pretty_name", "Icon"},
            entt::attribute{"tooltip",
                            "The image the game's icon on the phone is made from. Square images\n"
                            "fit best, at 512 x 512 pixels or more.\n\n"
                            "The image fills the part of the icon every launcher shows, and the\n"
                            "launcher cuts it to its shape: a circle, a rounded square or another.\n\n"
                            "None: the engine's logo."},
        })
        .data<&deploy_settings::android_icon_background>("android_icon_background"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_icon_background"},
            entt::attribute{"pretty_name", "Icon Background"},
            entt::attribute{"tooltip",
                            "The color behind the icon's image. It shows through the image's\n"
                            "transparent parts and at the icon's edges, which launchers reveal\n"
                            "when they move or grow the icon."},
            entt::attribute{"opaque", true},
        })
        .data<&deploy_settings::android_release>("android_release"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_release"},
            entt::attribute{"pretty_name", "Release Build"},
            entt::attribute{"tooltip",
                            "Build a release package signed with the keystore below, for stores.\n\n"
                            "Off: a debug package, signed with the Android SDK's debug keystore.\n"
                            "It installs on your devices, and stores refuse it."},
        })
        .data<&deploy_settings::android_keystore>("android_keystore"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_keystore"},
            entt::attribute{"pretty_name", "Keystore"},
            entt::attribute{"tooltip",
                            "The keystore that signs release packages.\n"
                            "Its passwords are never saved. The deploy reads them from the\n"
                            "UNRAVEL_KEYSTORE_PASSWORD and UNRAVEL_KEY_PASSWORD environment variables."},
        })
        .data<&deploy_settings::android_key_alias>("android_key_alias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_key_alias"},
            entt::attribute{"pretty_name", "Key Alias"},
            entt::attribute{"tooltip", "The key in the keystore that signs release packages."},
        });
}

SAVE(deploy_settings)
{
    try_save(ar, ser20::make_nvp("deploy_target", obj.deploy_target));
    try_save(ar, ser20::make_nvp("deploy_location", fs::to_utf8_string(obj.deploy_location)));
    try_save(ar, ser20::make_nvp("deploy_dependencies", obj.deploy_dependencies));
    try_save(ar, ser20::make_nvp("deploy_and_run", obj.deploy_and_run));
    try_save(ar, ser20::make_nvp("deploy_run_arguments", obj.deploy_run_arguments));
    try_save(ar, ser20::make_nvp("deploy_cooked_assets", obj.deploy_cooked_assets));
    try_save(ar, ser20::make_nvp("deploy_debug_symbols", obj.deploy_debug_symbols));
    try_save(ar, ser20::make_nvp("deploy_packed_assets", obj.deploy_packed_assets));
    try_save(ar, ser20::make_nvp("deploy_patch_release", obj.deploy_patch_release));
    try_save(ar, ser20::make_nvp("deploy_only_used_assets", obj.deploy_only_used_assets));
    try_save(ar, ser20::make_nvp("deploy_scenes", obj.deploy_scenes));
    try_save(ar, ser20::make_nvp("deploy_included", obj.deploy_included));
    try_save(ar, ser20::make_nvp("deploy_excluded", obj.deploy_excluded));
    try_save(ar, ser20::make_nvp("android_application_id", obj.android_application_id));
    try_save(ar, ser20::make_nvp("android_version_code", obj.android_version_code));
    try_save(ar, ser20::make_nvp("android_screen_orientation", obj.android_screen_orientation));
    try_save(ar, ser20::make_nvp("android_icon", obj.android_icon));
    try_save(ar, ser20::make_nvp("android_icon_background", obj.android_icon_background));
    try_save(ar, ser20::make_nvp("android_release", obj.android_release));
    try_save(ar, ser20::make_nvp("android_keystore", fs::to_utf8_string(obj.android_keystore)));
    try_save(ar, ser20::make_nvp("android_key_alias", obj.android_key_alias));
}
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_binary_t);

LOAD(deploy_settings)
{
    try_load(ar, ser20::make_nvp("deploy_target", obj.deploy_target));
    std::string deploy_location;
    if(try_load(ar, ser20::make_nvp("deploy_location", deploy_location)))
    {
        obj.deploy_location = fs::from_utf8_string(deploy_location);
    }

    try_load(ar, ser20::make_nvp("deploy_dependencies", obj.deploy_dependencies));
    try_load(ar, ser20::make_nvp("deploy_and_run", obj.deploy_and_run));
    try_load(ar, ser20::make_nvp("deploy_run_arguments", obj.deploy_run_arguments));
    try_load(ar, ser20::make_nvp("deploy_cooked_assets", obj.deploy_cooked_assets));
    try_load(ar, ser20::make_nvp("deploy_debug_symbols", obj.deploy_debug_symbols));
    try_load(ar, ser20::make_nvp("deploy_packed_assets", obj.deploy_packed_assets));
    try_load(ar, ser20::make_nvp("deploy_patch_release", obj.deploy_patch_release));
    try_load(ar, ser20::make_nvp("deploy_only_used_assets", obj.deploy_only_used_assets));
    try_load(ar, ser20::make_nvp("deploy_scenes", obj.deploy_scenes));
    try_load(ar, ser20::make_nvp("deploy_included", obj.deploy_included));
    try_load(ar, ser20::make_nvp("deploy_excluded", obj.deploy_excluded));
    try_load(ar, ser20::make_nvp("android_application_id", obj.android_application_id));
    try_load(ar, ser20::make_nvp("android_version_code", obj.android_version_code));
    try_load(ar, ser20::make_nvp("android_screen_orientation", obj.android_screen_orientation));
    try_load(ar, ser20::make_nvp("android_icon", obj.android_icon));
    try_load(ar, ser20::make_nvp("android_icon_background", obj.android_icon_background));
    try_load(ar, ser20::make_nvp("android_release", obj.android_release));
    std::string android_keystore;
    if(try_load(ar, ser20::make_nvp("android_keystore", android_keystore)))
    {
        obj.android_keystore = fs::from_utf8_string(android_keystore);
    }
    try_load(ar, ser20::make_nvp("android_key_alias", obj.android_key_alias));
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
