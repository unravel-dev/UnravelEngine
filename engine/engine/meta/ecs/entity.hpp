#pragma once
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>

#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <string_view>

namespace unravel
{

    
template<typename Entity>
struct entity_components
{
    Entity entity;
};

template<typename Entity>
struct entity_data
{
    entity_components<Entity> components;
};
enum class clone_mode_t
{
    none,
    cloning_object,
    cloning_prefab_instance,
    updating_prefab,
};
struct save_context
{
    auto get_clone_mode() const -> clone_mode_t
    {
        return clone_mode;
    }

    auto is_cloning() const -> bool
    {
        return clone_mode != clone_mode_t::none;
    }

    auto is_saving_to_prefab() const -> bool
    {
        return to_prefab;
    }
    clone_mode_t clone_mode{};
    bool to_prefab{};
    entt::const_handle save_source{};
};

struct load_context
{    
    auto get_clone_mode() const -> clone_mode_t
    {
        return clone_mode;
    }

    auto is_cloning() const -> bool
    {
        return clone_mode != clone_mode_t::none;
    }

    auto is_updating_prefab() const -> bool
    {
        return !mapping_by_prefab_uid.empty();
    }


    std::vector<entity_data<entt::handle>> entities;
    clone_mode_t clone_mode{};
    entt::registry* reg{};

    // The ids are not globally unique, so we need to map them to the handles
    std::map<entt::entity, entt::handle> mapping_by_eid;

    // The entity uids (from id_component) are globally unique, so we can use them to map entities
    std::map<hpp::uuid, entt::handle> mapping_by_uid;

    struct prefab_uid_mapping_t
    {
        entt::handle handle;
        bool consumed{};
    };

    // The uids are globally unique, so we can use them to map the entities
    std::map<hpp::uuid, prefab_uid_mapping_t> mapping_by_prefab_uid;

};

struct post_load_callbacks
{
    std::function<void(hpp::span<const entt::handle> new_entities)> callback;
};
void push_on_load_callbacks(const post_load_callbacks& callbacks);
void pop_on_load_callbacks();
auto get_post_load_callbacks() -> const post_load_callbacks*;

auto push_load_context(entt::registry& registry) -> bool;
void pop_load_context(bool push_result);
auto get_load_context() -> load_context&;

auto push_save_context() -> bool;
void pop_save_context(bool push_result);
auto get_save_context() -> save_context&;

template<typename T>
concept HasCharAndTraits = requires {
    typename T::char_type;
    typename T::traits_type;
};
template<typename T>
concept HasView = HasCharAndTraits<T> && requires(const T& t) {
    {
        t.view()
    } noexcept -> std::same_as<std::basic_string_view<typename T::char_type, typename T::traits_type>>;
};

void save_to_stream(std::ostream& stream, entt::const_handle obj);
void save_to_file(const std::string& absolute_path, entt::const_handle obj);
void save_to_stream_bin(std::ostream& stream, entt::const_handle obj);
void save_to_file_bin(const std::string& absolute_path, entt::const_handle obj);

// Non-template overloads: asset_writer::atomic_save_to_file is a template that
// calls unqualified save_to_file. Clang two-phase lookup + ADL only searches
// namespace entt for handle arguments, so the template cannot see these
// unravel overloads. Prefer these overloads over the template for handles.
namespace asset_writer
{
auto atomic_save_to_file(const fs::path& key, entt::const_handle obj) -> bool;
auto atomic_save_to_file(const fs::path& key, entt::handle obj) -> bool;
} // namespace asset_writer

void load_from_view(std::string_view view, entt::handle& obj);
void load_from_stream(std::istream& stream, entt::handle& obj);
void load_from_file(const std::string& absolute_path, entt::handle& obj);
void load_from_stream_bin(std::istream& stream, entt::handle& obj);
void load_from_file_bin(const std::string& absolute_path, entt::handle& obj);

auto load_from_prefab_out(const asset_handle<prefab>& pfb,
                          entt::registry& registry,
                          entt::handle& obj) -> bool;

auto load_from_prefab(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle;
auto load_from_prefab_bin(const asset_handle<prefab>& pfb, entt::registry& registry) -> entt::handle;

void clone_entity_from_stream(entt::const_handle src_obj, entt::handle& dst_obj);

void save_to_stream(std::ostream& stream, const scene& scn);
void save_to_file(const std::string& absolute_path, const scene& scn);
void save_to_stream_bin(std::ostream& stream, const scene& scn);
void save_to_file_bin(const std::string& absolute_path, const scene& scn);

void load_from_view(std::string_view view, scene& scn);
void load_from_stream(std::istream& stream, scene& scn);

void load_from_file(const std::string& absolute_path, scene& scn);
void load_from_stream_bin(std::istream& stream, scene& scn);
void load_from_file_bin(const std::string& absolute_path, scene& scn);

auto load_from_prefab(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool;
auto load_from_prefab_bin(const asset_handle<scene_prefab>& pfb, scene& scn) -> bool;

void clone_scene_from_stream(const scene& src_scene, scene& dst_scene);

template<typename Stream, typename T>
void load_from(Stream& stream, T& scn)
{
    stream.seekg(0);

    if constexpr(HasView<Stream>)
    {
        load_from_view(stream.view(), scn);
    }
    else
    {
        load_from_stream(stream, scn);
    }
}
} // namespace unravel

namespace ser20
{

SAVE_EXTERN(entt::const_handle);
LOAD_EXTERN(entt::handle);

template<typename T>
struct basic_handle_link
{
    T handle{};
};
struct const_entity_handle_link : basic_handle_link<entt::const_handle>
{
};
struct entity_handle_link : basic_handle_link<entt::handle>
{
};

SAVE_EXTERN(const_entity_handle_link);
LOAD_EXTERN(entity_handle_link);

} // namespace ser20
