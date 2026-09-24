#pragma once

#include "handle_impl.h"
#include <string>

#include "frame_buffer.h"
#include "texture.h"

#include <limits>
#include <map>
#include <memory>

namespace gfx
{

void deinit_uniform_cache();

struct uniform : public handle_impl<uniform, bgfx::UniformHandle>
{
    uniform() = default;
    ~uniform();
    //-----------------------------------------------------------------------------
    //  Name : populate ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    uniform(const std::string& _name, bgfx::UniformType::Enum _type, std::uint16_t _num = 1);

    //-----------------------------------------------------------------------------
    //  Name : populate ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    uniform(handle_type_t _handle);

    //-----------------------------------------------------------------------------
    //  Name : set_texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void set_texture(uint8_t _stage,
                     const gfx::frame_buffer* _handle,
                     uint8_t _attachment = 0,
                     uint32_t _flags = std::numeric_limits<uint32_t>::max());

    //-----------------------------------------------------------------------------
    //  Name : set_texture ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void set_texture(uint8_t _stage,
                     const gfx::texture* _texture,
                     uint32_t _flags = std::numeric_limits<uint32_t>::max());

    //-----------------------------------------------------------------------------
    //  Name : set_uniform ()
    /// <summary>
    ///
    ///
    ///
    /// </summary>
    //-----------------------------------------------------------------------------
    void set_uniform(const void* _value, uint16_t _num = 1);

    /// Uniform info
    bgfx::UniformInfo info;
};

} // namespace gfx
