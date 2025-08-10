#include "asset_compiler.h"
#include "asset_writer.h"
#include "importers/mesh_importer.h"

#include <bx/error.h>
#include <bx/process.h>
#include <bx/string.h>

#include <graphics/shader.h>
#include <graphics/texture.h>
#include <logging/logging.h>
#include <uuid/uuid.h>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <engine/assets/impl/asset_extensions.h>
#include <engine/engine.h>
#include <engine/settings/settings.h>
#include <engine/meta/animation/animation.hpp>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/audio/audio_clip.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/font.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>

#include <engine/meta/scripting/script.hpp>

#include <engine/scripting/ecs/systems/script_system.h>

#include <fstream>
#include <monopp/mono_jit.h>
#include <regex>
#include <subprocess/subprocess.hpp>

#include <core/base/platform/config.hpp>

namespace unravel::asset_compiler
{

namespace
{

auto resolve_path(const std::string& key) -> fs::path
{
    return fs::absolute(fs::resolve_protocol(key));
}

auto resolve_input_file(const fs::path& key) -> fs::path
{
    fs::path absolute_path = fs::convert_to_protocol(key);
    absolute_path = fs::resolve_protocol(fs::replace(absolute_path, ex::get_meta_directory(), ex::get_data_directory()));
    if(absolute_path.extension() == ".meta")
    {
        absolute_path.replace_extension();
    }
    return absolute_path;
}

auto escape_str(const std::string& str) -> std::string
{
    return "\"" + str + "\"";
}

auto run_process(const std::string& process,
                 const std::vector<std::string>& args_array,
                 bool chekc_retcode,
                 std::string& err) -> bool
{
    auto result = subprocess::call(process, args_array);
    err = result.out_output;

    if(!result.err_output.empty())
    {
        if(!err.empty())
        {
            err += "\n";
        }

        err += result.err_output;
    }

    if(err.find("error") != std::string::npos)
    {
        return false;
    }

    return result.retcode == 0;
}

void copy_compiled_file(const fs::path& from, const fs::path& to, const std::string& str_input)
{
    fs::error_code err;
    asset_writer::atomic_copy_file(from, to, err);

    if(!err)
    {
        //APPLOG_INFO("Successful compilation of {0} -> {1}", str_input, to.string());
    }
    else
    {
        APPLOG_ERROR("Failed compilation of {0} -> {1} with error: {2}", str_input, to.filename().string(), err.message());
    }
}

auto select_compressed_format(gfx::texture_format input_format,
                              const fs::path& extension,
                              texture_importer_meta::compression_quality quality) -> gfx::texture_format
{
    if(quality == texture_importer_meta::compression_quality::none)
    {
        return gfx::texture_format::Unknown;
    }

    auto info = gfx::get_format_info(input_format);

    if(extension == ".hdr" || extension == ".exr")
    {
        info.is_hdr = true;
    }

    // 1) HDR? Use BC6H for color data, ignoring alpha (HDR with alpha is non-trivial).
    if(info.is_hdr)
    {
        // BC6H: color (RGB) 16F
        // No standard BC format for HDR alpha in the block-compression range.
        return gfx::texture_format::BC6H;
    }

    // 2) Single channel => BC4
    //    e.g., for grayscale height map or single-channel mask
    if(info.num_hannels == 1)
    {
        return gfx::texture_format::BC4;
    }

    // 3) Two channel => BC5
    //    e.g., typical for 2D vector data, normal map XY
    if(info.num_hannels == 2)
    {
        return gfx::texture_format::BC5;
    }

    // 4) If we reach here, we have 3 or 4 channels in LDR.

    // 4a) No alpha needed => choose BC1 or BC7, etc.
    if(!info.has_alpha_channel)
    {
        switch(quality)
        {
            case texture_importer_meta::compression_quality::low_quality:
                // BC1 is cheap and has no alpha
                return gfx::texture_format::BC1;
            case texture_importer_meta::compression_quality::normal_quality:
                // BC1 is standard for color w/out alpha
                return gfx::texture_format::BC1;
            case texture_importer_meta::compression_quality::high_quality:
                // BC7 is higher quality for color, also supports alpha but not needed here.
                // It is also really slow for encoding so don't use it for now.
                return gfx::texture_format::BC1;
            default:
                break;
        }
        // fallback
        return gfx::texture_format::BC1;
    }
    else
    {
        // 4b) We do have alpha => choose BC2, BC3, or BC7.
        // BC2 (DXT3) is old and rarely used except for sharp alpha transitions.
        // BC3 (DXT5) is the typical solution for alpha textures if BC7 is not an option.
        // BC7 is better (but bigger decode cost).
        switch(quality)
        {
            case texture_importer_meta::compression_quality::low_quality:
                return gfx::texture_format::BC3;
            case texture_importer_meta::compression_quality::normal_quality:
                return gfx::texture_format::BC3; // DXT5
            case texture_importer_meta::compression_quality::high_quality:
                //  BC7 is best BC for RGBA
                // It is also really slow for encoding so don't use it for now.
                return gfx::texture_format::BC3;
            default:
                break;
        }
        // fallback
        return gfx::texture_format::BC3;
    }
}

auto compile_texture_to_file(const fs::path& input_path, 
                            const fs::path& output_path,
                            const texture_importer_meta& importer,
                            const std::string& protocol) -> bool
{
    std::string str_input = input_path.string();
    std::string str_output = output_path.string();
    
    bool try_compress = protocol == "app";
    
    auto quality = importer.quality;
    if(quality.compression == texture_importer_meta::compression_quality::project_default)
    {
        auto& ctx = engine::context();
        if(ctx.has<settings>())
        {
            auto& ss = ctx.get<settings>();
            quality.compression = ss.assets.texture.default_compression;
        }
    }

    if(quality.max_size == texture_importer_meta::texture_size::project_default)
    {
        auto& ctx = engine::context();
        if(ctx.has<settings>())
        {
            auto& ss = ctx.get<settings>();
            quality.max_size = ss.assets.texture.default_max_size;
        }
    }

    // If still default, set to normal quality
    if(quality.compression == texture_importer_meta::compression_quality::project_default)
    {
        quality.compression = texture_importer_meta::compression_quality::normal_quality;
    }

    // If still default, set to 2048
    if(quality.max_size == texture_importer_meta::texture_size::project_default)
    {
        quality.max_size = texture_importer_meta::texture_size::size_2048;
    }

    auto format =
        select_compressed_format(gfx::texture_format::RGBA8, input_path.extension(), quality.compression);
    
    std::vector<std::string> args_array = {
        "-f",
        str_input,
        "-o",
        str_output,
        "--as",
        "dds",
    };
    
    if(try_compress && format != gfx::texture_format::Unknown)
    {
        args_array.emplace_back("-t");
        args_array.emplace_back(gfx::to_string(format));

        if(format == gfx::texture_format::BC7 || format == gfx::texture_format::BC6H)
        {
            APPLOG_INFO("Compressing to {0}. May take a while.", gfx::to_string(format));

            args_array.emplace_back("-q");
            args_array.emplace_back("fastest");
        }
        else if(quality.compression == texture_importer_meta::compression_quality::high_quality)
        {
            args_array.emplace_back("-q");
            args_array.emplace_back("highest");
        }
    }

    if(importer.generate_mipmaps)
    {
        args_array.emplace_back("-m");
    }

    switch(quality.max_size)
    {
        case texture_importer_meta::texture_size::project_default:
        {
            break;
        }
        case texture_importer_meta::texture_size::size_32:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("32");
            break;
        }
        case texture_importer_meta::texture_size::size_64:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("64");
            break;
        }
        case texture_importer_meta::texture_size::size_128:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("128");
            break;
        }
        case texture_importer_meta::texture_size::size_256:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("256");
            break;
        }
        case texture_importer_meta::texture_size::size_512:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("512");
            break;
        }
        case texture_importer_meta::texture_size::size_1024:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("1024");
            break;
        }
        case texture_importer_meta::texture_size::size_2048:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("2048");
            break;
        }
        case texture_importer_meta::texture_size::size_4096:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("4096");
            break;
        }
        case texture_importer_meta::texture_size::size_8192:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("8192");
            break;
        }
        case texture_importer_meta::texture_size::size_16384:
        {
            args_array.emplace_back("--max");
            args_array.emplace_back("16384");
            break;
        }
    }

    switch(importer.type)
    {
        case texture_importer_meta::texture_type::equirect:
        {
            args_array.emplace_back("--equirect");
            break;
        }

        case texture_importer_meta::texture_type::normal_map:
        {
            args_array.emplace_back("--normalmap");
            break;
        }

        default:
            break;
    }

    std::string error;
    
    // Create an empty file at the output location so the process can write to it
    {
        std::ofstream output_file(str_output);
        (void)output_file;
    }
    
    auto texturec = fs::resolve_protocol("binary:/texturec");
    
    // Run the texture compiler directly to the temporary output location
    if(!run_process(texturec.string(), args_array, false, error))
    {
        APPLOG_ERROR("Failed compilation of {0} with error: {1}", str_input, error);
        return false;
    }
    
    return true;
}

auto compile_shader_to_file(const fs::path& input_path, 
                           const fs::path& output_path,
                           gfx::renderer_type renderer) -> bool
{
    std::string str_input = input_path.string();
    std::string str_output = output_path.string();
    
    std::string file = input_path.stem().string();
    fs::path dir = input_path.parent_path();

    fs::path include = fs::resolve_protocol("engine:/data/shaders");
    std::string str_include = include.string();
    
    fs::path varying = dir / (file + ".io");

    fs::error_code err;
    if(!fs::exists(varying, err))
    {
        varying = dir / "varying.def.io";
    }
    if(!fs::exists(varying, err))
    {
        varying = dir / "varying.def.sc";
    }
    
    std::string str_varying = varying.string();
    
    std::string str_platform;
    std::string str_profile;
    std::string str_type;
    std::string str_opt = "3";

    bool vs = hpp::string_view(file).starts_with("vs_");
    bool fs = hpp::string_view(file).starts_with("fs_");
    bool cs = hpp::string_view(file).starts_with("cs_");

    if(renderer == gfx::renderer_type::Vulkan)
    {
        str_platform = "windows";
        str_profile = "spirv";
    }

    if(renderer == gfx::renderer_type::Direct3D11 || renderer == gfx::renderer_type::Direct3D12)
    {
        str_platform = "windows";

        if(vs || fs)
        {
            str_profile = "s_5_0";
            str_opt = "3";
        }
        else if(cs)
        {
            str_profile = "s_5_0";
            str_opt = "1";
        }
    }
    else if(renderer == gfx::renderer_type::OpenGLES)
    {
        str_platform = "android";
        str_profile = "100_es";
    }
    else if(renderer == gfx::renderer_type::OpenGL)
    {
        str_platform = "linux";

        if(vs || fs)
            str_profile = "140";
        else if(cs)
            str_profile = "430";
    }
    else if(renderer == gfx::renderer_type::Metal)
    {
        str_platform = "osx";
        str_profile = "metal";
    }

    if(vs)
        str_type = "vertex";
    else if(fs)
        str_type = "fragment";
    else if(cs)
        str_type = "compute";
    else
        str_type = "unknown";

    std::vector<std::string> args_array = {
        "-f",
        str_input,
        "-o",
        str_output,
        "-i",
        str_include,
        "--varyingdef",
        str_varying,
        "--type",
        str_type,
        "--define",
        "BGFX_CONFIG_MAX_BONES=" + std::to_string(gfx::get_max_blend_transforms())
        //        "--Werror"
    };

    if(!str_platform.empty())
    {
        args_array.emplace_back("--platform");
        args_array.emplace_back(str_platform);
    }

    if(!str_profile.empty())
    {
        args_array.emplace_back("-p");
        args_array.emplace_back(str_profile);
    }

    if(!str_opt.empty())
    {
        args_array.emplace_back("-O");
        args_array.emplace_back(str_opt);
    }

    std::string error;

    // Create an empty file at the output location
    {
        std::ofstream output_file(str_output);
        (void)output_file;
    }

    auto shaderc = fs::resolve_protocol("binary:/shaderc");

    if(!run_process(shaderc.string(), args_array, true, error))
    {
        APPLOG_ERROR("Failed compilation of {0} -> {1} with error: {2}", str_input, output_path.filename().string(), error);
        return false;
    }
    
    return true;
}

} // namespace

template<>
auto compile<gfx::shader>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);
    std::string str_input = absolute_path.string();
    
    auto extension = output.extension();
    auto renderer = gfx::get_renderer_based_on_filename_extension(extension.string());
    
    fs::error_code err;
    // Use atomic_write_file to handle the temporary file creation and atomic rename
    asset_writer::atomic_write_file(output, [&](const fs::path& temp_output) 
    {
        return compile_shader_to_file(
            absolute_path,
            temp_output,
            renderer
        );
    }, err);
    
    if(err)
    {
        APPLOG_ERROR("Failed compilation of {0} -> {1} with error: {2}", 
            str_input, output.filename().string(), err.message());
        return false;
    }
    
    return true;
}

template<>
auto read_importer<gfx::texture>(asset_manager& am, const fs::path& key) -> std::shared_ptr<asset_importer_meta>
{
    auto absolute = fs::resolve_protocol(key).string();
    asset_meta meta;
    if(load_from_file(absolute, meta))
    {
        if(!meta.importer)
        {
            meta.importer = std::make_shared<texture_importer_meta>();

            meta.uid = am.add_asset_info_for_path(resolve_input_file(key), meta, true);

            fs::error_code err;
            asset_writer::atomic_write_file(absolute, [&](const fs::path& temp) 
            {
                save_to_file(temp.string(), meta);
            }, err);

            return nullptr;
        }
    }

    return meta.importer;
}

template<>
auto compile<gfx::texture>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto base_importer = read_importer<gfx::texture>(am, key);

    if(!base_importer)
    {
        return true;
    }
    auto importer = std::static_pointer_cast<texture_importer_meta>(base_importer);

    auto protocol = fs::extract_protocol(fs::convert_to_protocol(key)).generic_string();
    auto absolute_path = resolve_input_file(key);
    std::string str_input = absolute_path.string();

    fs::error_code err;
    
    asset_writer::atomic_write_file(output, [&](const fs::path& temp_output) 
    {
        return compile_texture_to_file(
            absolute_path, 
            temp_output, 
            *importer, 
            protocol
        );
    }, err);
    
    if(err)
    {
        APPLOG_ERROR("Failed compilation of {0} -> {1} with error: {2}", 
            str_input, output.filename().string(), err.message());
        return false;
    }
    
    return true;
}

template<>
auto compile<material>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);

    std::string str_input = absolute_path.string();

    fs::error_code err;

    std::shared_ptr<material> material;
    {
        load_from_file(str_input, material);

        asset_writer::atomic_write_file(output, [&](const fs::path& temp) 
        {
            save_to_file_bin(temp.string(), material);
        }, err);
    }


    return true;
}

template<>
auto read_importer<mesh>(asset_manager& am, const fs::path& key) -> std::shared_ptr<asset_importer_meta>
{
    auto absolute = fs::resolve_protocol(key).string();
    asset_meta meta;
    if(load_from_file(absolute, meta))
    {
        if(!meta.importer)
        {
            meta.importer = std::make_shared<mesh_importer_meta>();

            meta.uid = am.add_asset_info_for_path(resolve_input_file(key), meta, true);

            fs::error_code err;
            asset_writer::atomic_write_file(absolute, [&](const fs::path& temp) 
            {
                save_to_file(temp.string(), meta);
            }, err);

            return nullptr;
        }
    }

    return meta.importer;
}

template<>
auto compile<mesh>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    // Try to import first.
    auto base_importer = read_importer<mesh>(am, key);

    if(!base_importer)
    {
        return true;
    }

    auto importer = std::static_pointer_cast<mesh_importer_meta>(base_importer);

    auto absolute_path = resolve_input_file(key);

    std::string str_input = absolute_path.string();

    fs::error_code err;

    fs::path file = absolute_path.stem();
    fs::path dir = absolute_path.parent_path();

    mesh::load_data data;
    std::vector<animation_clip> animations;
    std::vector<importer::imported_material> materials;
    std::vector<importer::imported_texture> textures;

    if(!importer::load_mesh_data_from_file(am, absolute_path, *importer, data, animations, materials, textures))
    {
        APPLOG_ERROR("Failed compilation of {0}", str_input);
        return false;
    }
    if(!data.vertex_data.empty())
    {
        asset_writer::atomic_write_file(output, [&](const fs::path& temp) 
        {
            save_to_file_bin(temp.string(), data);
        }, err);
    }

    {
        for(const auto& animation : animations)
        {
           fs::path anim_output;
           if(animation.name.empty())
           {
               anim_output = (dir / file).string() + ".anim";
           }
           else
           {
               anim_output = dir / (animation.name + ".anim");
           }

           asset_writer::atomic_write_file(anim_output, [&](const fs::path& temp) 
           {
                save_to_file(temp.string(), animation);
           }, err);

            // APPLOG_INFO("Successful compilation of animation {0}", animation.name);
        }

        for(const auto& material : materials)
        {
           
            fs::path mat_output;

            if(material.name.empty())
            {
                mat_output = (dir / file).string() + ".mat";
            }
            else
            {
                mat_output = dir / (material.name + ".mat");
            }   

            asset_writer::atomic_write_file(mat_output, [&](const fs::path& temp) 
            {
                save_to_file(temp.string(), material.mat);
            }, err);

            // APPLOG_INFO("Successful compilation of material {0}", material.name);
        }
    }

    return true;
}

template<>
auto read_importer<animation_clip>(asset_manager& am, const fs::path& key) -> std::shared_ptr<asset_importer_meta>
{
    auto absolute = fs::resolve_protocol(key).string();
    asset_meta meta;
    if(load_from_file(absolute, meta))
    {
        if(!meta.importer)
        {
            meta.importer = std::make_shared<animation_importer_meta>();

            meta.uid = am.add_asset_info_for_path(resolve_input_file(key), meta, true);

            fs::error_code err;
            asset_writer::atomic_write_file(absolute, [&](const fs::path& temp) 
            {
                save_to_file(temp.string(), meta);
            }, err);

            return nullptr;
        }
    }

    return meta.importer;
}

template<>
auto compile<animation_clip>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    // Try to import first.
    auto base_importer = read_importer<animation_clip>(am, key);

    if(!base_importer)
    {
        return true;
    }

    auto importer = std::static_pointer_cast<animation_importer_meta>(base_importer);

    auto absolute_path = resolve_input_file(key);

    std::string str_input = absolute_path.string();

    fs::error_code err;

    animation_clip anim;
    {
        load_from_file(str_input, anim);

        anim.root_motion.keep_position_y = importer->root_motion.keep_position_y;
        anim.root_motion.keep_position_xz = importer->root_motion.keep_position_xz;
        anim.root_motion.keep_rotation = importer->root_motion.keep_rotation;
        anim.root_motion.keep_in_place = importer->root_motion.keep_in_place;

        fs::error_code err;
        asset_writer::atomic_write_file(output, [&](const fs::path& temp) 
        {
            save_to_file_bin(temp.string(), anim);
        }, err);
    }

    return true;
}

template<>
auto compile<font>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);
    std::string str_input = absolute_path.string();

    copy_compiled_file(absolute_path, output, str_input);

    return true;
}

template<>
auto compile<prefab>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);
    std::string str_input = absolute_path.string();

    copy_compiled_file(absolute_path, output, str_input);

    return true;
}

template<>
auto compile<scene_prefab>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);
    std::string str_input = absolute_path.string();

    copy_compiled_file(absolute_path, output, str_input);

    return true;
}

template<>
auto compile<physics_material>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);

    std::string str_input = absolute_path.string();

    fs::error_code err;

    auto material = std::make_shared<physics_material>();
    {
        load_from_file(str_input, material);

        asset_writer::atomic_write_file(output, [&](const fs::path& temp) 
        {
            save_to_file_bin(temp.string(), material);
        }, err);
    }

    return true;
}

template<>
auto compile<audio_clip>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);

    std::string str_input = absolute_path.string();

    fs::error_code err;

    audio::sound_data clip;
    {
        std::string error;
        if(!load_from_file(str_input, clip, error))
        {
            APPLOG_ERROR("Failed compilation of {0} with error: {1}", str_input, error);
            return false;
        }

        clip.convert_to_mono();

        asset_writer::atomic_write_file(output, [&](const fs::path& temp) 
        {
            save_to_file_bin(temp.string(), clip);
        }, err);
    }

    return true;
}
// Struct to hold the parsed error details
struct script_compilation_entry
{
    std::string file{}; // Path to the file
    int line{};         // Line number of the error
    std::string msg{};  // Full error line
};

// Function to parse all compilation errors
auto parse_compilation_errors(const std::string& log) -> std::vector<script_compilation_entry>
{
    // Regular expression to extract the warning details
    std::regex warning_regex(R"((.*)\((\d+),\d+\): error .*)");
    std::vector<script_compilation_entry> entries;

    // Use std::sregex_iterator to find all matches
    auto begin = std::sregex_iterator(log.begin(), log.end(), warning_regex);
    auto end = std::sregex_iterator();

    for(auto it = begin; it != end; ++it)
    {
        const std::smatch& match = *it;
        if(match.size() >= 3)
        {
            script_compilation_entry entry;
            entry.file = match[1].str();            // Extract file path
            entry.line = std::stoi(match[2].str()); // Extract line number
            entry.msg = match[0].str();             // Extract full warning line
            entries.emplace_back(std::move(entry));
        }
    }

    return entries;
}

// Function to parse all compilation warnings
auto parse_compilation_warnings(const std::string& log) -> std::vector<script_compilation_entry>
{
    // Regular expression to extract the warning details
    std::regex warning_regex(R"((.*)\((\d+),\d+\): error .*)");
    std::vector<script_compilation_entry> entries;

    // Use std::sregex_iterator to find all matches
    auto begin = std::sregex_iterator(log.begin(), log.end(), warning_regex);
    auto end = std::sregex_iterator();

    for(auto it = begin; it != end; ++it)
    {
        const std::smatch& match = *it;
        if(match.size() >= 3)
        {
            script_compilation_entry entry;
            entry.file = match[1].str();            // Extract file path
            entry.line = std::stoi(match[2].str()); // Extract line number
            entry.msg = match[0].str();             // Extract full warning line
            entries.emplace_back(std::move(entry));
        }

    }

    return entries;
}

template<>
auto compile<script_library>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    bool result = true;
    fs::error_code err;
    fs::path temp = fs::temp_directory_path(err);

    mono::compiler_params params;

    auto protocol = fs::extract_protocol(fs::convert_to_protocol(key)).generic_string();

    if(protocol != "engine")
    {
        auto lib_compiled_key = fs::resolve_protocol(script_system::get_lib_compiled_key("engine"));

        params.references.emplace_back(lib_compiled_key.filename().string());

        params.references_locations.emplace_back(lib_compiled_key.parent_path().string());
    }

    auto assets = am.get_assets<script>(protocol);
    for(const auto& asset : assets)
    {
        if(asset)
        {
            params.files.emplace_back(fs::resolve_protocol(asset.id()).string());
        }
    }

    temp /= script_system::get_lib_name(protocol);

    auto temp_xml = temp;
    temp_xml.replace_extension(".xml");
    auto output_xml = output;
    output_xml.replace_extension(".xml");

    auto temp_mdb = temp;
    temp_mdb.concat(".mdb");
    auto output_mdb = output;
    output_mdb.concat(".mdb");

    std::string str_output = temp.string();

    params.output_name = str_output;
    params.output_doc_name = temp_xml.string();
    if(params.files.empty())
    {
        fs::remove(output, err);
        fs::remove(output_mdb, err);

        if(protocol == "engine")
        {
            return false;
        }

        return result;
    }

    params.debug = flags & script_library::compilation_flags::debug;

    std::string error;
    auto cmd = mono::create_compile_command_detailed(params);

    APPLOG_TRACE("Script Compile : \n {0} {1}", cmd.cmd, cmd.args);

    fs::remove(temp, err);
    fs::remove(temp_mdb, err);
    fs::remove(temp_xml, err);

    if(!run_process(cmd.cmd, cmd.args, true, error))
    {
        auto parsed_errors = parse_compilation_errors(error);

        if(!parsed_errors.empty())
        {
            for(const auto& error : parsed_errors)
            {
                APPLOG_ERROR_LOC(error.file.c_str(), error.line, "", error.msg);
            }
        }
        else
        {
            APPLOG_ERROR("Failed compilation of {0} with error: {1}", output.string(), error);
        }
        result = false;
    }
    else
    {
        if(!params.debug)
        {
            fs::remove(output_mdb, err);
        }

        fs::create_directories(output.parent_path(), err);

        if(protocol != "engine")
        {
            auto parsed_warnings = parse_compilation_warnings(error);

            for(const auto& warning : parsed_warnings)
            {
                APPLOG_WARNING_LOC(warning.file.c_str(), warning.line, "", warning.msg);
            }
        }

        // mono::compile_cmd aot_cmd;
        // aot_cmd.cmd = "mono";
        // aot_cmd.args.emplace_back("--aot=full");
        // aot_cmd.args.emplace_back(temp.string());
        // error = {};
        // bool ok = run_process(aot_cmd.cmd, aot_cmd.args, true, error);

        //APPLOG_INFO("Successful compilation of {0}", fs::replace(output, "temp-", "").string());

        script_system::copy_compiled_lib(temp, output);
    }

    return result;
}

template<>
auto compile<script>(asset_manager& am, const fs::path& key, const fs::path& output, uint32_t flags) -> bool
{
    auto absolute_path = resolve_input_file(key);

    fs::error_code er;
    asset_writer::atomic_copy_file(absolute_path, output, er);

    // APPLOG_INFO("Successful copy to {0}", output.string());

    //script_system::set_needs_recompile(fs::extract_protocol(fs::convert_to_protocol(key)).string());

    return true;
}

} // namespace unravel::asset_compiler

