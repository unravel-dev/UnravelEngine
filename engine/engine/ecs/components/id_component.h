#pragma once
#include <engine/engine_export.h>

#include <string>
#include <uuid/uuid.h>

namespace unravel
{
template<typename T>
struct id_component_base
{
    void regenerate_id()
    {
        id = generate_uuid();
    }

    void generate_if_nil()
    {
        if(id.is_nil()) 
        {
            id = generate_uuid();
        }
    }
    /**
     * @brief The unique identifier for the entity.
     */
    hpp::uuid id;
};
/**
 * @struct id_component
 * @brief Component that provides a unique identifier (UUID) for an entity.
 */
struct id_component : public id_component_base<id_component>
{

};

} // namespace unravel
