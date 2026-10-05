/*
 * Mesh importer corpus digest (opt-in).
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   UNRAVEL_MESH_IMPORT_CORPUS=<list> <build-dir>/bin/unravel-tests --suite "mesh import"
 *
 * Imports every model of a corpus list through importer::load_mesh_data_from_file and writes a
 * deterministic digest of everything the importer produces: geometry, node tree, skin, animation
 * clips, material values and texture bindings, the texture manifest and every file the import wrote,
 * rewrote or removed next to its source. Two digests of one corpus from two builds differ exactly
 * where the importer output differs - which a refactor must never do, and a fix must do only where
 * it was meant to.
 *
 * The importer writes converted textures and .meta files next to its source, so every copy root is
 * copied into a scratch folder under app:/ (texture keys need a protocol) and imported there; the
 * corpus itself is never touched. Models sharing a copy root import in list order from one copy.
 *
 * Environment:
 *   UNRAVEL_MESH_IMPORT_CORPUS  list file, one "<copy root>|<model relative to the copy root>" per
 *                               line; a relative copy root resolves against the list's folder and
 *                               '#' starts a comment. Unset: the suite is skipped.
 *   UNRAVEL_MESH_IMPORT_DIGEST  digest output path (default: run_<stamp>_digest.txt next to the run
 *                               folder).
 *   UNRAVEL_MESH_IMPORT_KEEP    keep the scratch copies (default: removed after the run).
 */

#include "../tests.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/importers/mesh_importer.h>
#include <engine/rendering/material.h>
#include <logging/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

using namespace unravel;

namespace
{

constexpr uint64_t fnv_offset_basis = 14695981039346656037ull;
constexpr uint64_t fnv_prime = 1099511628211ull;

struct corpus_entry
{
    fs::path copy_root;
    fs::path model;
};

/// FNV-1a over raw bytes. Bit-exact on purpose: the digest compares one build against another.
class digest_hash
{
public:
    void add_bytes(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for(size_t i = 0; i < size; ++i)
        {
            value_ = (value_ ^ bytes[i]) * fnv_prime;
        }
    }

    template<typename T>
    void add(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        add_bytes(&value, sizeof(T));
    }

    void add_string(const std::string& text)
    {
        add(text.size());
        add_bytes(text.data(), text.size());
    }

    void add_matrix(const math::mat4& matrix)
    {
        add_bytes(math::value_ptr(matrix), sizeof(float) * 16);
    }

    auto get_value() const -> uint64_t
    {
        return value_;
    }

private:
    uint64_t value_{fnv_offset_basis};
};

using file_times_t = std::map<std::string, fs::file_time_type>;

auto read_corpus(const fs::path& list_path) -> std::vector<corpus_entry>
{
    std::vector<corpus_entry> entries;
    std::ifstream stream(list_path);
    std::string line;
    while(std::getline(stream, line))
    {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        const size_t separator = line.find('|');
        if(line.empty() || line[0] == '#' || separator == std::string::npos)
        {
            continue;
        }
        fs::path copy_root = line.substr(0, separator);
        if(copy_root.is_relative())
        {
            copy_root = list_path.parent_path() / copy_root;
        }
        entries.push_back({copy_root.lexically_normal(), fs::path(line.substr(separator + 1))});
    }
    return entries;
}

auto get_env(const char* name) -> std::string
{
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

auto make_group_name(const corpus_entry& entry, size_t index) -> std::string
{
    std::string name = entry.copy_root.filename().string();
    std::replace_if(
        name.begin(),
        name.end(),
        [](char c)
        {
            return c == ' ' || c == '.';
        },
        '_');
    return fmt::format("g{:02}_{}", index, name);
}

auto collect_file_times(const fs::path& root) -> file_times_t
{
    file_times_t times;
    fs::error_code err;
    for(fs::recursive_directory_iterator it(root, err), end; it != end; it.increment(err))
    {
        if(err)
        {
            break;
        }
        if(it->is_regular_file(err))
        {
            times[fs::relative(it->path(), root, err).generic_string()] = it->last_write_time(err);
        }
    }
    return times;
}

/// Content hash of a generated file. Meta files carry a random uid when the import creates them.
auto hash_generated_file(const fs::path& path) -> uint64_t
{
    std::ifstream stream(path, std::ios::binary);
    digest_hash hash;
    if(path.extension() != ".meta")
    {
        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        hash.add_string(content);
        return hash.get_value();
    }
    std::string line;
    while(std::getline(stream, line))
    {
        if(line.find("\"uid\"") == std::string::npos)
        {
            hash.add_string(line);
        }
    }
    return hash.get_value();
}

class digest_writer
{
public:
    explicit digest_writer(std::string run_key_prefix) : run_key_prefix_(std::move(run_key_prefix))
    {
    }

    template<typename... Args>
    void line(fmt::format_string<Args...> format, Args&&... args)
    {
        text_ += fmt::format(format, std::forward<Args>(args)...);
        text_ += '\n';
    }

    /// Asset keys embed the run folder; strip it so two runs compare.
    auto normalize_key(std::string key) const -> std::string
    {
        const size_t pos = key.find(run_key_prefix_);
        if(pos != std::string::npos)
        {
            key.replace(pos, run_key_prefix_.size(), "<run>/");
        }
        return key;
    }

    auto get_text() const -> const std::string&
    {
        return text_;
    }

private:
    std::string run_key_prefix_;
    std::string text_;
};

auto format_vec3(const math::vec3& value) -> std::string
{
    return fmt::format("({:.5f},{:.5f},{:.5f})", value.x, value.y, value.z);
}

auto format_vec4(const math::vec4& value) -> std::string
{
    return fmt::format("({:.5f},{:.5f},{:.5f},{:.5f})", value.x, value.y, value.z, value.w);
}

void write_geometry(digest_writer& out, const mesh::load_data& data)
{
    digest_hash vertex_hash;
    vertex_hash.add_bytes(data.vertex_data.data(), data.vertex_data.size());
    digest_hash triangle_hash;
    for(const auto& triangle : data.triangle_data)
    {
        triangle_hash.add(triangle.data_group_id);
        triangle_hash.add(triangle.indices);
        triangle_hash.add(triangle.flags);
    }
    out.line("  geometry vertices={} stride={} triangles={} materials={} submeshes={}",
             data.vertex_count,
             data.vertex_format.getStride(),
             data.triangle_count,
             data.material_count,
             data.submeshes.size());
    out.line("  vertex_hash={:016x} triangle_hash={:016x}", vertex_hash.get_value(), triangle_hash.get_value());
    out.line("  bbox {} {}", format_vec3(data.bbox.min), format_vec3(data.bbox.max));
    for(size_t i = 0; i < data.submeshes.size(); ++i)
    {
        const auto& submesh = data.submeshes[i];
        out.line("  submesh[{}] group={} vertices={}+{} faces={}+{} skinned={} stable_id={:08x} bbox {} {}",
                 i,
                 submesh.data_group_id,
                 submesh.vertex_start,
                 submesh.vertex_count,
                 submesh.face_start,
                 submesh.face_count,
                 submesh.skinned,
                 submesh.stable_id,
                 format_vec3(submesh.bbox.min),
                 format_vec3(submesh.bbox.max));
    }
}

void write_nodes(digest_writer& out, const mesh::load_data& data)
{
    if(!data.root_node)
    {
        out.line("  nodes <none>");
        return;
    }
    digest_hash hash;
    size_t count = 0;
    const std::function<void(const mesh::armature_node&, int)> visit = [&](const mesh::armature_node& node, int depth)
    {
        ++count;
        hash.add(depth);
        hash.add_string(node.name);
        hash.add(node.index);
        hash.add_matrix(node.local_transform.get_matrix());
        hash.add(node.submeshes.size());
        hash.add_bytes(node.submeshes.data(), node.submeshes.size() * sizeof(uint32_t));
        for(const auto& child : node.children)
        {
            if(child)
            {
                visit(*child, depth + 1);
            }
        }
    };
    visit(*data.root_node, 0);
    out.line("  nodes count={} hash={:016x} root='{}' root_translation={}",
             count,
             hash.get_value(),
             data.root_node->name,
             format_vec3(data.root_node->local_transform.get_translation()));
}

void write_skin(digest_writer& out, const mesh::load_data& data)
{
    const auto& bones = data.skin_data.get_bones();
    out.line("  bones count={}", bones.size());
    for(const auto& bone : bones)
    {
        digest_hash hash;
        hash.add_matrix(bone.bind_pose_transform.get_matrix());
        for(const auto& influence : bone.influences)
        {
            hash.add(influence.vertex_index);
            hash.add(influence.weight);
        }
        out.line("  bone '{}' influences={} hash={:016x} bounds {} {}",
                 bone.bone_id,
                 bone.influences.size(),
                 hash.get_value(),
                 format_vec3(bone.bounds.min),
                 format_vec3(bone.bounds.max));
    }
}

void write_animations(digest_writer& out, const std::vector<animation_clip>& animations)
{
    out.line("  animations count={}", animations.size());
    for(const auto& clip : animations)
    {
        digest_hash hash;
        for(const auto& channel : clip.channels)
        {
            hash.add_string(channel.node_name);
            hash.add(channel.node_index);
            for(const auto& key : channel.position_keys)
            {
                hash.add(key.time.count());
                hash.add(key.value);
            }
            for(const auto& key : channel.rotation_keys)
            {
                hash.add(key.time.count());
                hash.add(key.value);
            }
            for(const auto& key : channel.scaling_keys)
            {
                hash.add(key.time.count());
                hash.add(key.value);
            }
        }
        out.line("  clip '{}' duration={:.5f} channels={} hash={:016x} root_position='{}'#{} root_rotation='{}'#{}",
                 clip.name,
                 clip.duration.count(),
                 clip.channels.size(),
                 hash.get_value(),
                 clip.root_motion.position_node_name,
                 clip.root_motion.position_node_index,
                 clip.root_motion.rotation_node_name,
                 clip.root_motion.rotation_node_index);
    }
}

void write_materials(digest_writer& out, const std::vector<importer::imported_material>& materials)
{
    out.line("  materials count={}", materials.size());
    for(const auto& imported : materials)
    {
        const auto pbr = std::dynamic_pointer_cast<pbr_material>(imported.mat);
        if(!pbr)
        {
            out.line("  material '{}' <not pbr>", imported.name);
            continue;
        }
        out.line("  material '{}' base={} metal={:.5f} rough={:.5f} bump={:.5f} emissive={}x{:.5f} alpha={} cutoff={:.5f} "
                 "cull={}",
                 imported.name,
                 format_vec4(pbr->get_base_color().value),
                 pbr->get_metalness(),
                 pbr->get_roughness(),
                 pbr->get_bumpiness(),
                 format_vec4(pbr->get_emissive_color().value),
                 pbr->get_emissive_intensity(),
                 static_cast<uint32_t>(pbr->get_alpha_mode()),
                 pbr->get_alpha_cutoff(),
                 static_cast<uint32_t>(pbr->get_cull_type()));
        out.line("    maps color='{}' normal='{}' rough='{}' metal='{}' ao='{}' emissive='{}'",
                 out.normalize_key(pbr->get_color_map().id()),
                 out.normalize_key(pbr->get_normal_map().id()),
                 out.normalize_key(pbr->get_roughness_map().id()),
                 out.normalize_key(pbr->get_metalness_map().id()),
                 out.normalize_key(pbr->get_ao_map().id()),
                 out.normalize_key(pbr->get_emissive_map().id()));
    }
}

void write_texture_manifest(digest_writer& out, const std::vector<importer::imported_texture>& textures)
{
    out.line("  textures count={}", textures.size());
    for(const auto& texture : textures)
    {
        out.line("  texture '{}' semantic='{}' embedded={} inverse={} processed={} flags={:08x}",
                 texture.name,
                 texture.semantic,
                 texture.embedded_index,
                 texture.inverse,
                 texture.process_count,
                 texture.flags);
    }
}

void write_file_changes(digest_writer& out, const fs::path& root, const file_times_t& before)
{
    const file_times_t after = collect_file_times(root);
    for(const auto& [relative, time] : after)
    {
        const auto previous = before.find(relative);
        if(previous != before.end() && previous->second == time)
        {
            continue;
        }
        out.line("  file {} '{}' hash={:016x}",
                 previous == before.end() ? "created" : "rewritten",
                 relative,
                 hash_generated_file(root / relative));
    }
    for(const auto& [relative, time] : before)
    {
        if(after.find(relative) == after.end())
        {
            out.line("  file removed '{}'", relative);
        }
    }
}

void import_model(digest_writer& out, asset_manager& am, const fs::path& group_root, const fs::path& model)
{
    const fs::path source = group_root / model;
    const file_times_t before = collect_file_times(group_root);
    mesh_importer_meta import_meta{};
    mesh::load_data data;
    std::vector<animation_clip> animations;
    std::vector<importer::imported_material> materials;
    std::vector<importer::imported_texture> textures;
    const auto start = std::chrono::steady_clock::now();
    bool loaded = false;
    try
    {
        loaded = importer::load_mesh_data_from_file(am, source, import_meta, data, animations, materials, textures);
    }
    catch(const std::exception& e)
    {
        out.line("  exception: {}", e.what());
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
    std::printf("  %-48s %s %9.1f ms\n", model.generic_string().c_str(), loaded ? "ok  " : "FAIL", elapsed.count());
    out.line("  loaded={}", loaded);
    write_geometry(out, data);
    write_nodes(out, data);
    write_skin(out, data);
    write_animations(out, animations);
    write_materials(out, materials);
    write_texture_manifest(out, textures);
    write_file_changes(out, group_root, before);
}

auto run_mesh_import_digest(rtti::context& ctx) -> int
{
    const std::string corpus_path = get_env("UNRAVEL_MESH_IMPORT_CORPUS");
    if(corpus_path.empty())
    {
        std::printf("  skipped: set UNRAVEL_MESH_IMPORT_CORPUS to a corpus list to run\n");
        return 0;
    }
    const auto entries = read_corpus(corpus_path);
    if(entries.empty())
    {
        std::printf("  FAIL: corpus list '%s' has no entries\n", corpus_path.c_str());
        return 1;
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::string run_name = "run_" + std::to_string(stamp);
    const fs::path run_root = fs::resolve_protocol("app:/mesh_import") / run_name;
    digest_writer out("app:/mesh_import/" + run_name + "/");
    auto& am = ctx.get_cached<asset_manager>();
    int failures = 0;
    std::map<std::string, fs::path> group_roots;
    for(const auto& entry : entries)
    {
        const std::string source_key = entry.copy_root.generic_string();
        auto group = group_roots.find(source_key);
        if(group == group_roots.end())
        {
            const fs::path group_root = run_root / make_group_name(entry, group_roots.size());
            fs::error_code err;
            fs::create_directories(group_root, err);
            fs::copy(entry.copy_root, group_root, fs::copy_options::recursive | fs::copy_options::overwrite_existing, err);
            if(err)
            {
                std::printf("  FAIL: copying '%s': %s\n", source_key.c_str(), err.message().c_str());
                ++failures;
                continue;
            }
            group = group_roots.emplace(source_key, group_root).first;
        }
        out.line("== {}|{}", entry.copy_root.filename().generic_string(), entry.model.generic_string());
        import_model(out, am, group->second, entry.model);
    }
    std::string digest_path = get_env("UNRAVEL_MESH_IMPORT_DIGEST");
    if(digest_path.empty())
    {
        digest_path = (run_root.parent_path() / (run_name + "_digest.txt")).string();
    }
    {
        std::ofstream stream(digest_path, std::ios::binary);
        stream << out.get_text();
    }
    std::printf("  digest: %s\n", digest_path.c_str());
    if(get_env("UNRAVEL_MESH_IMPORT_KEEP").empty())
    {
        fs::error_code err;
        fs::remove_all(run_root, err);
    }
    return failures;
}

} // namespace

REGISTER_TEST_SUITE("mesh import corpus digest", run_mesh_import_digest)
