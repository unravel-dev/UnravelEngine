#pragma once
#include <engine/engine_export.h>

#include <engine/layers/layer_mask.h>
namespace unravel
{

/**
 * @struct layer_component
 * @brief Component that provides a layer mask for an entity.
 */
struct layer_component
{
    layer_mask layers;
};


} // namespace unravel
