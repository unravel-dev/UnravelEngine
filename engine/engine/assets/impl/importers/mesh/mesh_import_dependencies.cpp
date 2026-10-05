#include "mesh_import_dependencies.h"

#include "../mesh_importer.h"

#include <logging/logging.h>
#include <string_utils/utils.h>

#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <unordered_map>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

// Mesh formats whose geometry (or required sidecars) can live outside the root file.
// Single-file containers (.glb / .emesh / .fbx / ...) keep vertex buffers in-file.
auto is_multi_file_mesh_format(const fs::path& path) -> bool
{
    const auto ext = string_utils::to_lower(path.extension().string());
    return ext == ".gltf" || ext == ".obj";
}

auto read_text_file(const fs::path& path) -> std::string
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if(!file.is_open())
    {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

struct mesh_sidecar_dependency
{
    fs::path path;
    /// When non-zero (glTF buffer byteLength), wait until file_size >= this value.
    std::uintmax_t minimum_size{0};
    /// Geometry needs it (glTF buffers). Images and material libraries only cost their maps when
    /// missing, so the import proceeds without them once the wait times out.
    bool is_required{true};
};

enum class gltf_dependency_status
{
    /// JSON still truncated / buffers section not fully readable.
    not_ready,
    /// No external files required (embedded data: URIs only, or empty buffers).
    ready_embedded,
    /// External sidecar paths are known and should be waited on.
    ready_external,
};

auto skip_json_ws(const std::string& json, size_t pos) -> size_t
{
    while(pos < json.size() &&
          (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r'))
    {
        ++pos;
    }
    return pos;
}

auto read_json_string(const std::string& json, size_t& pos) -> std::string
{
    pos = skip_json_ws(json, pos);
    if(pos >= json.size() || json[pos] != '"')
    {
        return {};
    }
    ++pos;
    const size_t end = json.find('"', pos);
    if(end == std::string::npos)
    {
        pos = json.size();
        return {};
    }
    std::string value = json.substr(pos, end - pos);
    pos = end + 1;
    return value;
}

auto read_json_uint(const std::string& json, size_t& pos) -> std::uintmax_t
{
    pos = skip_json_ws(json, pos);
    if(pos >= json.size() || json[pos] < '0' || json[pos] > '9')
    {
        return 0;
    }
    std::uintmax_t value = 0;
    while(pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
    {
        value = (value * 10) + static_cast<std::uintmax_t>(json[pos] - '0');
        ++pos;
    }
    return value;
}

/// glTF URIs are percent-encoded ("My%20Texture.png"); the file on disk is not.
auto decode_uri(const std::string& uri) -> std::string
{
    constexpr int hex_radix = 16;
    std::string decoded;
    decoded.reserve(uri.size());
    for(size_t i = 0; i < uri.size(); ++i)
    {
        if(uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1]))
           && std::isxdigit(static_cast<unsigned char>(uri[i + 2])))
        {
            decoded.push_back(static_cast<char>(std::stoi(uri.substr(i + 1, 2), nullptr, hex_radix)));
            i += 2;
            continue;
        }
        decoded.push_back(uri[i]);
    }
    return decoded;
}

/**
 * @brief Parse glTF `buffers` / `images` objects for external uri + optional byteLength.
 *
 * Assimp has no public pre-ReadFile API for listing external URIs; it resolves them
 * only while loading. We scrape the JSON ourselves and wait for companions on disk.
 */
auto parse_gltf_external_dependencies(const std::string& json, const fs::path& parent)
    -> std::pair<gltf_dependency_status, std::vector<mesh_sidecar_dependency>>
{
    std::vector<mesh_sidecar_dependency> deps;
    bool saw_buffers_key = false;
    bool saw_external_buffer = false;
    bool saw_embedded_buffer = false;
    // Prefer structured scrape of buffer/image objects; fall back to any "uri" for images.
    auto append_uri = [&](const std::string& uri, std::uintmax_t minimum_size, bool is_required)
    {
        if(uri.empty() || uri.rfind("data:", 0) == 0)
        {
            if(!uri.empty())
            {
                saw_embedded_buffer = true;
            }
            return;
        }
        mesh_sidecar_dependency dep;
        dep.path = fs::absolute(parent / decode_uri(uri));
        dep.minimum_size = minimum_size;
        dep.is_required = is_required;
        deps.push_back(std::move(dep));
    };
    auto parse_object_uri_and_length = [&](size_t object_begin, size_t object_end, bool is_buffer)
    {
        std::string uri;
        std::uintmax_t byte_length = 0;
        size_t pos = object_begin;
        while(pos < object_end)
        {
            const size_t uri_key = json.find("\"uri\"", pos);
            const size_t len_key = json.find("\"byteLength\"", pos);
            size_t next = std::string::npos;
            bool is_uri = false;
            if(uri_key != std::string::npos && uri_key < object_end)
            {
                next = uri_key;
                is_uri = true;
            }
            if(len_key != std::string::npos && len_key < object_end &&
               (next == std::string::npos || len_key < next))
            {
                next = len_key;
                is_uri = false;
            }
            if(next == std::string::npos)
            {
                break;
            }
            pos = next;
            if(is_uri)
            {
                pos += 5; // "uri"
                pos = skip_json_ws(json, pos);
                if(pos < json.size() && json[pos] == ':')
                {
                    ++pos;
                }
                uri = read_json_string(json, pos);
            }
            else
            {
                pos += 12; // "byteLength"
                pos = skip_json_ws(json, pos);
                if(pos < json.size() && json[pos] == ':')
                {
                    ++pos;
                }
                byte_length = read_json_uint(json, pos);
            }
        }
        if(uri.rfind("data:", 0) == 0)
        {
            saw_embedded_buffer = true;
            return;
        }
        if(!uri.empty())
        {
            saw_external_buffer = true;
            append_uri(uri, byte_length, is_buffer);
        }
    };
    auto parse_named_array_objects = [&](const char* array_key, bool is_buffers)
    {
        const std::string key = array_key;
        size_t key_pos = json.find(key);
        if(key_pos == std::string::npos)
        {
            return false;
        }
        if(is_buffers)
        {
            saw_buffers_key = true;
        }
        size_t pos = key_pos + key.size();
        pos = skip_json_ws(json, pos);
        if(pos >= json.size() || json[pos] != ':')
        {
            return false;
        }
        ++pos;
        pos = skip_json_ws(json, pos);
        if(pos >= json.size() || json[pos] != '[')
        {
            return false;
        }
        ++pos;
        while(pos < json.size())
        {
            pos = skip_json_ws(json, pos);
            if(pos >= json.size())
            {
                return false;
            }
            if(json[pos] == ']')
            {
                return true;
            }
            if(json[pos] != '{')
            {
                return false;
            }
            const size_t object_begin = pos;
            int depth = 0;
            do
            {
                if(json[pos] == '{')
                {
                    ++depth;
                }
                else if(json[pos] == '}')
                {
                    --depth;
                }
                ++pos;
            } while(pos < json.size() && depth > 0);
            if(depth != 0)
            {
                return false;
            }
            parse_object_uri_and_length(object_begin, pos, is_buffers);
            pos = skip_json_ws(json, pos);
            if(pos < json.size() && json[pos] == ',')
            {
                ++pos;
            }
        }
        return false;
    };
    const bool buffers_ok = parse_named_array_objects("\"buffers\"", true);
    const bool images_ok = parse_named_array_objects("\"images\"", false);
    // Truncated glTF often has meshes/nodes but an incomplete trailing buffers array.
    if(saw_buffers_key && !buffers_ok)
    {
        return {gltf_dependency_status::not_ready, {}};
    }
    if(!images_ok && json.find("\"images\"") != std::string::npos)
    {
        // Images are optional for geometry, but if the key exists and is truncated,
        // the file is still landing - keep waiting so Assimp does not race.
        return {gltf_dependency_status::not_ready, {}};
    }
    if(!saw_buffers_key)
    {
        // No buffers key yet: either still copying, or malformed. Do not treat as ready.
        if(json.find("\"meshes\"") != std::string::npos || json.find("\"asset\"") != std::string::npos)
        {
            return {gltf_dependency_status::not_ready, {}};
        }
        return {gltf_dependency_status::not_ready, {}};
    }
    if(deps.empty())
    {
        if(saw_embedded_buffer || !saw_external_buffer)
        {
            return {gltf_dependency_status::ready_embedded, {}};
        }
        return {gltf_dependency_status::not_ready, {}};
    }
    return {gltf_dependency_status::ready_external, std::move(deps)};
}

auto collect_obj_external_dependencies(const fs::path& obj_path) -> std::vector<mesh_sidecar_dependency>
{
    std::vector<mesh_sidecar_dependency> deps;
    std::ifstream file(obj_path);
    if(!file.is_open())
    {
        return deps;
    }
    const fs::path parent = obj_path.parent_path();
    std::string line;
    while(std::getline(file, line))
    {
        line.erase(0, line.find_first_not_of(" \t"));
        if(line.rfind("mtllib", 0) != 0)
        {
            continue;
        }
        std::string rest = line.substr(6);
        const auto begin = rest.find_first_not_of(" \t");
        if(begin == std::string::npos)
        {
            continue;
        }
        const auto end = rest.find_last_not_of(" \t\r\n");
        rest = rest.substr(begin, end - begin + 1);
        if(rest.empty())
        {
            continue;
        }
        mesh_sidecar_dependency dep;
        dep.path = fs::absolute(parent / rest);
        dep.is_required = false;
        deps.push_back(std::move(dep));
    }
    return deps;
}

auto is_regular_file_nonempty(const fs::path& path) -> bool
{
    fs::error_code ec;
    if(!fs::exists(path, ec) || !fs::is_regular_file(path, ec))
    {
        return false;
    }
    const auto size = fs::file_size(path, ec);
    return !ec && size > 0;
}

/**
 * @brief Wait until every sidecar exists, meets minimum_size, and size is stable.
 *
 * Used so a .gltf is not compiled while its .bin / textures are still being copied.
 */
auto wait_until_sidecars_ready(const std::vector<mesh_sidecar_dependency>& deps,
                               std::chrono::milliseconds timeout,
                               std::chrono::milliseconds poll_interval = std::chrono::milliseconds(50),
                               int required_stable_samples = 2) -> bool
{
    if(deps.empty())
    {
        return true;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::unordered_map<std::string, std::uintmax_t> last_sizes;
    std::unordered_map<std::string, int> stable_counts;
    last_sizes.reserve(deps.size());
    stable_counts.reserve(deps.size());
    while(std::chrono::steady_clock::now() < deadline)
    {
        bool all_ready = true;
        for(const auto& dep : deps)
        {
            APPLOG_TRACE("Mesh Importer: waiting for sidecar {} (size={}, required>={})",
                         dep.path.generic_string(),
                         dep.minimum_size,
                         dep.minimum_size);
            fs::error_code ec;
            if(!fs::exists(dep.path, ec) || !fs::is_regular_file(dep.path, ec))
            {
                all_ready = false;
                continue;
            }
            const auto size = fs::file_size(dep.path, ec);
            if(ec || size == 0)
            {
                all_ready = false;
                continue;
            }
            if(dep.minimum_size > 0 && size < dep.minimum_size)
            {
                all_ready = false;
                continue;
            }
            const auto key = dep.path.generic_string();
            auto size_it = last_sizes.find(key);
            if(size_it != last_sizes.end() && size_it->second == size)
            {
                ++stable_counts[key];
            }
            else
            {
                last_sizes[key] = size;
                stable_counts[key] = 0;
            }
            if(stable_counts[key] < required_stable_samples)
            {
                all_ready = false;
            }
        }
        if(all_ready)
        {
            return true;
        }
        std::this_thread::sleep_for(poll_interval);
    }
    return false;
}

auto wait_until_files_ready(const std::vector<fs::path>& paths,
                            std::chrono::milliseconds timeout) -> bool
{
    std::vector<mesh_sidecar_dependency> deps;
    deps.reserve(paths.size());
    for(const auto& path : paths)
    {
        mesh_sidecar_dependency dep;
        dep.path = path;
        deps.push_back(std::move(dep));
    }
    return wait_until_sidecars_ready(deps, timeout);
}

} // namespace

auto wait_for_mesh_source_dependencies(const fs::path& path) -> bool
{
    constexpr auto k_timeout = std::chrono::seconds(3);
    // Always wait for the root source to finish landing (atomic copy / watcher races).
    if(!wait_until_files_ready({path}, k_timeout))
    {
        APPLOG_ERROR("Mesh Importer: timed out waiting for source file {}", path.generic_string());
        return false;
    }
    if(!is_multi_file_mesh_format(path))
    {
        return true;
    }
    const auto ext = string_utils::to_lower(path.extension().string());
    const auto deadline = std::chrono::steady_clock::now() + k_timeout;
    std::vector<mesh_sidecar_dependency> deps;
    if(ext == ".gltf")
    {
        // Re-read JSON until buffers/images parse cleanly. A size-stable but truncated
        // .gltf (copy still flushing) previously yielded zero URIs and skipped the .bin wait.
        while(std::chrono::steady_clock::now() < deadline)
        {
            const std::string json = read_text_file(path);
            if(json.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            const auto parsed = parse_gltf_external_dependencies(json, path.parent_path());
            if(parsed.first == gltf_dependency_status::not_ready)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if(parsed.first == gltf_dependency_status::ready_embedded)
            {
                return true;
            }
            deps = parsed.second;
            break;
        }
        if(deps.empty() && std::chrono::steady_clock::now() >= deadline)
        {
            APPLOG_ERROR("Mesh Importer: timed out parsing external dependencies for {}",
                         path.generic_string());
            return false;
        }
    }
    else if(ext == ".obj")
    {
        deps = collect_obj_external_dependencies(path);
        if(deps.empty())
        {
            return true;
        }
    }
    if(deps.empty())
    {
        return true;
    }
    APPLOG_TRACE("Mesh Importer: waiting for {} external dependenc{} for {}",
                 deps.size(),
                 deps.size() == 1 ? "y" : "ies",
                 path.generic_string());
                 
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if(remaining.count() <= 0)
    {
        return false;
    }
    if(wait_until_sidecars_ready(deps, remaining))
    {
        return true;
    }
    bool is_missing_required = false;
    for(const auto& dep : deps)
    {
        fs::error_code ec;
        const auto size = fs::exists(dep.path, ec) ? fs::file_size(dep.path, ec) : 0;
        if(is_regular_file_nonempty(dep.path) && (dep.minimum_size == 0 || size >= dep.minimum_size))
        {
            continue;
        }
        if(dep.is_required)
        {
            is_missing_required = true;
            APPLOG_ERROR("Mesh Importer: missing or incomplete dependency {} "
                         "(size={}, required>={}, required by {})",
                         dep.path.generic_string(),
                         size,
                         dep.minimum_size,
                         path.generic_string());
        }
        else
        {
            APPLOG_WARNING("Mesh Importer: optional dependency {} of {} is missing; importing without it",
                           dep.path.generic_string(),
                           path.generic_string());
        }
    }
    return !is_missing_required;
}

} // namespace mesh_import

auto collect_mesh_external_dependencies(const fs::path& path) -> std::vector<fs::path>
{
    const auto ext = string_utils::to_lower(path.extension().string());
    std::vector<fs::path> deps;
    if(ext == ".gltf")
    {
        const std::string json = mesh_import::read_text_file(path);
        if(json.empty())
        {
            return deps;
        }
        const auto parsed = mesh_import::parse_gltf_external_dependencies(json, path.parent_path());
        if(parsed.first != mesh_import::gltf_dependency_status::ready_external)
        {
            return deps;
        }
        deps.reserve(parsed.second.size());
        for(const auto& dep : parsed.second)
        {
            deps.push_back(dep.path);
        }
        return deps;
    }
    if(ext == ".obj")
    {
        for(const auto& dep : mesh_import::collect_obj_external_dependencies(path))
        {
            deps.push_back(dep.path);
        }
    }
    return deps;
}

} // namespace importer
} // namespace unravel
