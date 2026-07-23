#include "boot_config.h"

#include <engine/meta/settings/settings.hpp>
#include <engine/settings/settings.h>

#include <logging/logging.h>

namespace unravel
{

auto preferred_renderer_to_string(preferred_renderer value) -> hpp::string_view
{
    switch(value)
    {
        case preferred_renderer::opengl:
            return "opengl";
        case preferred_renderer::vulkan:
            return "vulkan";
        case preferred_renderer::direct3d11:
            return "directx11";
        case preferred_renderer::direct3d12:
            return "directx12";
        case preferred_renderer::metal:
            return "metal";
        case preferred_renderer::auto_detect:
        default:
            return "auto";
    }
}

auto preferred_renderer_pretty_name(preferred_renderer value) -> hpp::string_view
{
    switch(value)
    {
        case preferred_renderer::opengl:
            return "OpenGL";
        case preferred_renderer::vulkan:
            return "Vulkan";
        case preferred_renderer::direct3d11:
            return "Direct3D 11";
        case preferred_renderer::direct3d12:
            return "Direct3D 12";
        case preferred_renderer::metal:
            return "Metal";
        case preferred_renderer::auto_detect:
        default:
            return "Auto";
    }
}

auto preferred_renderers_for_platform(renderer_platform platform) -> hpp::span<const preferred_renderer>
{
    static constexpr preferred_renderer windows_backends[] = {
        preferred_renderer::auto_detect,
        preferred_renderer::opengl,
        preferred_renderer::vulkan,
        preferred_renderer::direct3d11,
        preferred_renderer::direct3d12,
    };
    static constexpr preferred_renderer linux_backends[] = {
        preferred_renderer::auto_detect,
        preferred_renderer::opengl,
        preferred_renderer::vulkan,
    };
    static constexpr preferred_renderer macos_backends[] = {
        preferred_renderer::auto_detect,
        preferred_renderer::metal,
        preferred_renderer::vulkan,
    };
    switch(platform)
    {
        case renderer_platform::windows:
            return windows_backends;
        case renderer_platform::linux:
            return linux_backends;
        case renderer_platform::macos:
            return macos_backends;
        default:
            return windows_backends;
    }
}

auto is_preferred_renderer_available_on(preferred_renderer value, renderer_platform platform) -> bool
{
    for(const preferred_renderer candidate : preferred_renderers_for_platform(platform))
    {
        if(candidate == value)
        {
            return true;
        }
    }
    return false;
}

auto preferred_renderer_from_string(hpp::string_view value) -> preferred_renderer
{
    if(value == "opengl")
    {
        return preferred_renderer::opengl;
    }
    if(value == "vulkan")
    {
        return preferred_renderer::vulkan;
    }
    if(value == "directx11" || value == "direct3d11" || value == "dx11")
    {
        return preferred_renderer::direct3d11;
    }
    if(value == "directx12" || value == "direct3d12" || value == "dx12")
    {
        return preferred_renderer::direct3d12;
    }
    if(value == "metal")
    {
        return preferred_renderer::metal;
    }
    return preferred_renderer::auto_detect;
}

auto preferred_renderer_to_gfx_type(preferred_renderer value) -> gfx::renderer_type
{
    switch(value)
    {
        case preferred_renderer::opengl:
            return gfx::renderer_type::OpenGL;
        case preferred_renderer::vulkan:
            return gfx::renderer_type::Vulkan;
        case preferred_renderer::direct3d11:
            return gfx::renderer_type::Direct3D11;
        case preferred_renderer::direct3d12:
            return gfx::renderer_type::Direct3D12;
        case preferred_renderer::metal:
            return gfx::renderer_type::Metal;
        case preferred_renderer::auto_detect:
        default:
            return gfx::renderer_type::Count;
    }
}

auto physics_backend_to_string(physics_backend_type value) -> hpp::string_view
{
    switch(value)
    {
        case physics_backend_type::bullet:
        default:
            return "bullet";
    }
}

auto physics_backend_pretty_name(physics_backend_type value) -> hpp::string_view
{
    switch(value)
    {
        case physics_backend_type::bullet:
        default:
            return "Bullet";
    }
}

auto physics_backend_from_string(hpp::string_view value) -> physics_backend_type
{
    if(value == "bullet" || value == "bullet3")
    {
        return physics_backend_type::bullet;
    }
    return physics_backend_type::bullet;
}

auto available_physics_backends() -> hpp::span<const physics_backend_type>
{
    static constexpr physics_backend_type backends[] = {physics_backend_type::bullet};
    return backends;
}

auto boot_config_from_settings(const settings& project_settings) -> boot_config
{
    boot_config result{};
    result.renderer = project_settings.graphics.renderer.get_for_current_platform();
    result.physics = project_settings.physics.backend;
    return result;
}

auto resolve_boot_config(const cmd_line::parser& parser, const boot_config& project_hint) -> boot_config
{
    boot_config result = project_hint;
    std::string preferred_renderer_arg;
    if(parser.try_get("renderer", preferred_renderer_arg) && !preferred_renderer_arg.empty())
    {
        const preferred_renderer from_cli = preferred_renderer_from_string(preferred_renderer_arg);
        if(preferred_renderer_arg != "auto" && from_cli != preferred_renderer::auto_detect)
        {
            result.cli.renderer = true;
            result.renderer = from_cli;
        }
    }
    std::string physics_arg;
    if(parser.try_get("physics", physics_arg) && !physics_arg.empty() && physics_arg != "auto")
    {
        result.cli.physics = true;
        result.physics = physics_backend_from_string(physics_arg);
    }
    return result;
}

auto peek_project_boot_config(const fs::path& project_path) -> boot_config
{
    const fs::path settings_path = project_path / "settings" / "settings.cfg";
    fs::error_code err;
    if(!fs::exists(settings_path, err))
    {
        return {};
    }
    settings project_settings{};
    if(!load_from_file(settings_path.string(), project_settings))
    {
        APPLOG_WARNING("Failed to peek project settings at {}", settings_path.string());
        return {};
    }
    return boot_config_from_settings(project_settings);
}

auto boot_config_requires_restart(const boot_config& active, const boot_config& project) -> bool
{
    boot_config effective = project;
    if(active.cli.renderer)
    {
        effective.renderer = active.renderer;
    }
    if(active.cli.physics)
    {
        effective.physics = active.physics;
    }
    return active != effective;
}

} // namespace unravel
