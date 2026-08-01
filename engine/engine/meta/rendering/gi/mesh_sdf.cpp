#include "mesh_sdf.hpp"

#include <engine/meta/core/math/bbox.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

SAVE(mesh_sdf)
{
    try_save(ar, ser20::make_nvp("bounds", obj.bounds));
    try_save(ar, ser20::make_nvp("voxel_size", obj.voxel_size));
    try_save(ar, ser20::make_nvp("grid_dim", obj.grid_dim));
    try_save(ar, ser20::make_nvp("brick_dim", obj.brick_dim));
    try_save(ar, ser20::make_nvp("is_two_sided", obj.is_two_sided));
    try_save(ar, ser20::make_nvp("two_sided_thickness", obj.two_sided_thickness));
    try_save(ar, ser20::make_nvp("indirection", obj.indirection));
    try_save(ar, ser20::make_nvp("brick_voxels", obj.brick_voxels));
}
SAVE_INSTANTIATE(mesh_sdf, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(mesh_sdf, ser20::oarchive_associative_t);

LOAD(mesh_sdf)
{
    try_load(ar, ser20::make_nvp("bounds", obj.bounds));
    try_load(ar, ser20::make_nvp("voxel_size", obj.voxel_size));
    try_load(ar, ser20::make_nvp("grid_dim", obj.grid_dim));
    try_load(ar, ser20::make_nvp("brick_dim", obj.brick_dim));
    try_load(ar, ser20::make_nvp("is_two_sided", obj.is_two_sided));
    try_load(ar, ser20::make_nvp("two_sided_thickness", obj.two_sided_thickness));
    try_load(ar, ser20::make_nvp("indirection", obj.indirection));
    try_load(ar, ser20::make_nvp("brick_voxels", obj.brick_voxels));
}
LOAD_INSTANTIATE(mesh_sdf, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(mesh_sdf, ser20::iarchive_associative_t);

} // namespace unravel
