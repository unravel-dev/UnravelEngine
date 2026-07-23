#pragma once
#include <engine/engine_export.h>

#include <base/platform/config.hpp>
#include <cmd_line/parser.h>
#include <engine/physics/physics_types.h>
#include <filesystem/filesystem.h>
#include <graphics/graphics.h>
#include <hpp/span.hpp>
#include <hpp/string_view.hpp>

#include <cstdint>
#include <string>

namespace unravel
{

struct settings;

/**
 * @brief Preferred graphics backend for cold init (requires process restart to change).
 */
enum class preferred_renderer : uint8_t
{
    auto_detect = 0,
    opengl,
    vulkan,
    direct3d11,
    direct3d12,
    metal,
};

/**
 * @brief Target OS for per-platform renderer preferences in project settings.
 */
enum class renderer_platform : uint8_t
{
    windows = 0,
    linux,
    macos,
};

/**
 * @brief Per-platform preferred renderer stored in project settings.
 */
struct platform_renderer_settings
{
    preferred_renderer windows{preferred_renderer::auto_detect};
    preferred_renderer linux{preferred_renderer::auto_detect};
    preferred_renderer macos{preferred_renderer::auto_detect};

    friend auto operator==(const platform_renderer_settings& lhs, const platform_renderer_settings& rhs) -> bool =
        default;

    /**
     * @brief Returns the preference for the platform this binary was built for.
     */
    [[nodiscard]] auto get_for_current_platform() const -> preferred_renderer
    {
#if UNRAVEL_PLATFORM_WINDOWS
        return windows;
#elif UNRAVEL_PLATFORM_LINUX
        return linux;
#elif UNRAVEL_PLATFORM_OSX
        return macos;
#else
        return preferred_renderer::auto_detect;
#endif
    }
};

/**
 * @brief Resolved cold-boot configuration used when initializing systems.
 *
 * Published on @ref rtti::context before @ref engine::init_core so cold systems
 * (renderer, physics, ...) can read it. Compared against project peeks when
 * opening a project to decide whether a process restart is required.
 *
 * To add a new cold field later: persist it under settings, add a member + cli
 * flag here, wire from_settings / resolve / requires_restart, read it in init,
 * expose UI with restart prompt, and strip its CLI flag in prepare_restart.
 */
struct boot_config
{
    preferred_renderer renderer{preferred_renderer::auto_detect};
    physics_backend_type physics{physics_backend_type::bullet};

    struct cli_overrides
    {
        bool renderer{false};
        bool physics{false};
    } cli;

    friend auto operator==(const boot_config& lhs, const boot_config& rhs) -> bool
    {
        return lhs.renderer == rhs.renderer && lhs.physics == rhs.physics;
    }
};

[[nodiscard]] ENGINE_EXPORT auto preferred_renderer_to_string(preferred_renderer value) -> hpp::string_view;
[[nodiscard]] ENGINE_EXPORT auto preferred_renderer_pretty_name(preferred_renderer value) -> hpp::string_view;
[[nodiscard]] ENGINE_EXPORT auto preferred_renderer_from_string(hpp::string_view value) -> preferred_renderer;
[[nodiscard]] ENGINE_EXPORT auto preferred_renderer_to_gfx_type(preferred_renderer value) -> gfx::renderer_type;

[[nodiscard]] ENGINE_EXPORT auto preferred_renderers_for_platform(renderer_platform platform)
    -> hpp::span<const preferred_renderer>;
[[nodiscard]] ENGINE_EXPORT auto is_preferred_renderer_available_on(preferred_renderer value,
                                                                   renderer_platform platform) -> bool;

[[nodiscard]] ENGINE_EXPORT auto physics_backend_to_string(physics_backend_type value) -> hpp::string_view;
[[nodiscard]] ENGINE_EXPORT auto physics_backend_pretty_name(physics_backend_type value) -> hpp::string_view;
[[nodiscard]] ENGINE_EXPORT auto physics_backend_from_string(hpp::string_view value) -> physics_backend_type;
[[nodiscard]] ENGINE_EXPORT auto available_physics_backends() -> hpp::span<const physics_backend_type>;

/**
 * @brief Extracts cold boot fields from already-loaded project settings.
 */
[[nodiscard]] ENGINE_EXPORT auto boot_config_from_settings(const settings& project_settings) -> boot_config;

/**
 * @brief Builds a boot_config from an optional project hint and CLI overrides.
 */
[[nodiscard]] ENGINE_EXPORT auto resolve_boot_config(const cmd_line::parser& parser, const boot_config& project_hint)
    -> boot_config;

/**
 * @brief Loads project settings from disk (if present) and returns cold boot fields.
 */
[[nodiscard]] ENGINE_EXPORT auto peek_project_boot_config(const fs::path& project_path) -> boot_config;

/**
 * @brief Returns true when applying @p project would require a process restart.
 *
 * Respects explicit CLI overrides on @p active (CLI wins, no restart for that field).
 */
[[nodiscard]] ENGINE_EXPORT auto boot_config_requires_restart(const boot_config& active, const boot_config& project)
    -> bool;

} // namespace unravel
