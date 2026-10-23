#include "android.h"
#include "run_arguments.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace unravel::deployment
{
namespace
{
constexpr const char* TEMPLATE_STAMP_NAME = "template.stamp";
constexpr const char* DEFAULT_COMPANY_PART = "unravel";
constexpr const char* DEFAULT_PRODUCT_PART = "game";
/// Put before an application id part that does not start with a letter.
constexpr const char* LEADING_LETTERS = "app";
constexpr const char* GRADLE_WRAPPER_MAIN = "org.gradle.wrapper.GradleWrapperMain";
constexpr const char* PLAYER_ACTIVITY = "com.unravel.player.UnravelActivity";
/// The start intent's extra that UnravelActivity reads the game's command line options from.
constexpr const char* ARGUMENTS_EXTRA = "arguments";
/// What a permission named without a package gets in front.
constexpr const char* ANDROID_PERMISSION_PREFIX = "android.permission.";
/// The engine's tag: spdlog's Android sink.
constexpr const char* ENGINE_LOG_TAG = "spdlog";
/// The tags of the game's own libraries: the engine, SDL's Java side and its HID support, OpenAL Soft and
/// the Vulkan loader.
constexpr std::array<const char*, 4> GAME_LOG_TAGS = {ENGINE_LOG_TAG, "hidapi", "openal", "vulkan"};
/// The bracketed groups before the text of an engine line: its time, level and logger.
constexpr int ENGINE_LOG_PREFIX_GROUPS = 3;
/// What a serial of a phone on wireless debugging holds: an mDNS service name, or an address with a port.
constexpr const char* WIRELESS_SERVICE_MARKER = "._adb-tls-connect._tcp";
/// The type `adb mdns services` names a phone's connect service with; pairing services have another.
constexpr const char* WIRELESS_SERVICE_TYPE = "_adb-tls-connect._tcp";
/// `adb mdns services` separates a service's name, type and address with tabs; a name can hold spaces.
constexpr char SERVICE_FIELD_SEPARATOR = '\t';
constexpr std::size_t SERVICE_FIELD_COUNT = 3;
/// What adb names an emulator: "emulator-5554".
constexpr const char* EMULATOR_SERIAL_PREFIX = "emulator-";
/// A device's properties as name=value lines, one shell call for all of them. Each value comes with its
/// name, so one the phone does not report cannot shift the others; `settings` sends its errors nowhere.
constexpr const char* DEVICE_PROPERTIES_COMMAND = "echo \"serial=$(getprop ro.serialno)\"; "
                                                  "echo \"model=$(getprop ro.product.model)\"; "
                                                  "echo \"android=$(getprop ro.build.version.release)\"; "
                                                  "echo \"name=$(settings get global device_name 2>/dev/null)\"";
/// What `settings get` prints for a setting that was never set.
constexpr const char* UNSET_SETTING = "null";
constexpr char ADDRESS_PORT_SEPARATOR = ':';
/// adb's suffix for a service name announced again: "adb-<serial>-<id> (2)._adb-tls-connect._tcp".
constexpr const char* REPEATED_SERVICE_MARKER = " (";
/// The states `adb devices` prints after a serial; "no permissions" is two words.
constexpr std::array<const char*, 12> DEVICE_STATES = {"device",
                                                       "unauthorized",
                                                       "offline",
                                                       "authorizing",
                                                       "connecting",
                                                       "bootloader",
                                                       "recovery",
                                                       "rescue",
                                                       "sideload",
                                                       "host",
                                                       "unknown",
                                                       "no"};
constexpr const char* NO_PERMISSIONS_STATE_WORD = "no";
constexpr const char* NO_PERMISSIONS_STATE = "no permissions";
/// SDL's tags all start with it: SDL, SDL/<category>, SDLAudio and the like.
constexpr const char* SDL_TAG_PREFIX = "SDL";
/// The tag of Java's uncaught exceptions.
constexpr const char* JAVA_CRASH_TAG = "AndroidRuntime";
constexpr char FATAL_LEVEL = 'F';

auto read_stamp(const fs::path& file) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> values;
    std::ifstream stream(file);
    std::string line;
    while(std::getline(stream, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if(separator != std::string::npos)
        {
            values[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }
    return values;
}

auto is_present(const fs::path& path) -> bool
{
    fs::error_code ec;
    return fs::exists(path, ec);
}

auto is_letter(char c) -> bool
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

auto is_id_character(char c) -> bool
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// One application id part from free text: lowercase, '_' for everything else, no '_' runs or edges.
auto make_id_part(const std::string& text, const char* fallback) -> std::string
{
    std::string part;
    for(const char c : text)
    {
        const auto code = static_cast<unsigned char>(c);
        const bool is_kept = code < 0x80 && std::isalnum(code) != 0;
        const char mapped = is_kept ? static_cast<char>(std::tolower(code)) : '_';
        if(mapped == '_' && (part.empty() || part.back() == '_'))
        {
            continue;
        }
        part += mapped;
    }
    while(!part.empty() && part.back() == '_')
    {
        part.pop_back();
    }
    if(part.empty())
    {
        return fallback;
    }
    if(!is_letter(part.front()))
    {
        part.insert(0, LEADING_LETTERS);
    }
    return part;
}

auto get_environment(const char* name) -> fs::path
{
    const char* value = std::getenv(name);
    return value != nullptr ? fs::path(value) : fs::path{};
}

/// The children of `parent` whose names start with `prefix`, each joined with `inner`, newest name first.
void add_matching_children(const fs::path& parent, const std::string& prefix, const fs::path& inner, std::vector<fs::path>& out)
{
    fs::error_code ec;
    std::vector<fs::path> found;
    for(const auto& entry : fs::directory_iterator(parent, ec))
    {
        const auto name = entry.path().filename().string();
        if(name.rfind(prefix, 0) == 0)
        {
            found.push_back(entry.path() / inner);
        }
    }
    std::sort(found.rbegin(), found.rend());
    out.insert(out.end(), found.begin(), found.end());
}

auto is_device_state(const std::string& word) -> bool
{
    return std::any_of(DEVICE_STATES.begin(),
                       DEVICE_STATES.end(),
                       [&word](const char* state)
                       {
                           return word == state;
                       });
}

/// Which connection to a phone the device list keeps: USB first, then wireless under its plain name.
auto get_connection_rank(const android_device& device) -> int
{
    if(!device.is_wireless())
    {
        return 0;
    }
    return device.serial.find(REPEATED_SERVICE_MARKER) == std::string::npos ? 1 : 2;
}

auto get_executable_name(const std::string& name) -> std::string
{
#if defined(_WIN32)
    return name + ".exe";
#else
    return name;
#endif
}

} // namespace

auto android_template::get_project_dir() const -> fs::path
{
    return root / "project";
}

auto android_template::get_player_dir() const -> fs::path
{
    return root / "lib" / abi;
}

auto android_template::get_sdl_java_dir() const -> fs::path
{
    return root / "sdl" / "java";
}

auto android_template::get_dotnet_dir() const -> fs::path
{
    return root / "dotnet";
}

auto find_android_template(const fs::path& root, const std::string& engine_version, const std::string& configuration)
    -> android_template_lookup
{
    android_template_lookup lookup;
    const auto stamp_file = root / TEMPLATE_STAMP_NAME;
    if(!is_present(root))
    {
        lookup.error = "This editor was built without the Android player. A source build makes it when the "
                       "Android NDK is installed (Android Studio's SDK Manager).";
        return lookup;
    }
    if(!is_present(stamp_file))
    {
        lookup.error = "The Android player beside this editor is incomplete. Rebuild the editor.";
        return lookup;
    }
    auto values = read_stamp(stamp_file);
    auto& found = lookup.found;
    found.root = root;
    found.engine_version = values["engine_version"];
    found.configuration = values["configuration"];
    found.abi = values["abi"];
    found.dotnet_runtime = values["dotnet_runtime"];
    found.symbols_dir = values["symbols"];
    if(found.engine_version != engine_version)
    {
        lookup.error = "The Android player beside this editor comes from engine version " + found.engine_version +
                       ", and the editor is " + engine_version + ". Rebuild the editor.";
        return lookup;
    }
    if(found.configuration != configuration)
    {
        lookup.error = "The Android player beside this editor is a " + found.configuration + " build, and the editor is " +
                       configuration + ". Rebuild the editor.";
        return lookup;
    }
    const bool is_complete = !found.abi.empty() && !found.dotnet_runtime.empty() &&
                             is_present(found.get_player_dir() / "libmain.so") &&
                             is_present(found.get_project_dir() / "gradle" / "wrapper" / "gradle-wrapper.jar") &&
                             is_present(found.get_sdl_java_dir()) && is_present(found.get_dotnet_dir() / "native");
    if(!is_complete)
    {
        lookup.error = "The Android player beside this editor is incomplete. Rebuild the editor.";
    }
    return lookup;
}

auto make_default_application_id(const std::string& company, const std::string& product) -> std::string
{
    return "com." + make_id_part(company, DEFAULT_COMPANY_PART) + "." + make_id_part(product, DEFAULT_PRODUCT_PART);
}

auto check_application_id(const std::string& id) -> std::string
{
    if(id.empty())
    {
        return "The application id is empty.";
    }
    if(id.find('.') == std::string::npos)
    {
        return "The application id needs two or more parts separated by '.', such as com.company.game.";
    }
    std::stringstream parts(id);
    std::string part;
    while(std::getline(parts, part, '.'))
    {
        const bool is_valid = !part.empty() && is_letter(part.front()) &&
                              std::all_of(part.begin(), part.end(), is_id_character);
        if(!is_valid)
        {
            return "Each part of the application id starts with a letter and holds only letters, digits and '_'.";
        }
    }
    if(id.back() == '.')
    {
        return "Each part of the application id starts with a letter and holds only letters, digits and '_'.";
    }
    return {};
}

auto get_screen_orientation(android_orientation orientation) -> std::string
{
    switch(orientation)
    {
        case android_orientation::portrait:
            return "sensorPortrait";
        case android_orientation::automatic:
            return "fullUser";
        case android_orientation::landscape:
        default:
            return "sensorLandscape";
    }
}

auto get_android_permission_entries() -> const std::vector<android_permission_entry>&
{
    static const std::vector<android_permission_entry> entries = {
        {"internet", &android_permissions::internet, {"android.permission.INTERNET", ""}},
        {"network_state", &android_permissions::network_state, {"android.permission.ACCESS_NETWORK_STATE", ""}},
        {"vibrate", &android_permissions::vibrate, {"android.permission.VIBRATE", ""}},
        {"microphone", &android_permissions::microphone, {"android.permission.RECORD_AUDIO", ""}},
        {"camera", &android_permissions::camera, {"android.permission.CAMERA", ""}},
        // Android 12 and later grant the fine location only beside the coarse one.
        {"location", &android_permissions::location, {"android.permission.ACCESS_COARSE_LOCATION", "android.permission.ACCESS_FINE_LOCATION"}},
        {"notifications", &android_permissions::notifications, {"android.permission.POST_NOTIFICATIONS", ""}},
    };
    return entries;
}

auto get_android_permission_name(const std::string& name) -> std::string
{
    return name.find('.') == std::string::npos ? ANDROID_PERMISSION_PREFIX + name : name;
}

auto check_android_permission_name(const std::string& name) -> std::string
{
    std::stringstream parts(get_android_permission_name(name));
    std::string part;
    bool is_valid = !name.empty() && name.back() != '.';
    while(is_valid && std::getline(parts, part, '.'))
    {
        is_valid = !part.empty() && (is_letter(part.front()) || part.front() == '_') &&
                   std::all_of(part.begin(), part.end(), is_id_character);
    }
    return is_valid ? std::string{}
                    : "The permission '" + name + "' is no valid name. Names are parts separated by '.', each starting " +
                          "with a letter and holding only letters, digits and '_', such as android.permission.WAKE_LOCK.";
}

auto get_android_permission_names(const android_permissions& permissions) -> std::vector<std::string>
{
    std::vector<std::string> names;
    const auto add = [&names](const std::string& name)
    {
        if(!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
        {
            names.push_back(name);
        }
    };
    for(const auto& entry : get_android_permission_entries())
    {
        if(permissions.*entry.member)
        {
            add(entry.manifest_names[0]);
            add(entry.manifest_names[1]);
        }
    }
    for(const auto& name : permissions.other)
    {
        add(get_android_permission_name(name));
    }
    return names;
}

auto make_android_permissions_manifest(const std::vector<std::string>& names) -> std::string
{
    std::string text = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n";
    for(const auto& name : names)
    {
        text += "    <uses-permission android:name=\"" + name + "\" />\n";
    }
    return text + "</manifest>\n";
}

auto android_device::is_wireless() const -> bool
{
    return serial.find(WIRELESS_SERVICE_MARKER) != std::string::npos || serial.find(ADDRESS_PORT_SEPARATOR) != std::string::npos;
}

auto android_device::is_named(const std::string& name) const -> bool
{
    return serial == name || std::find(aliases.begin(), aliases.end(), name) != aliases.end();
}

auto android_device::get_name() const -> std::string
{
    if(!device_name.empty())
    {
        return device_name;
    }
    return model.empty() ? serial : model;
}

auto android_device::get_details() const -> std::string
{
    std::vector<std::string> parts;
    if(!model.empty() && model != get_name())
    {
        parts.push_back(model);
    }
    if(!android_version.empty())
    {
        parts.push_back("Android " + android_version);
    }
    if(serial.rfind(EMULATOR_SERIAL_PREFIX, 0) == 0)
    {
        parts.push_back("Emulator");
    }
    else
    {
        parts.push_back(is_wireless() ? "Wi-Fi" : "USB");
    }
    if(!is_ready())
    {
        parts.push_back(state);
    }
    std::string details;
    for(const auto& part : parts)
    {
        details += details.empty() ? part : ", " + part;
    }
    return details;
}

auto android_device::get_label() const -> std::string
{
    return get_name() + " - " + get_details();
}

auto merge_android_devices(std::vector<android_device> devices) -> std::vector<android_device>
{
    std::vector<android_device> merged;
    for(auto& device : devices)
    {
        const auto same = device.hardware_serial.empty()
                              ? merged.end()
                              : std::find_if(merged.begin(),
                                             merged.end(),
                                             [&device](const android_device& kept)
                                             {
                                                 return kept.hardware_serial == device.hardware_serial;
                                             });
        if(same == merged.end())
        {
            merged.push_back(std::move(device));
        }
        else if(get_connection_rank(device) < get_connection_rank(*same))
        {
            device.aliases = std::move(same->aliases);
            device.aliases.push_back(same->serial);
            *same = std::move(device);
        }
        else
        {
            same->aliases.push_back(device.serial);
        }
    }
    return merged;
}

auto parse_android_devices(const std::string& adb_output) -> std::vector<android_device>
{
    std::vector<android_device> devices;
    std::stringstream lines(adb_output);
    std::string line;
    while(std::getline(lines, line))
    {
        std::stringstream words(line);
        std::vector<std::string> tokens;
        for(std::string word; words >> word;)
        {
            tokens.push_back(word);
        }
        // The serial can hold spaces, so the state word ends it. The header and the server's start
        // messages ("* daemon started successfully") have none.
        const auto state = tokens.empty() ? tokens.end() : std::find_if(tokens.begin() + 1, tokens.end(), is_device_state);
        if(state == tokens.end())
        {
            continue;
        }
        android_device device;
        for(auto it = tokens.begin(); it != state; ++it)
        {
            device.serial += device.serial.empty() ? *it : " " + *it;
        }
        device.state = *state == NO_PERMISSIONS_STATE_WORD ? NO_PERMISSIONS_STATE : *state;
        for(auto it = state + 1; it != tokens.end(); ++it)
        {
            if(it->rfind("model:", 0) == 0)
            {
                device.model = it->substr(6);
                std::replace(device.model.begin(), device.model.end(), '_', ' ');
            }
        }
        devices.push_back(std::move(device));
    }
    return devices;
}

auto parse_wireless_debugging_services(const std::string& adb_output) -> std::vector<wireless_debugging_service>
{
    std::vector<wireless_debugging_service> services;
    std::stringstream lines(adb_output);
    std::string line;
    while(std::getline(lines, line))
    {
        while(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        std::vector<std::string> fields;
        std::stringstream columns(line);
        for(std::string field; std::getline(columns, field, SERVICE_FIELD_SEPARATOR);)
        {
            fields.push_back(field);
        }
        if(fields.size() == SERVICE_FIELD_COUNT && fields[1] == WIRELESS_SERVICE_TYPE && !fields[2].empty())
        {
            services.push_back({fields[0], fields[2]});
        }
    }
    return services;
}

auto get_android_device_properties_command() -> std::string
{
    return DEVICE_PROPERTIES_COMMAND;
}

void read_android_device_properties(const std::string& shell_output, android_device& device)
{
    std::stringstream lines(shell_output);
    std::string line;
    while(std::getline(lines, line))
    {
        while(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const auto separator = line.find('=');
        if(separator == std::string::npos || separator + 1 == line.size())
        {
            continue;
        }
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if(key == "serial")
        {
            device.hardware_serial = value;
        }
        else if(key == "model")
        {
            device.model = value;
        }
        else if(key == "android")
        {
            device.android_version = value;
        }
        else if(key == "name" && value != UNSET_SETTING)
        {
            device.device_name = value;
        }
    }
}

auto parse_logcat_line(const std::string& text, logcat_line& line) -> bool
{
    // "10-18 20:44:29.426 25848 25903 I spdlog  : message"
    std::stringstream words(text);
    std::string date;
    std::string time;
    std::string thread;
    std::string level;
    if(!(words >> date >> time >> line.pid >> thread >> level) || level.size() != 1 || date.size() != 5)
    {
        return false;
    }
    line.level = level.front();
    const auto rest_begin = static_cast<std::size_t>(words.tellg());
    const auto separator = text.find(':', rest_begin);
    if(separator == std::string::npos)
    {
        return false;
    }
    line.tag = text.substr(rest_begin, separator - rest_begin);
    const auto tag_begin = line.tag.find_first_not_of(' ');
    const auto tag_end = line.tag.find_last_not_of(' ');
    line.tag = tag_begin == std::string::npos ? std::string() : line.tag.substr(tag_begin, tag_end - tag_begin + 1);
    const auto message_begin = separator + 1 < text.size() && text[separator + 1] == ' ' ? separator + 2 : separator + 1;
    line.message = message_begin < text.size() ? text.substr(message_begin) : std::string();
    return true;
}

auto is_game_log_line(const logcat_line& line) -> bool
{
    if(line.level == FATAL_LEVEL || line.tag == JAVA_CRASH_TAG || line.tag.rfind(SDL_TAG_PREFIX, 0) == 0)
    {
        return true;
    }
    return std::any_of(GAME_LOG_TAGS.begin(),
                       GAME_LOG_TAGS.end(),
                       [&line](const char* tag)
                       {
                           return line.tag == tag;
                       });
}

auto get_device_log_message(const logcat_line& line) -> std::string
{
    const auto& message = line.message;
    const bool has_time = message.size() > 1 && message[0] == '[' && std::isdigit(static_cast<unsigned char>(message[1])) != 0;
    if(line.tag != ENGINE_LOG_TAG || !has_time)
    {
        return message;
    }
    std::size_t begin = 0;
    for(int group = 0; group < ENGINE_LOG_PREFIX_GROUPS && begin < message.size() && message[begin] == '['; ++group)
    {
        const auto end = message.find("] ", begin);
        if(end == std::string::npos)
        {
            break;
        }
        begin = end + 2;
    }
    return message.substr(begin);
}

auto get_android_toolchain_search() -> android_toolchain_search
{
    android_toolchain_search search;
    for(const char* name : {"ANDROID_HOME", "ANDROID_SDK_ROOT"})
    {
        const auto value = get_environment(name);
        if(!value.empty())
        {
            search.sdk_candidates.push_back(value);
        }
    }
    const auto java_home = get_environment("JAVA_HOME");
    if(!java_home.empty())
    {
        search.jdk_candidates.push_back(java_home);
    }
#if defined(_WIN32)
    const auto local_app_data = get_environment("LOCALAPPDATA");
    if(!local_app_data.empty())
    {
        search.sdk_candidates.push_back(local_app_data / "Android" / "Sdk");
    }
    auto program_files = get_environment("ProgramFiles");
    if(program_files.empty())
    {
        program_files = "C:/Program Files";
    }
    add_matching_children(program_files / "Android", "Android Studio", "jbr", search.jdk_candidates);
    if(!local_app_data.empty())
    {
        add_matching_children(local_app_data / "Programs", "Android Studio", "jbr", search.jdk_candidates);
    }
#elif defined(__APPLE__)
    const auto home = get_environment("HOME");
    search.sdk_candidates.push_back(home / "Library" / "Android" / "sdk");
    add_matching_children("/Applications", "Android Studio", fs::path("Contents") / "jbr" / "Contents" / "Home",
                          search.jdk_candidates);
#else
    const auto home = get_environment("HOME");
    search.sdk_candidates.push_back(home / "Android" / "Sdk");
    add_matching_children("/opt", "android-studio", "jbr", search.jdk_candidates);
    add_matching_children(home, "android-studio", "jbr", search.jdk_candidates);
    // Snap revisions differ: newer ones unpack Android Studio at the snap root (/snap/android-studio/current),
    // older ones one folder down (/snap/android-studio/current/android-studio).
    add_matching_children("/snap", "android-studio", fs::path("current") / "jbr", search.jdk_candidates);
    add_matching_children("/snap", "android-studio", fs::path("current") / "android-studio" / "jbr", search.jdk_candidates);
#endif
    return search;
}

auto find_android_toolchain(const android_toolchain_search& search) -> android_toolchain
{
    android_toolchain tools;
    const auto adb_name = get_executable_name("adb");
    for(const auto& sdk : search.sdk_candidates)
    {
        if(is_present(sdk / "platform-tools" / adb_name))
        {
            tools.sdk = sdk;
            tools.adb = sdk / "platform-tools" / adb_name;
            break;
        }
    }
    const auto java_name = get_executable_name("java");
    for(const auto& jdk : search.jdk_candidates)
    {
        if(is_present(jdk / "bin" / java_name))
        {
            tools.java = jdk / "bin" / java_name;
            break;
        }
    }
    if(tools.sdk.empty())
    {
        tools.error = "No Android SDK was found. Install Android Studio, or set ANDROID_HOME to the SDK folder.";
    }
    else if(tools.java.empty())
    {
        tools.error = "No Java was found. Install Android Studio, which bundles one, or set JAVA_HOME.";
    }
    return tools;
}

auto make_gradle_arguments(const android_toolchain& tools, const android_package_params& params)
    -> std::vector<std::string>
{
    const auto& player = params.player;
    std::vector<std::string> arguments{
        tools.java.string(),
        "-classpath",
        (params.project_dir / "gradle" / "wrapper" / "gradle-wrapper.jar").string(),
        GRADLE_WRAPPER_MAIN,
        "--project-dir",
        params.project_dir.string(),
        params.is_release ? "assembleRelease" : "assembleDebug",
        "-Punravel.player=" + player.get_player_dir().generic_string(),
        "-Punravel.sdlJava=" + player.get_sdl_java_dir().generic_string(),
        "-Punravel.dotnet=" + player.get_dotnet_dir().generic_string(),
        "-Punravel.dotnetVersion=" + player.dotnet_runtime,
        "-Punravel.content=" + params.content_dir.generic_string(),
        "-Punravel.applicationId=" + params.application_id,
        "-Punravel.appName=" + params.name,
        "-Punravel.versionCode=" + std::to_string(params.version_code),
        "-Punravel.versionName=" + params.version_name,
        "-Punravel.orientation=" + get_screen_orientation(params.orientation),
        "-Punravel.icon=" + params.icon_dir.generic_string(),
        "-Punravel.permissions=" + params.permissions_manifest.generic_string(),
    };
    if(params.is_release && !params.keystore.empty())
    {
        arguments.push_back("-Punravel.keystore=" + params.keystore.generic_string());
        arguments.push_back("-Punravel.keyAlias=" + params.key_alias);
    }
    return arguments;
}

auto make_gradle_local_properties(const fs::path& sdk) -> std::string
{
    // '\' and ':' are special in a properties file; escaped the way Android Studio writes the file.
    const std::string path = sdk.generic_string();
    std::string escaped;
    for(const char c : path)
    {
        if(c == ':' || c == '\\')
        {
            escaped += '\\';
        }
        escaped += c;
    }
    return "sdk.dir=" + escaped + "\n";
}

auto get_built_package(const fs::path& project_dir, bool is_release) -> fs::path
{
    const std::string type = is_release ? "release" : "debug";
    const std::string suffix = is_release ? "release-unsigned" : "debug";
    const auto outputs = project_dir / "app" / "build" / "outputs" / "apk" / type;
    // Signed release builds drop "-unsigned".
    if(is_release && is_present(outputs / "app-release.apk"))
    {
        return outputs / "app-release.apk";
    }
    return outputs / ("app-" + suffix + ".apk");
}

auto make_start_arguments(const android_toolchain& tools,
                          const std::string& device,
                          const std::string& application_id,
                          const std::vector<std::string>& run_arguments) -> std::vector<std::string>
{
    std::vector<std::string> arguments{
        tools.adb.string(), "-s", device, "shell", "am", "start", "-W", "-n", application_id + "/" + PLAYER_ACTIVITY};
    if(run_arguments.empty())
    {
        return arguments;
    }
    std::string command_line;
    for(const auto& argument : run_arguments)
    {
        command_line += command_line.empty() ? argument : " " + argument;
    }
    // adb joins its arguments into one command for the device's shell, which would split the options.
    arguments.insert(arguments.end(), {"--es", ARGUMENTS_EXTRA, quote_for_shell(command_line)});
    return arguments;
}

} // namespace unravel::deployment
