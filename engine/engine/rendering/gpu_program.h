#pragma once
#include <engine/engine_export.h>

#include <engine/assets/asset_handle.h>

#include <graphics/program.h>
#include <graphics/shader.h>
#include <math/math.h>

namespace unravel
{
/**
 * @brief Class representing a GPU program.
 */
class gpu_program
{
public:
    using ptr = std::shared_ptr<gpu_program>;
    using wptr = std::weak_ptr<gpu_program>;
    using uptr = std::unique_ptr<gpu_program>;

    /**
     * @brief Default constructor.
     */
    gpu_program() = default;

    /**
     * @brief Constructor to create a program from a compute shader asset.
     *
     * @param compute_shader The compute shader asset.
     */
    gpu_program(asset_handle<gfx::shader> compute_shader);

    /**
     * @brief Constructor to create a program from vertex and fragment shader assets.
     *
     * @param vertex_shader The vertex shader asset.
     * @param fragment_shader The fragment shader asset.
     */
    gpu_program(asset_handle<gfx::shader> vertex_shader, asset_handle<gfx::shader> fragment_shader);

    /**
     * @brief Begins usage of the program. Checks validity of attached shaders and
     * recreates the internal program if necessary.
     *
     * @return true if the program begins successfully, false otherwise.
     */
    auto begin() -> bool;

    /**
     * @brief Indicates the end of working with a program.
     */
    void end();

    /**
     * @brief Sets the texture for a specific stage using a frame buffer.
     *
     * @param _stage The stage number.
     * @param _sampler The sampler name.
     * @param _handle The frame buffer handle.
     * @param _attachment The attachment point.
     * @param _flags The texture flags.
     */
    void set_texture(std::uint8_t stage,
                     const hpp::string_view& sampler,
                     const gfx::frame_buffer* handle,
                     uint8_t attachment = 0,
                     std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

    /**
     * @brief Sets the texture for a specific stage using a texture.
     *
     * @param _stage The stage number.
     * @param _sampler The sampler name.
     * @param _texture The texture handle.
     * @param _flags The texture flags.
     */
    void set_texture(std::uint8_t stage,
                     const hpp::string_view& sampler,
                     const gfx::texture* texture,
                     std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

    /**
     * @brief Sets a uniform value in the shader program.
     *
     * @param _name The name of the uniform.
     * @param _value The value to set.
     * @param _num The number of elements (default is 1).
     */
    void set_uniform(const hpp::string_view& name, const void* value, std::uint16_t num = 1);

    /**
     * @brief Sets a uniform value in the shader program.
     *
     * @param _name The name of the uniform.
     * @param _value The vec4 value to set.
     * @param _num The number of elements (default is 1).
     */
    void set_uniform(const hpp::string_view& name, const math::vec4& value, std::uint16_t num = 1);

    /**
     * @brief Sets a uniform value in the shader program.
     *
     * @param _name The name of the uniform.
     * @param _value The vec3 value to set.
     * @param _num The number of elements (default is 1).
     */
    void set_uniform(const hpp::string_view& name, const math::vec3& value, std::uint16_t num = 1);

    /**
     * @brief Sets a uniform value in the shader program.
     *
     * @param _name The name of the uniform.
     * @param _value The vec2 value to set.
     * @param _num The number of elements (default is 1).
     */
    void set_uniform(const hpp::string_view& name, const math::vec2& value, std::uint16_t num = 1);

    /**
     * @brief Retrieves a uniform from the shader program.
     *
     * @param _name The name of the uniform.
     * @return The uniform pointer.
     */
    auto get_uniform(const hpp::string_view& name) -> gfx::program::uniform_ptr;

    /**
     * @brief Retrieves the native handle of the internal shader program.
     *
     * @return The native handle of the program.
     */
    auto native_handle() const -> gfx::program::handle_type_t;

    /**
     * @brief Retrieves the shader assets that created the shader program.
     *
     * @return The vector of shader assets.
     */
    auto get_shaders() const -> const std::vector<asset_handle<gfx::shader>>&;

    /**
     * @brief Checks if the GPU program is valid.
     *
     * @return true if the program is valid, false otherwise.
     */
    auto is_valid() const -> bool;

    /**
     * @brief Populates the GPU program.
     */
    void populate();

    /**
     * @brief Attaches a shader to the GPU program.
     *
     * @param shader The shader to attach.
     */
    void attach_shader(asset_handle<gfx::shader> shader);

private:
    std::vector<asset_handle<gfx::shader>> shaders_; ///< Shaders that created this program.
    std::vector<gfx::shader::ptr> shaders_cached_;      ///< Cached shaders.
    std::shared_ptr<gfx::program> program_;          ///< The GPU program.
};

/**
 * @brief Structure for caching uniforms.
 */
struct uniforms_cache
{
    /**
     * @brief Caches a uniform in the GPU program.
     *
     * @param program The GPU program.
     * @param uniform The uniform to cache.
     * @param name The name of the uniform.
     */

    void cache_uniform(gpu_program* program, gfx::program::uniform_ptr& uniform, const hpp::string_view& name, gfx::uniform_type type, uint16_t num = 1)
    {
        program->begin();
        uniform = program->get_uniform(name);

        if(uniform == nullptr)
        {
            uniform = std::make_shared<gfx::uniform>(std::string(name), type, num);
            //APPLOG_TRACE("Uniform {} created.", name);
        }
        else
        {
            //APPLOG_TRACE("Uniform {} found.", name);
        }

        program->end();
    }
};

} // namespace unravel

namespace gfx
{

/**
 * @brief Sets the transformation matrices.
 *
 * @param matrices The vector of transformation matrices.
 */
void set_world_transform(const std::vector<math::transform::mat4_t>& matrices);
void set_transform(const std::vector<math::transform::mat4_t>& matrices);

/**
 * @brief Sets the transformations.
 *
 * @param matrices The vector of transformations.
 */
void set_world_transform(const std::vector<math::transform>& matrices);
void set_transform(const std::vector<math::transform>& matrices);

/**
 * @brief Sets the transformation matrix.
 *
 * @param matrix The transformation matrix.
 */
void set_world_transform(const math::transform::mat4_t& matrix);
void set_transform(const math::transform::mat4_t& matrix);

/**
 * @brief Sets the transformation.
 *
 * @param matrix The transformation.
 */
void set_world_transform(const math::transform& matrix);
void set_transform(const math::transform& matrix);

/**
 * @brief Sets the texture for a specific stage using a frame buffer.
 *
 * @param uniform The uniform pointer.
 * @param _stage The stage number.
 * @param _handle The frame buffer handle.
 * @param _attachment The attachment point.
 * @param _flags The texture flags.
 */
void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::frame_buffer::ptr& handle,
                 uint8_t attachment = 0,
                 std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

/**
 * @brief Sets the texture for a specific stage using a texture.
 *
 * @param uniform The uniform pointer.
 * @param _stage The stage number.
 * @param _texture The texture handle.
 * @param _flags The texture flags.
 */
void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::texture::ptr& texture,
                 std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

/**
 * @brief Sets the texture for a specific stage using a frame buffer.
 *
 * @param uniform The uniform pointer.
 * @param _stage The stage number.
 * @param _handle The frame buffer handle.
 * @param _attachment The attachment point.
 * @param _flags The texture flags.
 */
void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::frame_buffer* handle,
                 uint8_t attachment = 0,
                 std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

/**
 * @brief Sets the texture for a specific stage using a texture.
 *
 * @param uniform The uniform pointer.
 * @param _stage The stage number.
 * @param _texture The texture handle.
 * @param _flags The texture flags.
 */
void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const gfx::texture* texture,
                 std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

/**
 * @brief Sets the texture for a specific stage using a texture.
 *
 * @param uniform The uniform pointer.
 * @param _stage The stage number.
 * @param _texture The texture handle.
 * @param _flags The texture flags.
 */
void set_texture(const gfx::program::uniform_ptr& uniform,
                 std::uint8_t stage,
                 const asset_handle<gfx::texture>& texture,
                 std::uint32_t flags = std::numeric_limits<std::uint32_t>::max());

/**
 * @brief Sets a uniform value.
 *
 * @param uniform The uniform pointer.
 * @param _value The value to set.
 * @param _num The number of elements (default is 1).
 */
void set_uniform(const gfx::program::uniform_ptr& uniform, const void* value, std::uint16_t num = 1);

/**
 * @brief Sets a uniform value.
 *
 * @param uniform The uniform pointer.
 * @param _value The mat4 value to set.
 * @param _num The number of elements (default is 1).
 */
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::mat4& value, std::uint16_t num = 1);

/**
 * @brief Sets a uniform value.
 *
 * @param uniform The uniform pointer.
 * @param _value The vec4 value to set.
 * @param _num The number of elements (default is 1).
 */
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec4& value, std::uint16_t num = 1);

/**
 * @brief Sets a uniform value.
 *
 * @param uniform The uniform pointer.
 * @param _value The vec3 value to set.
 * @param _num The number of elements (default is 1).
 */
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec3& value, std::uint16_t num = 1);

/**
 * @brief Sets a uniform value.
 *
 * @param uniform The uniform pointer.
 * @param _value The vec2 value to set.
 * @param _num The number of elements (default is 1).
 */
void set_uniform(const gfx::program::uniform_ptr& uniform, const math::vec2& value, std::uint16_t num = 1);

} // namespace gfx
