/*
 * Copyright 2011-2016 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */
 #ifndef COMMON_H_HEADER_GUARD
 #define COMMON_H_HEADER_GUARD

#include <bgfx_shader.sh>

/// The draw's world matrix (gfx::set_world_transform). Skinned draws read their bones from the palettes of
/// skinning_palette.sh.
uniform mat4  u_world[1];

#include "shaderlib.sh"

#endif // COMMON_H_HEADER_GUARD
