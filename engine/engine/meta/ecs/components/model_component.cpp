#include "model_component.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/model.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/set.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{
namespace
{

/// The key a model's morph weights are saved under: an object whose members are the targets' names.
constexpr const char* MORPH_WEIGHTS_KEY = "morph_weights";

template<typename Archive>
void save_morph_weights(Archive& ar, const std::vector<morph_weight>& weights)
{
    if(weights.empty())
    {
        return;
    }
    ar.setNextName(MORPH_WEIGHTS_KEY);
    ar.startNode();
    {
        // Each weight's path is the one its load and its prefab override use: "morph_weights/<name>".
        const serialization::path_segment_guard list(MORPH_WEIGHTS_KEY);
        for(const auto& entry : weights)
        {
            try_save(ar, ser20::make_nvp(entry.name.c_str(), entry.weight));
        }
    }
    ar.finishNode();
}

template<typename Archive>
auto load_morph_weights(Archive& ar) -> std::vector<morph_weight>
{
    std::vector<morph_weight> weights;
    serialization::load_named_members(ar,
                                      MORPH_WEIGHTS_KEY,
                                      [&](const std::string& name)
                                      {
                                          morph_weight entry{name, 0.0f};
                                          if(!try_load(ar, ser20::make_nvp(entry.name.c_str(), entry.weight)))
                                          {
                                              return false;
                                          }
                                          weights.push_back(std::move(entry));
                                          return true;
                                      });
    return weights;
}

/// Whether the path filter in force reads the weight @p name; always without a filter.
auto is_morph_weight_listed(const std::string& name) -> bool
{
    const auto* path_ctx = serialization::get_path_context();
    if(path_ctx == nullptr || !path_ctx->is_recording())
    {
        return true;
    }
    const serialization::path_segment_guard member(name);
    return path_ctx->should_serialize_property(path_ctx->get_current_path());
}

/**
 * @brief The model's weights after a load: every saved weight the filter in force lists is set, every listed weight
 *        the document lacks goes back to the mesh's default, and the rest stay. Without a filter that is the document's
 *        weights alone.
 */
template<typename Archive>
auto merge_listed_morph_weights(Archive& ar, std::vector<morph_weight> weights) -> std::vector<morph_weight>
{
    std::vector<morph_weight> saved;
    {
        const serialization::path_context_pause unfiltered;
        saved = load_morph_weights(ar);
    }
    const serialization::path_segment_guard list(MORPH_WEIGHTS_KEY);
    const auto is_saved = [&saved](const std::string& name)
    {
        return std::any_of(saved.begin(),
                           saved.end(),
                           [&name](const morph_weight& entry)
                           {
                               return entry.name == name;
                           });
    };
    weights.erase(std::remove_if(weights.begin(),
                                 weights.end(),
                                 [&](const morph_weight& entry)
                                 {
                                     return !is_saved(entry.name) && is_morph_weight_listed(entry.name);
                                 }),
                  weights.end());
    for(auto& entry : saved)
    {
        if(!is_morph_weight_listed(entry.name))
        {
            continue;
        }
        auto found = std::find_if(weights.begin(),
                                  weights.end(),
                                  [&entry](const morph_weight& existing)
                                  {
                                      return existing.name == entry.name;
                                  });
        if(found != weights.end())
        {
            found->weight = entry.weight;
        }
        else
        {
            weights.push_back(std::move(entry));
        }
    }
    return weights;
}

} // namespace

REFLECT(model_component)
{
    entt::meta_factory<decal_receive>{}
        .type("decal_receive"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "decal_receive"},
            entt::attribute{"pretty_name", "Decal Receive"},
        })
        .data<decal_receive::automatic>("automatic"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "automatic"},
            entt::attribute{"pretty_name", "Auto"},
        })
        .data<decal_receive::on>("on"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "on"},
            entt::attribute{"pretty_name", "On"},
        })
        .data<decal_receive::off>("off"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "off"},
            entt::attribute{"pretty_name", "Off"},
        });

    entt::meta_factory<model_component>{}
        .type("model_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Model"},
        })
        .func<&component_meta<model_component>::exists>("component_exists"_hs)
        .func<&component_meta<model_component>::add>("component_add"_hs)
        .func<&component_meta<model_component>::remove>("component_remove"_hs)
        .func<&component_meta<model_component>::save>("component_save"_hs)
        .func<&component_meta<model_component>::load>("component_load"_hs)
        .data<&model_component::set_enabled, &model_component::is_enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Is the model visible?"},
        })
        .data<&model_component::set_static, &model_component::is_static>("static"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "static"},
            entt::attribute{"pretty_name", "Static"},
            entt::attribute{"tooltip", "Is the model static?"},
        })
        .data<&model_component::set_casts_shadow, &model_component::casts_shadow>("casts_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip", "Is the model casting shadows?"},
        })
        .data<&model_component::set_decal_receive, &model_component::get_decal_receive>("receive_decals"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "receive_decals"},
            entt::attribute{"pretty_name", "Receive Decals"},
            entt::attribute{"tooltip", "Whether decals draw on the model. Auto: every model but a skinned one, as a decal fixed "
                                       "in the world slides on a moving character and a puddle should not paint the feet "
                                       "walking through it. Off suits water, glass and anything a level's decals must leave "
                                       "alone."},
        })
        .data<&model_component::set_post_process_mask, &model_component::get_post_process_mask>("post_process_mask"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "post_process_mask"},
            entt::attribute{"pretty_name", "Post Process Mask"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 255},
            entt::attribute{"tooltip", "A value from 1 to 255 that post-process materials find the model by, also where "
                                       "something hides it: outlines on what the player can use, silhouettes seen through "
                                       "walls. They read it with Scene Texture's Custom Mask and Custom Depth, or run only "
                                       "over it with Run Where Mask. 0 leaves the model out."},
        })
        .data<&model_component::set_sort_priority, &model_component::get_sort_priority>("sort_priority"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "sort_priority"},
            entt::attribute{"pretty_name", "Sort Priority"},
            entt::attribute{"tooltip", "Where the model's see-through surfaces of the translucency pass draw among the "
                                       "other see-through surfaces and particles: a lower value draws first, behind the "
                                       "others, and equal values draw from far to near. Set it where one large surface "
                                       "around smaller ones sorts wrong, such as water under a boat's windows."},
        })
        .data<nullptr, &model_component::get_world_bounds>("world_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_bounds"},
            entt::attribute{"pretty_name", "World Bounds"},
            entt::attribute{"tooltip", "The world bounds of the model."},
        })
        .data<&model_component::set_model, &model_component::get_model>("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
            // Its materials and LODs are the component's own rows, not a "Model" fold inside the Model component.
            entt::attribute{"flattable", true},
        });
}

SAVE(model_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("static", obj.is_static()));
    try_save(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow()));
    try_save(ar, ser20::make_nvp("receive_decals", obj.get_decal_receive()));
    try_save(ar, ser20::make_nvp("post_process_mask", obj.get_post_process_mask()));
    try_save(ar, ser20::make_nvp("model", obj.get_model()));
    try_save(ar, ser20::make_nvp("sort_priority", obj.get_sort_priority()));
    save_morph_weights(ar, obj.get_morph_weights());
}
SAVE_INSTANTIATE(model_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model_component, ser20::oarchive_binary_t);

LOAD(model_component)
{
    bool is_enabled{true};
    if(try_load(ar, ser20::make_nvp("enabled", is_enabled)))
    {
        obj.set_enabled(is_enabled);
    }

    bool is_static{};
    if(try_load(ar, ser20::make_nvp("static", is_static)))
    {
        obj.set_static(is_static);
    }

    bool casts_shadow{};
    if(try_load(ar, ser20::make_nvp("casts_shadow", casts_shadow)))
    {
        obj.set_casts_shadow(casts_shadow);
    }

    decal_receive receive_decals{decal_receive::automatic};
    if(try_load(ar, ser20::make_nvp("receive_decals", receive_decals)))
    {
        obj.set_decal_receive(receive_decals);
    }

    int post_process_mask{};
    if(try_load(ar, ser20::make_nvp("post_process_mask", post_process_mask)))
    {
        obj.set_post_process_mask(post_process_mask);
    }

    auto mod = obj.get_model();
    if(try_load(ar, ser20::make_nvp("model", mod)))
    {
        obj.set_model(mod);
    }

    int sort_priority{};
    if(try_load(ar, ser20::make_nvp("sort_priority", sort_priority)))
    {
        obj.set_sort_priority(sort_priority);
    }

    obj.set_morph_weights(merge_listed_morph_weights(ar, obj.get_morph_weights()));
}
LOAD_INSTANTIATE(model_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model_component, ser20::iarchive_binary_t);

REFLECT(bone_component)
{
    entt::meta_factory<bone_component>{}
        .type("bone_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Bone"},
        })
        .func<&component_meta<bone_component>::exists>("component_exists"_hs)
        .func<&component_meta<bone_component>::add>("component_add"_hs)
        .func<&component_meta<bone_component>::save>("component_save"_hs)
        .func<&component_meta<bone_component>::load>("component_load"_hs)
        .func<&component_meta<bone_component>::remove>("component_remove"_hs)
        .data<nullptr, &bone_component::bone_index>("bone_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_index"},
            entt::attribute{"pretty_name", "Bone Index"},
            entt::attribute{"tooltip", "The bone index this object represents."},
        });
}

SAVE(bone_component)
{
    try_save(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
SAVE_INSTANTIATE(bone_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(bone_component, ser20::oarchive_binary_t);

LOAD(bone_component)
{
    try_load(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
LOAD_INSTANTIATE(bone_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(bone_component, ser20::iarchive_binary_t);

REFLECT(submesh_entry)
{
    entt::meta_factory<submesh_entry>{}
        .type("submesh_entry"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_entry"},
            entt::attribute{"pretty_name", "Submesh Entry"},
        })
        .data<nullptr, &submesh_entry::submesh_index>("submesh_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_index"},
            entt::attribute{"pretty_name", "Submesh Index"},
            entt::attribute{"tooltip", "Index of the submesh in the mesh asset."},
        })
        .data<&submesh_entry::casts_shadow>("casts_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip", "Whether this submesh casts shadows."},
        })
        .data<&submesh_entry::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Whether this submesh is rendered."},
        });
}

REFLECT(submesh_component)
{
    entt::meta_factory<submesh_component>{}
        .type("submesh_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Submesh"},
        })
        .func<&component_meta<submesh_component>::exists>("component_exists"_hs)
        .func<&component_meta<submesh_component>::add>("component_add"_hs)
        .func<&component_meta<submesh_component>::remove>("component_remove"_hs)
        .func<&component_meta<submesh_component>::save>("component_save"_hs)
        .func<&component_meta<submesh_component>::load>("component_load"_hs)
        .data<&submesh_component::entries>("entries"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entries"},
            entt::attribute{"pretty_name", "Entries"},
            entt::attribute{"tooltip", "Per-submesh render settings (shadows, visibility)."},
            // The mesh asset dictates which submeshes exist; users edit entry settings only.
            entt::attribute{"is_fixed_size_array", true},
        });
}

SAVE(submesh_entry)
{
    try_save(ar, ser20::make_nvp("submesh_index", obj.submesh_index));
    try_save(ar, ser20::make_nvp("stable_id", obj.stable_id));
    try_save(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow));
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
}
SAVE_INSTANTIATE(submesh_entry, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(submesh_entry, ser20::oarchive_binary_t);

LOAD(submesh_entry)
{
    try_load(ar, ser20::make_nvp("submesh_index", obj.submesh_index));
    try_load(ar, ser20::make_nvp("stable_id", obj.stable_id));
    try_load(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow));
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
}
LOAD_INSTANTIATE(submesh_entry, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(submesh_entry, ser20::iarchive_binary_t);

SAVE(submesh_component)
{
    try_save(ar, ser20::make_nvp("entries", obj.entries));
}
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_binary_t);

LOAD(submesh_component)
{
    try_load(ar, ser20::make_nvp("entries", obj.entries));

    // Legacy data only serialized the bare index vector - synthesize default entries.
    std::vector<uint32_t> legacy_indices;
    try_load(ar, ser20::make_nvp("submeshes", legacy_indices));
    obj.migrate_legacy_indices(legacy_indices);
}
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_binary_t);

} // namespace unravel
