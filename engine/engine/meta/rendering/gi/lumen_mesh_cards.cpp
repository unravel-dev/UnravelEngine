#include "lumen_mesh_cards.hpp"

#include <engine/meta/core/math/bbox.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

SAVE(lumen_card)
{
    try_save(ar, ser20::make_nvp("origin", obj.origin));
    try_save(ar, ser20::make_nvp("extent", obj.extent));
    try_save(ar, ser20::make_nvp("axis_x", obj.axis_x));
    try_save(ar, ser20::make_nvp("axis_y", obj.axis_y));
    try_save(ar, ser20::make_nvp("axis_z", obj.axis_z));
    try_save(ar, ser20::make_nvp("direction", obj.direction));
}
SAVE_INSTANTIATE(lumen_card, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(lumen_card, ser20::oarchive_associative_t);

LOAD(lumen_card)
{
    try_load(ar, ser20::make_nvp("origin", obj.origin));
    try_load(ar, ser20::make_nvp("extent", obj.extent));
    try_load(ar, ser20::make_nvp("axis_x", obj.axis_x));
    try_load(ar, ser20::make_nvp("axis_y", obj.axis_y));
    try_load(ar, ser20::make_nvp("axis_z", obj.axis_z));
    try_load(ar, ser20::make_nvp("direction", obj.direction));
}
LOAD_INSTANTIATE(lumen_card, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(lumen_card, ser20::iarchive_associative_t);

SAVE(lumen_mesh_cards)
{
    try_save(ar, ser20::make_nvp("bounds", obj.bounds));
    try_save(ar, ser20::make_nvp("cards", obj.cards));
    try_save(ar, ser20::make_nvp("is_mostly_two_sided", obj.is_mostly_two_sided));
}
SAVE_INSTANTIATE(lumen_mesh_cards, ser20::oarchive_binary_t);
SAVE_INSTANTIATE(lumen_mesh_cards, ser20::oarchive_associative_t);

LOAD(lumen_mesh_cards)
{
    try_load(ar, ser20::make_nvp("bounds", obj.bounds));
    try_load(ar, ser20::make_nvp("cards", obj.cards));
    try_load(ar, ser20::make_nvp("is_mostly_two_sided", obj.is_mostly_two_sided));
}
LOAD_INSTANTIATE(lumen_mesh_cards, ser20::iarchive_binary_t);
LOAD_INSTANTIATE(lumen_mesh_cards, ser20::iarchive_associative_t);

} // namespace unravel
