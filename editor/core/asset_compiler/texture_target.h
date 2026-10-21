#pragma once

#include <bgfx/bgfx.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/asset_storage.h>
#include <engine/settings/settings.h>
#include <engine/settings/target_platform.h>
#include <filesystem/filesystem.h>

#include <cstdint>
#include <string>

namespace unravel::asset_compiler
{

/// The block-compressed formats a platform's GPUs read.
enum class texture_compression_family : std::uint8_t
{
    /// BC1 to BC7, which desktop GPUs read.
    bc,
    /// ASTC, read by most Android GPUs.
    astc,
    /// ETC2 and EAC, read by every OpenGL ES 3 and Vulkan Android GPU.
    etc2,
};

using texture_import_defaults = settings::asset_settings::texture_importer_settings;

/// What a texture is compiled for: the platform whose per-platform quality applies, the formats its GPUs
/// read, and the defaults a texture left on Project Default takes.
struct texture_target
{
    target_platform platform{target_platform::desktop};
    texture_compression_family family{texture_compression_family::bc};
    texture_import_defaults defaults;
};

/// The parameters a texture compiles with for one target, every Project Default resolved. With the
/// source and its importer settings they decide the compiled file.
struct texture_compile_parameters
{
    texture_compression_family family{texture_compression_family::bc};
    texture_importer_meta::texture_size max_size{texture_importer_meta::texture_size::size_2048};
    texture_importer_meta::compression_quality compression{texture_importer_meta::compression_quality::normal_quality};

    friend auto operator==(const texture_compile_parameters& lhs, const texture_compile_parameters& rhs) -> bool = default;
};

/**
 * @brief The parameters `importer` compiles with for `target`: the platform's own quality while it is
 * overridden, otherwise the texture's; Project Default from the target's defaults, then normal quality
 * and 2048. An uncompressed texture compiles the same for every format family, so its parameters name the
 * family BC.
 */
auto resolve_texture_parameters(const texture_importer_meta& importer, const texture_target& target) -> texture_compile_parameters;

/**
 * @brief The ASTC format an LDR texture of `width` x `height` texels compiles to at `quality`.
 *
 * texturec resamples a texture to whole blocks, rounding its size up before and after fitting it into
 * `max_side` (0 for no limit). So a texture takes the most compressed block of its quality that keeps the
 * size it would have without blocks (low 8x8, 6x6, 5x5, 4x4; normal 6x6, 5x5, 4x4; high 4x4), and 4x4 when
 * none does, the size BC's 4x4 blocks give it on desktop: a variant has the size of the texture it stands
 * in for, which UI sprite rectangles and pixel sizes are measured in.
 */
auto select_astc_ldr_format(std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t max_side,
                            texture_importer_meta::compression_quality quality) -> bgfx::TextureFormat::Enum;

/**
 * @brief The parameters as a manifest records them, with the texture compiler's version, such as
 * "v2/astc/compression_3/size_6": a compiled texture whose manifest records other parameters is compiled
 * again.
 */
auto to_string(const texture_compile_parameters& parameters) -> std::string;

/// The target the editor compiles the project's textures for in its cache (compiled/): this computer, BC,
/// the project's defaults. The project manager sets it from the project settings; any thread may read it.
void set_editor_texture_target(const texture_target& target);
auto get_editor_texture_target() -> texture_target;

/**
 * @brief The target the editor compiles the texture at `key` (a protocol or absolute path) for: the editor
 * texture target for the project's textures, and the default target for the engine's and the editor's,
 * which every project shares.
 */
auto get_editor_texture_target_for(const fs::path& key) -> texture_target;

/**
 * @brief Compiles the texture whose source is at `key` (a protocol or absolute path to the source or to
 * its .meta) for `target` into `output`, with the manifest beside it, both written atomically.
 *
 * Reads the texture's importer settings without writing them: a texture without settings compiles with
 * the defaults a new one gets.
 * @return False when it could not be compiled; the log says why.
 */
auto compile_texture_for_target(const fs::path& key, const fs::path& output, const texture_target& target) -> bool;

/**
 * @brief Whether `output` holds the texture at `key` as compile_texture_for_target would write it for
 * `target` now: the file and its manifest exist, the manifest is of the current format and fingerprint
 * algorithm, it records the parameters the target resolves, and the source, its settings and its
 * dependencies have the fingerprint it records.
 */
auto is_texture_compiled_for_target(const fs::path& key, const fs::path& output, const texture_target& target) -> bool;

/// The parameters of the texture at `key` for `target`, read from its importer settings.
auto get_texture_parameters(const fs::path& key, const texture_target& target) -> texture_compile_parameters;

/// The importer settings of the texture at `key`, without writing them: the defaults a new texture gets when
/// it has none.
auto read_texture_importer(const fs::path& key) -> texture_importer_meta;

/// is_texture_compiled_for_target for parameters already resolved (resolve_texture_parameters).
auto is_texture_compiled_with(const fs::path& key, const fs::path& output, const texture_compile_parameters& parameters) -> bool;

} // namespace unravel::asset_compiler
