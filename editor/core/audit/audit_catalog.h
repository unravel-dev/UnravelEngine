#pragma once

#include "audit_types.h"

#include <string>
#include <vector>

/// Every rule the audit checks, in one place: ids, titles, texts and thresholds.
namespace unravel::audit
{

/// Rule ids, as reports, configs and tools name them.
namespace rules
{
constexpr const char* MISSING_ASSET = "reference.missing_asset";
constexpr const char* UNREADABLE_DOCUMENT = "document.unreadable";
constexpr const char* INVALID_CONTROLLER = "animation.controller_invalid";
constexpr const char* JOINT_PROBLEM = "physics.joint_problem";
constexpr const char* FEATURE_OFF = "scene.feature_off";
constexpr const char* SCENE_COLOR_OFF = "material.scene_color_off";
constexpr const char* STEREO_SPATIAL = "audio.stereo_spatial";
constexpr const char* CAMERA_DEPTH_RANGE = "scene.camera_depth_range";
constexpr const char* DEPLOY_PROBLEM = "deploy.problem";
constexpr const char* MISSING_SCENE = "content.missing_scene";
constexpr const char* EXCLUDED_BUT_USED = "content.excluded_but_used";
constexpr const char* ORPHAN_RULE = "content.orphan_rule";
constexpr const char* NAMED_FOLDER = "content.named_folder";
constexpr const char* BUILD_GREW = "deploy.build_grew";
constexpr const char* COMPILE_ERROR = "script.compile_error";
constexpr const char* COMPILE_WARNING = "script.compile_warning";
constexpr const char* TEXTURE_UNCOMPRESSED = "texture.uncompressed";
constexpr const char* TEXTURE_OVERSIZED = "texture.oversized";
constexpr const char* TEXTURE_NO_MIPS = "texture.no_mips";
constexpr const char* AUDIO_LARGE_DECODED = "audio.large_decoded";
constexpr const char* AUDIO_UNCOMPRESSED = "audio.uncompressed";
constexpr const char* MESH_NO_LODS = "mesh.no_lods";
constexpr const char* MESH_HEAVY_MORPHS = "mesh.heavy_morphs";
constexpr const char* DUPLICATE_FILES = "asset.duplicate_files";
constexpr const char* COSTLIER_THAN_START = "settings.costlier_than_start";
constexpr const char* TEXTURE_DEFAULT_UNCOMPRESSED = "settings.texture_default_uncompressed";
constexpr const char* AUDIO_DEFAULT_UNCOMPRESSED = "settings.audio_default_uncompressed";
constexpr const char* PHYSICS_RATE = "settings.physics_rate";
constexpr const char* CODE_FIND_ENTITY = "code.find_entity";
constexpr const char* CODE_CAST_ALL = "code.cast_all";
constexpr const char* CODE_LINQ = "code.linq";
constexpr const char* CODE_CLOSURE = "code.closure";
constexpr const char* CODE_BOXING = "code.boxing";
constexpr const char* CODE_STRING = "code.string";
constexpr const char* CODE_ARRAY = "code.array";
constexpr const char* CODE_OBJECT = "code.object";
constexpr const char* CODE_REFLECTION = "code.reflection";
constexpr const char* CODE_LOG = "code.log";
constexpr const char* CODE_EMPTY_UPDATE = "code.empty_update";
constexpr const char* CODE_ALLOCATING_CALL = "code.allocating_call";
constexpr const char* CODE_INSTANTIATE = "code.instantiate";
constexpr const char* CODE_ASSET_LOOKUP = "code.asset_lookup";
constexpr const char* CODE_INTERFACE_ENUMERATION = "code.interface_enumeration";
constexpr const char* CODE_LOCAL_TIME = "code.local_time";
constexpr const char* CODE_GC_COLLECT = "code.gc_collect";
constexpr const char* CODE_BLOCKING_WAIT = "code.blocking_wait";
constexpr const char* CODE_FILE_ACCESS = "code.file_access";
} // namespace rules

/// Threshold names.
namespace thresholds
{
constexpr const char* MIN_BYTES = "min_bytes";
constexpr const char* MAX_SIDE = "max_side";
constexpr const char* MAX_DECODED_BYTES = "max_decoded_bytes";
constexpr const char* TRIANGLES = "triangles";
constexpr const char* MORPH_BYTES = "morph_bytes";
constexpr const char* MORPHED_VERTICES = "morphed_vertices";
constexpr const char* MIN_FILE_BYTES = "min_file_bytes";
constexpr const char* MAX_RATE = "max_rate";
constexpr const char* MIN_GROWTH_BYTES = "min_growth_bytes";
constexpr const char* MAX_FAR_TO_NEAR = "max_far_to_near";
} // namespace thresholds

/// Fix ids: what the editor can change to resolve an issue.
namespace fixes
{
/// Sets the texture's compression on the issue's platforms to Normal.
constexpr const char* COMPRESS_TEXTURE = "texture.compress";
/// Sets the texture's largest size on the issue's platforms to the rule's threshold.
constexpr const char* LIMIT_TEXTURE_SIZE = "texture.limit_size";
constexpr const char* GENERATE_MIPS = "texture.generate_mips";
/// Sets the clip's compression on the issue's platforms to Normal.
constexpr const char* COMPRESS_AUDIO = "audio.compress";
constexpr const char* FORCE_MONO = "audio.force_mono";
constexpr const char* GENERATE_LODS = "mesh.generate_lods";
/// Sets the setting in the issue's profile to the platform's starting value.
constexpr const char* USE_PLATFORM_START = "settings.use_platform_start";
/// Removes the content rule from the issue's profile.
constexpr const char* REMOVE_CONTENT_RULE = "content.remove_rule";
} // namespace fixes

/// Every rule, in the order the editor lists them.
auto get_rules() -> const std::vector<rule>&;

/// The rule with this id, or null.
auto find_rule(const std::string& id) -> const rule*;

/// What a fix does, in a short imperative phrase for a button ("Compress the texture"); empty for an unknown id.
auto get_fix_label(const std::string& fix_id) -> const char*;

} // namespace unravel::audit
