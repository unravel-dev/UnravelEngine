#include "bundled_files.h"

#include <logging/logging.h>
#include <ospp/bundle.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace unravel
{
namespace
{
auto read_text_file(const fs::path& file) -> std::string
{
    std::ifstream stream(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

auto write_file(const fs::path& file, const std::string& content) -> bool
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(content.data(), std::streamsize(content.size()));
    return bool(stream);
}

/// The names in the list, the build id on its first line left out.
auto get_listed_names(const std::string& list) -> std::vector<std::string>
{
    std::vector<std::string> names;
    std::istringstream lines(list);
    std::string line;
    std::getline(lines, line);
    while(std::getline(lines, line))
    {
        if(!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if(!line.empty())
        {
            names.push_back(line);
        }
    }
    return names;
}

auto extract_file(const std::string& name, const fs::path& destination, std::uint64_t& total_size) -> bool
{
    std::string content;
    if(!os::bundle::read_file(name, content))
    {
        APPLOG_ERROR("Cannot read {} from the application package.", name);
        return false;
    }
    if(!write_file(destination, content))
    {
        APPLOG_ERROR("Cannot write {}.", destination.generic_string());
        return false;
    }
    total_size += content.size();
    return true;
}
} // namespace

auto extract_bundled_files() -> bool
{
    if(!os::bundle::is_supported())
    {
        return true;
    }
    std::string list;
    if(!os::bundle::read_file(BUNDLED_FILES_LIST_NAME, list))
    {
        APPLOG_ERROR("The application package has no {}. It was built without game content.", BUNDLED_FILES_LIST_NAME);
        return false;
    }
    const fs::path binary_path = fs::resolve_protocol("binary:/");
    const fs::path extracted_list = binary_path / BUNDLED_FILES_LIST_NAME;
    if(read_text_file(extracted_list) == list)
    {
        APPLOG_INFO("The packaged files of this build are already extracted.");
        return true;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto names = get_listed_names(list);
    std::uint64_t total_size = 0;
    for(const auto& name : names)
    {
        if(!extract_file(name, binary_path / name, total_size))
        {
            return false;
        }
    }
    if(!write_file(extracted_list, list))
    {
        APPLOG_ERROR("Cannot write {}.", extracted_list.generic_string());
        return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    APPLOG_INFO("Extracted {} packaged files ({} KB) in {} ms.", names.size(), total_size / 1024, elapsed.count());
    return true;
}

auto get_bundled_name(const fs::path& file) -> std::string
{
    return file.lexically_relative(fs::resolve_protocol("binary:/")).generic_string();
}

} // namespace unravel
