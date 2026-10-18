#include "dependencies.h"

#include <base/platform/config.hpp>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace unravel::deployment
{
namespace
{
#if UNRAVEL_PLATFORM_WINDOWS
/// Dependencies.exe marks the modules it found in the executable's own folder.
constexpr const char* APPLICATION_DIRECTORY_MARKER = "[ApplicationDirectory]";
/// Separates the module's name from its path.
constexpr const char* MODULE_PATH_SEPARATOR = " : ";
#else
/// ldd's "name => path (address)" separator.
constexpr const char* LDD_ARROW = "=> ";
constexpr const char* LDD_ADDRESS = " (0x";
#endif

void trim_right(std::string& line)
{
    line.erase(std::find_if(line.rbegin(),
                            line.rend(),
                            [](char c)
                            {
                                return std::isspace(static_cast<unsigned char>(c)) == 0;
                            })
                   .base(),
               line.end());
}

#if UNRAVEL_PLATFORM_WINDOWS
/// A Dependencies.exe line naming a module of the application's folder ("[ApplicationDirectory] a.dll :
/// a.dll"), as a path in `folder`; empty for any other line.
auto parse_dependency_line(const std::string& line, const fs::path& folder) -> std::string
{
    if(line.find(APPLICATION_DIRECTORY_MARKER) == std::string::npos)
    {
        return {};
    }
    const std::size_t separator = line.find(MODULE_PATH_SEPARATOR);
    if(separator == std::string::npos)
    {
        return {};
    }
    auto path = line.substr(separator + std::string(MODULE_PATH_SEPARATOR).size());
    trim_right(path);
    // The scanner names modules relative to the application's folder.
    const fs::path module(path);
    return path.empty() ? std::string{} : (module.is_absolute() ? module : folder / module).string();
}
#else
/// An ldd line naming a library in `folder`; empty for any other line.
auto parse_dependency_line(const std::string& line, const fs::path& folder) -> std::string
{
    const std::size_t arrow = line.find(LDD_ARROW);
    const bool is_local = arrow == std::string::npos;
    const std::size_t start = is_local ? line.find('\t') : arrow;
    if(start == std::string::npos)
    {
        return {};
    }
    auto path = line.substr(start + (is_local ? 1 : std::string(LDD_ARROW).size()));
    path = path.substr(0, path.find(LDD_ADDRESS));
    trim_right(path);
    fs::path library(path);
    if(is_local)
    {
        library = folder / library;
    }
    fs::error_code ec;
    const bool is_in_folder = fs::exists(library, ec) && fs::exists(folder, ec) && fs::equivalent(library.parent_path(), folder, ec);
    return is_in_folder ? library.string() : std::string{};
}
#endif

auto get_scanner_command(const fs::path& executable, const fs::path& scanner) -> std::vector<std::string>
{
#if UNRAVEL_PLATFORM_WINDOWS
    return {scanner.string(), "-modules", executable.string()};
#else
    (void)scanner;
    return {"ldd", executable.string()};
#endif
}

auto scan_dependencies(const fs::path& executable, const fs::path& scanner) -> std::vector<std::string>
{
    const auto command = get_scanner_command(executable, scanner);
    const auto result = subprocess::call(command);
    if(result.retcode != 0 || result.out_output.empty())
    {
        throw std::runtime_error(fmt::format("The dependency scan of {} failed (exit code {}): {}",
                                             executable.generic_string(),
                                             result.retcode,
                                             result.out_output.empty() ? std::string("no output") : result.out_output));
    }
    APPLOG_TRACE("Dependencies: \n{}", result.out_output);
    return parse_dependencies(result.out_output, executable.parent_path());
}

/// The size and write time of the executable and its libraries: a changed one makes a scan stale.
auto make_scan_stamp(const fs::path& executable, const std::vector<std::string>& dependencies) -> std::string
{
    std::string stamp;
    fs::error_code ec;
    const auto append = [&stamp, &ec](const fs::path& file)
    {
        stamp += fmt::format("{}:{}:{};",
                             file.generic_string(),
                             fs::file_size(file, ec),
                             fs::last_write_time(file, ec).time_since_epoch().count());
    };
    append(executable);
    std::for_each(dependencies.begin(), dependencies.end(), append);
    return stamp;
}
} // namespace

auto parse_dependencies(const std::string& output, const fs::path& folder) -> std::vector<std::string>
{
    std::vector<std::string> dependencies;
    std::stringstream stream(output);
    std::string line;
    while(std::getline(stream, line))
    {
        auto dependency = parse_dependency_line(line, folder);
        if(!dependency.empty())
        {
            dependencies.push_back(std::move(dependency));
        }
    }
    return dependencies;
}

auto find_dependencies(const fs::path& executable, const fs::path& scanner) -> std::vector<std::string>
{
    struct scan
    {
        std::string stamp;
        std::vector<std::string> dependencies;
    };
    static std::mutex scans_mutex;
    static std::map<std::string, scan> scans;
    std::lock_guard<std::mutex> lock(scans_mutex);
    const auto found = scans.find(executable.string());
    if(found != scans.end() && found->second.stamp == make_scan_stamp(executable, found->second.dependencies))
    {
        return found->second.dependencies;
    }
    // A failed scan throws before anything is kept, so the next deploy scans again.
    auto dependencies = scan_dependencies(executable, scanner);
    scans[executable.string()] = {make_scan_stamp(executable, dependencies), dependencies};
    return dependencies;
}

} // namespace unravel::deployment
