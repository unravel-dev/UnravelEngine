#include "android.h"

#include <algorithm>
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

auto android_device::get_label() const -> std::string
{
    return model.empty() ? serial : model + " (" + serial + ")";
}

auto parse_android_devices(const std::string& adb_output) -> std::vector<android_device>
{
    std::vector<android_device> devices;
    std::stringstream lines(adb_output);
    std::string line;
    while(std::getline(lines, line))
    {
        std::stringstream words(line);
        android_device device;
        if(!(words >> device.serial >> device.state))
        {
            continue;
        }
        // The header and the server's start messages ("* daemon started successfully").
        if(device.serial == "List" || device.serial == "*")
        {
            continue;
        }
        std::string word;
        while(words >> word)
        {
            if(word.rfind("model:", 0) == 0)
            {
                device.model = word.substr(6);
                std::replace(device.model.begin(), device.model.end(), '_', ' ');
            }
        }
        devices.push_back(std::move(device));
    }
    return devices;
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

auto make_start_arguments(const android_toolchain& tools, const std::string& device, const std::string& application_id)
    -> std::vector<std::string>
{
    return {tools.adb.string(), "-s", device, "shell", "am", "start", "-W", "-n", application_id + "/" + PLAYER_ACTIVITY};
}

} // namespace unravel::deployment
