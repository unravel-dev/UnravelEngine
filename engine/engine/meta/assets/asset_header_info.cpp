#include "asset_header_info.hpp"

#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

SAVE(asset_header_info)
{
    try_save(ar, ser20::make_nvp("file_size", obj.file_size));
}
SAVE_INSTANTIATE(asset_header_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(asset_header_info, ser20::oarchive_binary_t);

LOAD(asset_header_info)
{
    try_load(ar, ser20::make_nvp("file_size", obj.file_size));
}
LOAD_INSTANTIATE(asset_header_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(asset_header_info, ser20::iarchive_binary_t);

//-----------------------------------------------------------------------------
SAVE(texture_header_info)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_save(ar, ser20::make_nvp("width", obj.width));
    try_save(ar, ser20::make_nvp("height", obj.height));
    try_save(ar, ser20::make_nvp("depth", obj.depth));
    try_save(ar, ser20::make_nvp("num_layers", obj.num_layers));
    try_save(ar, ser20::make_nvp("num_mips", obj.num_mips));
    try_save(ar, ser20::make_nvp("format", obj.format));
}
SAVE_INSTANTIATE(texture_header_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(texture_header_info, ser20::oarchive_binary_t);

LOAD(texture_header_info)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_load(ar, ser20::make_nvp("width", obj.width));
    try_load(ar, ser20::make_nvp("height", obj.height));
    try_load(ar, ser20::make_nvp("depth", obj.depth));
    try_load(ar, ser20::make_nvp("num_layers", obj.num_layers));
    try_load(ar, ser20::make_nvp("num_mips", obj.num_mips));
    try_load(ar, ser20::make_nvp("format", obj.format));
}
LOAD_INSTANTIATE(texture_header_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(texture_header_info, ser20::iarchive_binary_t);

//-----------------------------------------------------------------------------
SAVE(mesh_header_info)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_save(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_save(ar, ser20::make_nvp("index_count", obj.index_count));
    try_save(ar, ser20::make_nvp("submesh_count", obj.submesh_count));
    try_save(ar, ser20::make_nvp("lod_count", obj.lod_count));
}
SAVE_INSTANTIATE(mesh_header_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(mesh_header_info, ser20::oarchive_binary_t);

LOAD(mesh_header_info)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_load(ar, ser20::make_nvp("vertex_count", obj.vertex_count));
    try_load(ar, ser20::make_nvp("index_count", obj.index_count));
    try_load(ar, ser20::make_nvp("submesh_count", obj.submesh_count));
    try_load(ar, ser20::make_nvp("lod_count", obj.lod_count));
}
LOAD_INSTANTIATE(mesh_header_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(mesh_header_info, ser20::iarchive_binary_t);

//-----------------------------------------------------------------------------
SAVE(animation_header_info)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_save(ar, ser20::make_nvp("duration", obj.duration));
    try_save(ar, ser20::make_nvp("channel_count", obj.channel_count));
    try_save(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
}
SAVE_INSTANTIATE(animation_header_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(animation_header_info, ser20::oarchive_binary_t);

LOAD(animation_header_info)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_load(ar, ser20::make_nvp("duration", obj.duration));
    try_load(ar, ser20::make_nvp("channel_count", obj.channel_count));
    try_load(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
}
LOAD_INSTANTIATE(animation_header_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(animation_header_info, ser20::iarchive_binary_t);

//-----------------------------------------------------------------------------
SAVE(audio_header_info)
{
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_save(ar, ser20::make_nvp("duration", obj.duration));
    try_save(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
    try_save(ar, ser20::make_nvp("channels", obj.channels));
}
SAVE_INSTANTIATE(audio_header_info, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(audio_header_info, ser20::oarchive_binary_t);

LOAD(audio_header_info)
{
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<asset_header_info>(&obj)));
    try_load(ar, ser20::make_nvp("duration", obj.duration));
    try_load(ar, ser20::make_nvp("sample_rate", obj.sample_rate));
    try_load(ar, ser20::make_nvp("channels", obj.channels));
}
LOAD_INSTANTIATE(audio_header_info, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(audio_header_info, ser20::iarchive_binary_t);

} // namespace unravel
