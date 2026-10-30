#include "deploy_settings.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <serialization/types/array.hpp>
#include <serialization/types/string.hpp>
#include <serialization/types/vector.hpp>

#include <fstream>

namespace unravel
{
REFLECT(android_deploy_settings)
{
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

    entt::meta_factory<android_texture_compression>{}
        .type("android_texture_compression"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_texture_compression"},
            entt::attribute{"pretty_name", "Texture Format"},
        })
        .data<android_texture_compression::astc>("astc"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "astc"},
            entt::attribute{"pretty_name", "ASTC"},
        })
        .data<android_texture_compression::etc2>("etc2"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "etc2"},
            entt::attribute{"pretty_name", "ETC2"},
        })
        .data<android_texture_compression::bc>("bc"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bc"},
            entt::attribute{"pretty_name", "BC (Desktop)"},
        });

    entt::meta_factory<android_permissions>{}
        .type("android_permissions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_permissions"},
            entt::attribute{"pretty_name", "Permissions"},
        })
        .data<&android_permissions::internet>("internet"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "internet"},
            entt::attribute{"pretty_name", "Internet"},
            entt::attribute{"tooltip",
                            "INTERNET: networking, such as HttpClient and sockets.\n"
                            "Granted when the game installs, without a prompt."},
        })
        .data<&android_permissions::network_state>("network_state"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "network_state"},
            entt::attribute{"pretty_name", "Network State"},
            entt::attribute{"tooltip",
                            "ACCESS_NETWORK_STATE: whether the phone is online, and over which network.\n"
                            "Granted when the game installs, without a prompt."},
        })
        .data<&android_permissions::vibrate>("vibrate"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "vibrate"},
            entt::attribute{"pretty_name", "Vibrate"},
            entt::attribute{"tooltip",
                            "VIBRATE: the phone's vibration, also for gamepad rumble.\n"
                            "Granted when the game installs, without a prompt."},
        })
        .data<&android_permissions::microphone>("microphone"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "microphone"},
            entt::attribute{"pretty_name", "Microphone"},
            entt::attribute{"tooltip",
                            "RECORD_AUDIO: the microphone.\n"
                            "The player is asked when the game first uses it."},
        })
        .data<&android_permissions::camera>("camera"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "camera"},
            entt::attribute{"pretty_name", "Camera"},
            entt::attribute{"tooltip",
                            "CAMERA: the phone's cameras.\n"
                            "The player is asked when the game first uses them."},
        })
        .data<&android_permissions::location>("location"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "location"},
            entt::attribute{"pretty_name", "Location"},
            entt::attribute{"tooltip",
                            "ACCESS_COARSE_LOCATION and ACCESS_FINE_LOCATION: where the phone is.\n"
                            "The player is asked when the game first uses it, and may allow only\n"
                            "the approximate location."},
        })
        .data<&android_permissions::notifications>("notifications"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "notifications"},
            entt::attribute{"pretty_name", "Notifications"},
            entt::attribute{"tooltip",
                            "POST_NOTIFICATIONS: notifications from the game.\n"
                            "From Android 13 the player is asked when the game first sends one."},
        })
        .data<&android_permissions::other>("other"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "other"},
            entt::attribute{"pretty_name", "Other Permissions"},
            entt::attribute{"tooltip",
                            "Further permissions by name, such as android.permission.WAKE_LOCK or a\n"
                            "vendor's. A name without a '.' gets android.permission. in front,\n"
                            "so WAKE_LOCK works too."},
        });

    entt::meta_factory<android_deploy_settings>{}
        .type("android_deploy_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "android_deploy_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Android"},
        })
        .data<&android_deploy_settings::application_id>("application_id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "application_id"},
            entt::attribute{"pretty_name", "Application Id"},
            entt::attribute{"tooltip",
                            "The game's id on Android devices and in stores, such as com.company.game.\n"
                            "Empty: com.<company>.<product>, made from the product settings and\n"
                            "following them. Typing an id overrides it.\n\n"
                            "Changing it after a release installs a separate app: devices and stores\n"
                            "know the game by it, so it never carries the version."},
        })
        .data<&android_deploy_settings::permissions>("permissions"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "permissions"},
            entt::attribute{"pretty_name", "Permissions"},
            entt::attribute{"tooltip",
                            "What the game asks for in its manifest. Internet, Network State and\n"
                            "Vibrate are granted at install; the others are asked of the player\n"
                            "when the game first uses them."},
        })
        .data<&android_deploy_settings::version_code>("version_code"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "version_code"},
            entt::attribute{"pretty_name", "Version Code"},
            entt::attribute{"tooltip",
                            "Android's version number of the game.\n"
                            "Stores need a larger one for every release they get."},
            entt::attribute{"min", 1},
        })
        .data<&android_deploy_settings::screen_orientation>("screen_orientation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_orientation"},
            entt::attribute{"pretty_name", "Orientation"},
            entt::attribute{"tooltip",
                            "How the game turns with the phone.\n\n"
                            "Landscape and Portrait allow both directions of that orientation.\n"
                            "Automatic follows the phone."},
        })
        .data<&android_deploy_settings::icon>("icon"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "icon"},
            entt::attribute{"pretty_name", "Icon"},
            entt::attribute{"tooltip",
                            "The image the game's icon on the phone is made from. Square images\n"
                            "fit best, at 512 x 512 pixels or more.\n\n"
                            "The image fills the part of the icon every launcher shows, and the\n"
                            "launcher cuts it to its shape: a circle, a rounded square or another.\n\n"
                            "None: the engine's logo."},
        })
        .data<&android_deploy_settings::icon_background>("icon_background"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "icon_background"},
            entt::attribute{"pretty_name", "Icon Background"},
            entt::attribute{"tooltip",
                            "The color behind the icon's image. It shows through the image's\n"
                            "transparent parts and at the icon's edges, which launchers reveal\n"
                            "when they move or grow the icon."},
            entt::attribute{"opaque", true},
        })
        .data<&android_deploy_settings::release>("release"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "release"},
            entt::attribute{"pretty_name", "Release Build"},
            entt::attribute{"tooltip",
                            "Build a release package signed with the keystore below, for stores.\n\n"
                            "Off: a debug package, signed with the Android SDK's debug keystore.\n"
                            "It installs on your devices, and stores refuse it."},
        })
        .data<&android_deploy_settings::keystore>("keystore"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "keystore"},
            entt::attribute{"pretty_name", "Keystore"},
            entt::attribute{"tooltip",
                            "The keystore that signs release packages.\n"
                            "Its passwords are never saved. The deploy reads them from the\n"
                            "UNRAVEL_KEYSTORE_PASSWORD and UNRAVEL_KEY_PASSWORD environment variables."},
        })
        .data<&android_deploy_settings::key_alias>("key_alias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "key_alias"},
            entt::attribute{"pretty_name", "Key Alias"},
            entt::attribute{"tooltip", "The key in the keystore that signs release packages."},
        })
        .data<&android_deploy_settings::texture_compression>("texture_compression"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "texture_compression"},
            entt::attribute{"pretty_name", "Texture Format"},
            entt::attribute{"tooltip",
                            "The formats the game's textures are compressed to on the phone.\n\n"
                            "ASTC: the better image for its size. A phone without it decodes the\n"
                            "textures when they load, into four times the memory.\n"
                            "ETC2: read by every OpenGL ES 3 and Vulkan phone, at a lower quality.\n"
                            "BC (Desktop): the formats the editor compiles for this computer, shipped\n"
                            "as they are. Only some phones read them; the others decode the textures\n"
                            "when they load, into four to eight times the memory.\n\n"
                            "The deploy compresses the textures for this profile and keeps them\n"
                            "for its next deploys."},
        });
}

SAVE(android_permissions)
{
    try_save(ar, ser20::make_nvp("internet", obj.internet));
    try_save(ar, ser20::make_nvp("network_state", obj.network_state));
    try_save(ar, ser20::make_nvp("vibrate", obj.vibrate));
    try_save(ar, ser20::make_nvp("microphone", obj.microphone));
    try_save(ar, ser20::make_nvp("camera", obj.camera));
    try_save(ar, ser20::make_nvp("location", obj.location));
    try_save(ar, ser20::make_nvp("notifications", obj.notifications));
    try_save(ar, ser20::make_nvp("other", obj.other));
}
SAVE_INSTANTIATE(android_permissions, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(android_permissions, ser20::oarchive_binary_t);

LOAD(android_permissions)
{
    try_load(ar, ser20::make_nvp("internet", obj.internet));
    try_load(ar, ser20::make_nvp("network_state", obj.network_state));
    try_load(ar, ser20::make_nvp("vibrate", obj.vibrate));
    try_load(ar, ser20::make_nvp("microphone", obj.microphone));
    try_load(ar, ser20::make_nvp("camera", obj.camera));
    try_load(ar, ser20::make_nvp("location", obj.location));
    try_load(ar, ser20::make_nvp("notifications", obj.notifications));
    try_load(ar, ser20::make_nvp("other", obj.other));
}
LOAD_INSTANTIATE(android_permissions, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(android_permissions, ser20::iarchive_binary_t);

SAVE(android_deploy_settings)
{
    try_save(ar, ser20::make_nvp("application_id", obj.application_id));
    try_save(ar, ser20::make_nvp("permissions", obj.permissions));
    try_save(ar, ser20::make_nvp("version_code", obj.version_code));
    try_save(ar, ser20::make_nvp("screen_orientation", obj.screen_orientation));
    try_save(ar, ser20::make_nvp("icon", obj.icon));
    try_save(ar, ser20::make_nvp("icon_background", obj.icon_background));
    try_save(ar, ser20::make_nvp("release", obj.release));
    try_save(ar, ser20::make_nvp("keystore", fs::to_utf8_string(obj.keystore)));
    try_save(ar, ser20::make_nvp("key_alias", obj.key_alias));
    try_save(ar, ser20::make_nvp("texture_compression", obj.texture_compression));
}
SAVE_INSTANTIATE(android_deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(android_deploy_settings, ser20::oarchive_binary_t);

LOAD(android_deploy_settings)
{
    try_load(ar, ser20::make_nvp("application_id", obj.application_id));
    try_load(ar, ser20::make_nvp("permissions", obj.permissions));
    try_load(ar, ser20::make_nvp("version_code", obj.version_code));
    try_load(ar, ser20::make_nvp("screen_orientation", obj.screen_orientation));
    try_load(ar, ser20::make_nvp("icon", obj.icon));
    try_load(ar, ser20::make_nvp("icon_background", obj.icon_background));
    try_load(ar, ser20::make_nvp("release", obj.release));
    std::string keystore;
    if(try_load(ar, ser20::make_nvp("keystore", keystore)))
    {
        obj.keystore = fs::from_utf8_string(keystore);
    }
    try_load(ar, ser20::make_nvp("key_alias", obj.key_alias));
    try_load(ar, ser20::make_nvp("texture_compression", obj.texture_compression));
}
LOAD_INSTANTIATE(android_deploy_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(android_deploy_settings, ser20::iarchive_binary_t);

REFLECT(steam_deploy_settings)
{
    entt::meta_factory<steam_deploy_settings>{}
        .type("steam_deploy_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "steam_deploy_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Steam"},
        })
        .data<&steam_deploy_settings::app_id>("app_id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "app_id"},
            entt::attribute{"pretty_name", "App ID"},
            entt::attribute{"tooltip",
                            "The game's app ID in Steamworks.\n"
                            "0: the profile is not uploaded to Steam."},
        })
        .data<&steam_deploy_settings::depot_id>("depot_id"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "depot_id"},
            entt::attribute{"pretty_name", "Depot ID"},
            entt::attribute{"tooltip",
                            "The depot the build goes to: the app's depot for the operating system\n"
                            "this profile builds for (the one the editor runs on)."},
        })
        .data<&steam_deploy_settings::account>("account"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "account"},
            entt::attribute{"pretty_name", "Account"},
            entt::attribute{"tooltip",
                            "The Steam account that uploads, best one with build rights only.\n\n"
                            "Its password is never saved. Log it in once in a terminal with\n"
                            "steamcmd +login <account>, with the password and the Steam Guard\n"
                            "code. Uploads then use the login steamcmd keeps."},
        })
        .data<&steam_deploy_settings::branch>("branch"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "branch"},
            entt::attribute{"pretty_name", "Set Live On"},
            entt::attribute{"tooltip",
                            "The branch the build is set live on once uploaded, such as beta. The branch\n"
                            "must exist in Steamworks.\n\n"
                            "Empty: the build is only uploaded, and set live on the Steamworks website.\n"
                            "Steam sets builds live on the default branch only from the website."},
        });
}

SAVE(steam_deploy_settings)
{
    try_save(ar, ser20::make_nvp("app_id", obj.app_id));
    try_save(ar, ser20::make_nvp("depot_id", obj.depot_id));
    try_save(ar, ser20::make_nvp("account", obj.account));
    try_save(ar, ser20::make_nvp("branch", obj.branch));
}
SAVE_INSTANTIATE(steam_deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(steam_deploy_settings, ser20::oarchive_binary_t);

LOAD(steam_deploy_settings)
{
    try_load(ar, ser20::make_nvp("app_id", obj.app_id));
    try_load(ar, ser20::make_nvp("depot_id", obj.depot_id));
    try_load(ar, ser20::make_nvp("account", obj.account));
    try_load(ar, ser20::make_nvp("branch", obj.branch));
}
LOAD_INSTANTIATE(steam_deploy_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(steam_deploy_settings, ser20::iarchive_binary_t);

REFLECT(desktop_deploy_settings)
{
    entt::meta_factory<desktop_deploy_settings>{}
        .type("desktop_deploy_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "desktop_deploy_settings"},
            entt::attribute{"category", "EDITOR"},
            entt::attribute{"pretty_name", "Desktop"},
        })
        .data<&desktop_deploy_settings::icon>("icon"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "icon"},
            entt::attribute{"pretty_name", "Icon"},
            entt::attribute{"tooltip",
                            "The image the game's icon is made from: the icon of its executable in\n"
                            "the file manager, the taskbar and the window's title bar. Square images\n"
                            "fit best, at 256 x 256 pixels or more.\n\n"
                            "Windows only. None: the engine's logo."},
        });
}

SAVE(desktop_deploy_settings)
{
    try_save(ar, ser20::make_nvp("icon", obj.icon));
    try_save(ar, ser20::make_nvp("steam", obj.steam));
}
SAVE_INSTANTIATE(desktop_deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(desktop_deploy_settings, ser20::oarchive_binary_t);

LOAD(desktop_deploy_settings)
{
    try_load(ar, ser20::make_nvp("icon", obj.icon));
    try_load(ar, ser20::make_nvp("steam", obj.steam));
}
LOAD_INSTANTIATE(desktop_deploy_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(desktop_deploy_settings, ser20::iarchive_binary_t);

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
            entt::attribute{"tooltip",
                            "The folder the game is built into. Each profile builds into a folder\n"
                            "of its own in it, named after the profile.\n\n"
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
                            "A release of this profile kept with Keep as Release (its name, the\n"
                            "product version it was kept at). Its packs ship unchanged and\n"
                            "content_p1.pak holds every change since, so players who have it\n"
                            "download only the patch.\n\n"
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
                            "--novsync turns off vsync.\n"
                            "--log-here writes the log beside the game, not in the user's data folder.\n\n"
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
        });
}

SAVE(deploy_settings)
{
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
}
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(deploy_settings, ser20::oarchive_binary_t);

LOAD(deploy_settings)
{
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
} // namespace unravel
