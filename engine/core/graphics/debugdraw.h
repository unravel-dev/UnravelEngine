#pragma once

#include "graphics.h"
#include "utils/debugdraw/debugdraw.h"

namespace gfx
{
struct dd_raii
{
    /// Draws into a camera view, whose depth is reversed: the depth test is GREATER unless _depthTestLess says.
    dd_raii(uint16_t _viewId, bool _depthTestLess = false, bgfx::Encoder* _encoder = NULL);

    ~dd_raii();

    DebugDrawEncoder encoder;
    bgfx::ViewId view{};
};

// Draws a billboard icon.
//  - dd: the debug draw encoder.
//  - iconTexture: the BGFX texture handle for the icon.
//  - iconCenter: the world-space position where the icon should appear.
//  - cameraPos: the world-space position of the camera.
//  - halfSize: half the size of the quad (icon).
void draw_billboard(DebugDrawEncoder& dd,
                    bgfx::TextureHandle icon_texture,
                    const bx::Vec3& icon_center,
                    const bx::Vec3& camera_pos,
                    const bx::Vec3& camera_look_dir,
                    float half_size);
} // namespace gfx
