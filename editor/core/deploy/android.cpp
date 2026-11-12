#include "android.h"
#include "files.h"
#include "folders.h"
#include "run_arguments.h"

#include <engine/permissions/permission_requests.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <utility>

namespace unravel::deployment
{
namespace
{
constexpr const char* TEMPLATE_STAMP_NAME = "template.stamp";
/// The file of a build folder's owner, and its keys.
constexpr const char* BUILD_OWNER_NAME = "owner.stamp";
constexpr const char* OWNER_PROJECT_KEY = "project";
constexpr const char* OWNER_PROFILE_KEY = "profile";
/// The file every deploy writes into a build folder's Gradle project, also before build folders named their owner.
constexpr const char* GRADLE_LOCAL_PROPERTIES = "project/local.properties";
constexpr const char* DEFAULT_COMPANY_PART = "unravel";
constexpr const char* DEFAULT_PRODUCT_PART = "game";
/// Put before an application id part that does not start with a letter.
constexpr const char* LEADING_LETTERS = "app";
constexpr const char* GRADLE_WRAPPER_MAIN = "org.gradle.wrapper.GradleWrapperMain";
/// The wrapper's property that names the Gradle distribution, how the distribution's file name ends, and where
/// Gradle keeps the distributions it downloaded, under its own folder.
constexpr const char* GRADLE_DISTRIBUTION_KEY = "distributionUrl=";
constexpr const char* GRADLE_DISTRIBUTION_EXTENSION = ".zip";
constexpr const char* GRADLE_DISTRIBUTIONS_FOLDER = "wrapper/dists";
/// A release build leaves no Gradle daemon behind: one would keep the keystore's passwords in its environment.
constexpr const char* GRADLE_NO_DAEMON = "--no-daemon";
/// A debug build's daemon, which makes the next build start fast, ends after this long without one; Gradle's own
/// three hours keep its memory long after the editor closed.
constexpr const char* GRADLE_DAEMON_IDLE_TIMEOUT = "-Dorg.gradle.daemon.idletimeout=600000";
/// How a manifest's root element starts and ends.
constexpr const char* MANIFEST_TAG = "<manifest";
constexpr const char* MANIFEST_END_TAG = "</manifest>";
/// Java 8 and older name their version "1.<major>".
constexpr int OLD_JAVA_FIRST_NUMBER = 1;
/// The file at a JDK's top that describes it, and the line of it that gives the version (JAVA_VERSION="21.0.3").
constexpr const char* JDK_RELEASE_FILE = "release";
constexpr const char* JDK_VERSION_KEY = "JAVA_VERSION=";
/// How a crash report in a device's log starts, and how the line that names its process starts.
constexpr const char* CRASH_REPORT_START = "*** *** ***";
constexpr const char* CRASH_REPORT_PROCESS = "pid: ";
/// The lines of a crash report held until it names its process, which it does within its first ten.
constexpr std::size_t CRASH_REPORT_HELD_MAX = 64;
constexpr const char* PLAYER_ACTIVITY = "com.unravel.player.UnravelActivity";
/// The start intent's extra that UnravelActivity reads the game's command line options from.
constexpr const char* ARGUMENTS_EXTRA = "arguments";
/// What a permission named without a package gets in front.
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
constexpr player_wording ANDROID_PLAYER_WORDING{"The Android player beside this editor", "Rebuild the editor."};

auto get_incomplete_problem() -> std::string
{
    return fmt::format("{} is incomplete. {}", ANDROID_PLAYER_WORDING.player, ANDROID_PLAYER_WORDING.advice);
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

auto android_template::get_symbols_dir() const -> fs::path
{
    return root / "symbols";
}

auto android_template::get_sdl_java_dir() const -> fs::path
{
    return root / "sdl" / "java";
}

auto android_template::get_dotnet_dir() const -> fs::path
{
    return root / "dotnet";
}

auto android_template::get_framework_dir() const -> fs::path
{
    // The folder is named after the runtime's target framework: net<major>.<minor> of its version.
    return get_dotnet_dir() / "lib" / ("net" + dotnet_runtime.substr(0, dotnet_runtime.rfind('.')));
}

namespace
{
auto make_owner_line(const char* key, const std::string& value) -> std::string
{
    return std::string(key) + "=" + value + "\n";
}
} // namespace

auto write_android_build_owner(const fs::path& build_dir, const android_build_owner& owner) -> bool
{
    fs::error_code ec;
    fs::create_directories(build_dir, ec);
    const auto text = make_owner_line(OWNER_PROJECT_KEY, fs::to_utf8_string(owner.project)) +
                      make_owner_line(OWNER_PROFILE_KEY, owner.profile);
    return write_file(build_dir / BUILD_OWNER_NAME, text.data(), text.size());
}

auto read_android_build_owner(const fs::path& build_dir) -> android_build_owner
{
    auto values = read_stamp(build_dir / BUILD_OWNER_NAME);
    return {fs::from_utf8_string(values[OWNER_PROJECT_KEY]), values[OWNER_PROFILE_KEY]};
}

namespace
{
/// When a deploy last used `build_dir`: when it wrote the folder's owner, or set up its Gradle project; for a
/// folder with neither, when the folder itself last changed.
auto get_last_use(const fs::path& build_dir) -> fs::file_time_type
{
    fs::error_code ec;
    for(const auto& written : {build_dir / BUILD_OWNER_NAME, build_dir / GRADLE_LOCAL_PROPERTIES})
    {
        const auto time = fs::last_write_time(written, ec);
        if(!ec)
        {
            return time;
        }
    }
    return fs::last_write_time(build_dir, ec);
}

auto is_unused_build_dir(const fs::path& build_dir, std::chrono::hours lifetime) -> bool
{
    const auto owner = read_android_build_owner(build_dir);
    if(!owner.project.empty() && !is_present(owner.project))
    {
        return true;
    }
    return get_last_use(build_dir) < fs::file_time_type::clock::now() - lifetime;
}
} // namespace

auto find_unused_android_build_dirs(const fs::path& staging, const fs::path& kept, std::chrono::hours lifetime)
    -> std::vector<fs::path>
{
    std::vector<fs::path> unused;
    fs::error_code ec;
    for(fs::directory_iterator it(staging, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->is_directory(ec) && it->path().filename() != kept.filename() && is_unused_build_dir(it->path(), lifetime))
        {
            unused.push_back(it->path());
        }
    }
    return unused;
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
        lookup.error = get_incomplete_problem();
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
    lookup.error = get_version_problem(found.engine_version, engine_version, ANDROID_PLAYER_WORDING);
    if(!lookup.error.empty())
    {
        return lookup;
    }
    if(found.configuration != configuration)
    {
        lookup.error = fmt::format("{} is a {} build, and the editor is {}. {}",
                                   ANDROID_PLAYER_WORDING.player,
                                   found.configuration,
                                   configuration,
                                   ANDROID_PLAYER_WORDING.advice);
        return lookup;
    }
    const bool is_complete = !found.abi.empty() && !found.dotnet_runtime.empty() &&
                             is_present(found.get_player_dir() / "libmain.so") &&
                             is_present(found.get_symbols_dir() / found.abi / "libmain.so.sym") &&
                             is_present(found.get_project_dir() / "gradle" / "wrapper" / "gradle-wrapper.jar") &&
                             is_present(found.get_sdl_java_dir()) && is_present(found.get_dotnet_dir() / "native");
    if(!is_complete)
    {
        lookup.error = get_incomplete_problem();
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

namespace
{
/// The uses-permission elements of `names`, a line each.
auto make_permission_lines(const std::vector<std::string>& names) -> std::string
{
    std::string lines;
    for(const auto& name : names)
    {
        lines += "    <uses-permission android:name=\"" + name + "\" />\n";
    }
    return lines;
}

/// Where the opening tag that starts at `tag_begin` ends in `xml`: its '>', which a quoted attribute value may
/// hold without ending the tag. npos when the tag does not end.
auto find_tag_end(const std::string& xml, std::size_t tag_begin) -> std::size_t
{
    char quote = 0;
    for(std::size_t index = tag_begin; index < xml.size(); ++index)
    {
        const char c = xml[index];
        if(quote != 0)
        {
            quote = c == quote ? char(0) : quote;
        }
        else if(c == '"' || c == '\'')
        {
            quote = c;
        }
        else if(c == '>')
        {
            return index;
        }
    }
    return std::string::npos;
}
} // namespace

auto make_android_permissions_manifest(const std::vector<std::string>& names) -> std::string
{
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n" +
           make_permission_lines(names) + "</manifest>\n";
}

auto make_android_build_manifest(const std::string& additions_manifest, const std::vector<std::string>& names) -> std::string
{
    if(additions_manifest.empty())
    {
        return make_android_permissions_manifest(names);
    }
    const auto tag_begin = additions_manifest.find(MANIFEST_TAG);
    const auto tag_end = tag_begin == std::string::npos ? tag_begin : find_tag_end(additions_manifest, tag_begin);
    if(tag_end == std::string::npos)
    {
        return {};
    }
    // "<manifest ... />" holds nothing: it is opened to take the permissions, and closed after them.
    const bool is_empty_element = tag_end > 0 && additions_manifest[tag_end - 1] == '/';
    if(is_empty_element)
    {
        return additions_manifest.substr(0, tag_end - 1) + ">\n" + make_permission_lines(names) + MANIFEST_END_TAG +
               additions_manifest.substr(tag_end + 1);
    }
    return additions_manifest.substr(0, tag_end + 1) + "\n" + make_permission_lines(names) + additions_manifest.substr(tag_end + 1);
}

auto android_device::is_wireless() const -> bool
{
    return serial.find(WIRELESS_SERVICE_MARKER) != std::string::npos || serial.find(ADDRESS_PORT_SEPARATOR) != std::string::npos;
}

auto android_device::is_named(const std::string& name) const -> bool
{
    return serial == name || std::find(aliases.begin(), aliases.end(), name) != aliases.end();
}

auto resolve_android_device(const std::vector<android_device>& devices, const std::string& requested) -> std::string
{
    if(requested.empty())
    {
        const auto is_ready = [](const android_device& device)
        {
            return device.is_ready();
        };
        const auto ready = std::find_if(devices.begin(), devices.end(), is_ready);
        return std::count_if(devices.begin(), devices.end(), is_ready) == 1 ? ready->serial : std::string{};
    }
    // A second connection to the phone is folded into one entry: the entry's own serial is used.
    const auto chosen = std::find_if(devices.begin(),
                                     devices.end(),
                                     [&requested](const android_device& device)
                                     {
                                         return device.is_named(requested);
                                     });
    return chosen != devices.end() ? chosen->serial : requested;
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
    // The distribution's own JDKs (java-17-openjdk-amd64), which install_android_studio.sh installs: a JAVA_HOME
    // that names an older Java gives way to one of them.
    add_matching_children("/usr/lib/jvm", "java-", fs::path{}, search.jdk_candidates);
#endif
    return search;
}

auto parse_java_version(const std::string& report) -> int
{
    // The version is the first quoted text: "21.0.3", or "1.8.0_281" for Java 8 and older, which name their major
    // version second.
    const auto begin = report.find('"');
    const auto end = begin == std::string::npos ? begin : report.find('"', begin + 1);
    if(end == std::string::npos)
    {
        return 0;
    }
    std::stringstream numbers(report.substr(begin + 1, end - begin - 1));
    int first = 0;
    int second = 0;
    char separator = 0;
    if(!(numbers >> first))
    {
        return 0;
    }
    if(first != OLD_JAVA_FIRST_NUMBER)
    {
        return first;
    }
    return (numbers >> separator >> second) ? second : first;
}

auto parse_jdk_release_version(const std::string& release) -> int
{
    // The key starts a line: JAVA_RUNTIME_VERSION and the like only end with it.
    const std::string key(JDK_VERSION_KEY);
    for(auto found = release.find(key); found != std::string::npos; found = release.find(key, found + 1))
    {
        if(found == 0 || release[found - 1] == '\n')
        {
            const auto line_end = std::min(release.find('\n', found), release.size());
            return parse_java_version(release.substr(found, line_end - found));
        }
    }
    return 0;
}

auto get_java_version(const fs::path& java) -> int
{
    // A JDK says its version in a file at its top, two folders above its java: reading it starts no program. Only a
    // Java without that file is started and asked.
    std::string release;
    read_whole_file(java.parent_path().parent_path() / JDK_RELEASE_FILE, release);
    if(const int version = parse_jdk_release_version(release); version > 0)
    {
        return version;
    }
    const auto report = subprocess::call({java.string(), "-version"});
    return report.started ? parse_java_version(report.out_output) : 0;
}

auto find_android_toolchain(const android_toolchain_search& search, const java_version_probe& get_version) -> android_toolchain
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
    fs::path old_java;
    int old_java_version = 0;
    for(const auto& jdk : search.jdk_candidates)
    {
        const auto java = jdk / "bin" / java_name;
        if(!is_present(java))
        {
            continue;
        }
        const int version = get_version(java);
        if(version >= MIN_ANDROID_JAVA_VERSION)
        {
            tools.java = java;
            break;
        }
        if(old_java.empty())
        {
            old_java = java;
            old_java_version = version;
        }
    }
    if(tools.sdk.empty())
    {
        tools.error = "No Android SDK was found. Install Android Studio, or set ANDROID_HOME to the SDK folder.";
    }
    else if(tools.java.empty() && !old_java.empty())
    {
        tools.error = fmt::format("The Java at {} is version {}, and Android builds need {} or newer. Install Android "
                                  "Studio, which bundles one, or point JAVA_HOME at a newer JDK.",
                                  old_java.generic_string(),
                                  old_java_version,
                                  MIN_ANDROID_JAVA_VERSION);
    }
    else if(tools.java.empty())
    {
        tools.error = "No Java was found. Install Android Studio, which bundles one, or set JAVA_HOME.";
    }
    return tools;
}

auto get_gradle_distribution_name(const std::string& wrapper_properties) -> std::string
{
    const auto key = wrapper_properties.find(GRADLE_DISTRIBUTION_KEY);
    if(key == std::string::npos)
    {
        return {};
    }
    const auto line_end = std::min(wrapper_properties.find_first_of("\r\n", key), wrapper_properties.size());
    const auto address = wrapper_properties.substr(key, line_end - key);
    const auto name_begin = address.rfind('/');
    const auto name_end = address.rfind(GRADLE_DISTRIBUTION_EXTENSION);
    if(name_begin == std::string::npos || name_end == std::string::npos || name_end <= name_begin)
    {
        return {};
    }
    return address.substr(name_begin + 1, name_end - name_begin - 1);
}

auto has_gradle_distribution(const fs::path& gradle_home, const std::string& name) -> bool
{
    return !name.empty() && is_present(gradle_home / GRADLE_DISTRIBUTIONS_FOLDER / fs::from_utf8_string(name));
}

auto get_gradle_home() -> fs::path
{
    const auto set_home = get_environment("GRADLE_USER_HOME");
    if(!set_home.empty())
    {
        return set_home;
    }
#if defined(_WIN32)
    return get_environment("USERPROFILE") / ".gradle";
#else
    return get_environment("HOME") / ".gradle";
#endif
}

auto parse_android_process_id(const std::string& output, int exit_code) -> std::string
{
    std::stringstream words(output);
    std::string first;
    words >> first;
    const bool is_id = !first.empty() && std::all_of(first.begin(),
                                                     first.end(),
                                                     [](unsigned char c)
                                                     {
                                                         return std::isdigit(c) != 0;
                                                     });
    return exit_code == 0 && is_id ? first : std::string{};
}

android_crash_report_filter::android_crash_report_filter(std::string process)
    : process_(std::move(process))
{
}

auto android_crash_report_filter::add(android_crash_line line) -> std::vector<android_crash_line>
{
    if(line.message.rfind(CRASH_REPORT_START, 0) == 0)
    {
        owner_ = owner::unknown;
        held_.clear();
    }
    if(owner_ == owner::other_process)
    {
        return {};
    }
    if(owner_ == owner::this_process)
    {
        return {std::move(line)};
    }
    const bool names_process = line.message.rfind(CRASH_REPORT_PROCESS, 0) == 0;
    const auto named = names_process ? line.message.substr(std::char_traits<char>::length(CRASH_REPORT_PROCESS)) : std::string{};
    if(held_.size() < CRASH_REPORT_HELD_MAX)
    {
        held_.push_back(std::move(line));
    }
    if(!names_process)
    {
        return {};
    }
    // "pid: 123, tid: 456, name: ...": the id ends at the comma.
    owner_ = named.substr(0, named.find(',')) == process_ ? owner::this_process : owner::other_process;
    return owner_ == owner::this_process ? std::exchange(held_, {}) : std::vector<android_crash_line>{};
}

auto check_android_version_code(std::uint32_t version_code) -> std::string
{
    if(version_code <= MAX_ANDROID_VERSION_CODE)
    {
        return {};
    }
    return fmt::format("The version code {} is above {}, the largest Google Play takes.", version_code, MAX_ANDROID_VERSION_CODE);
}

auto make_signing_environment(const android_signing_passwords& passwords) -> std::vector<std::string>
{
    std::vector<std::string> environment;
    if(!passwords.keystore.empty())
    {
        environment.push_back(std::string(KEYSTORE_PASSWORD_VARIABLE) + "=" + passwords.keystore);
    }
    const auto& key = passwords.key.empty() ? passwords.keystore : passwords.key;
    if(!key.empty())
    {
        environment.push_back(std::string(KEY_PASSWORD_VARIABLE) + "=" + key);
    }
    return environment;
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
        params.is_release ? GRADLE_NO_DAEMON : GRADLE_DAEMON_IDLE_TIMEOUT,
        params.is_release ? "assembleRelease" : "assembleDebug",
        "-Punravel.player=" + player.get_player_dir().generic_string(),
        "-Punravel.symbols=" + player.get_symbols_dir().generic_string(),
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
        "-Punravel.trimmedFramework=" + params.trimmed_framework_list.generic_string(),
    };
    if(params.is_release)
    {
        arguments.push_back("bundleRelease");
        arguments.push_back("packageNativeDebugSymbols");
    }
    if(params.is_release && !params.keystore.empty())
    {
        arguments.push_back("-Punravel.keystore=" + params.keystore.generic_string());
        arguments.push_back("-Punravel.keyAlias=" + params.key_alias);
    }
    if(!params.additions.empty())
    {
        arguments.push_back("-Punravel.additions=" + params.additions.generic_string());
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

auto get_built_bundle(const fs::path& project_dir) -> fs::path
{
    return project_dir / "app" / "build" / "outputs" / "bundle" / "release" / "app-release.aab";
}

auto get_built_native_symbols(const fs::path& project_dir) -> fs::path
{
    return project_dir / "app" / "build" / "outputs" / "native-debug-symbols" / "native-debug-symbols.zip";
}

auto get_bundle_path(const fs::path& package) -> fs::path
{
    return fs::path(package).replace_extension(".aab");
}

auto get_native_symbols_path(const fs::path& package) -> fs::path
{
    return package.parent_path() / (package.stem().string() + NATIVE_SYMBOLS_FILE_SUFFIX);
}

auto make_adb_command(const fs::path& adb, const std::string& device, std::vector<std::string> arguments)
    -> std::vector<std::string>
{
    arguments.insert(arguments.begin(), {adb.string(), "-s", device});
    return arguments;
}

auto make_start_arguments(const std::string& application_id, const std::vector<std::string>& run_arguments)
    -> std::vector<std::string>
{
    std::vector<std::string> arguments{"shell", "am", "start", "-W", "-n", application_id + "/" + PLAYER_ACTIVITY};
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
