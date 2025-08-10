#include "debugdraw.h"

#include "debugdraw.h"
#include <cmath>

namespace gfx
{
dd_raii::dd_raii(uint16_t _viewId, bool _depthTestLess, bgfx::Encoder* _encoder)
{
    view = _viewId;
    encoder.begin(_viewId, _depthTestLess, _encoder);

    encoder.setColor(0x00000000);

    draw_billboard(encoder, BGFX_INVALID_HANDLE, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 1.0f);

    encoder.setColor(0xffffffff);
}

dd_raii::~dd_raii()
{
    encoder.end();
}

void draw_billboard(DebugDrawEncoder& dd,
                    bgfx::TextureHandle icon_texture,
                    const bx::Vec3& icon_center,
                    const bx::Vec3& camera_pos,
                    const bx::Vec3& camera_look_dir,
                    float half_size)
{
    bx::Vec3 toCamera = bx::mul(camera_look_dir, -1.0f);

    // Choose a world-up vector. If the icon-to-camera vector is nearly parallel to the default up, choose a different
    // one.
    bx::Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    if(std::fabs(bx::dot(toCamera, worldUp)) > 0.99f)
    {
        worldUp.x = 1.0f;
        worldUp.y = 0.0f;
        worldUp.z = 0.0f;
    }

    // Compute billboard basis:
    // Right vector = cross(worldUp, toCamera)
    bx::Vec3 right = bx::cross(worldUp, toCamera);
    right = bx::normalize(right);

    // Up vector = cross(toCamera, right)
    bx::Vec3 up = bx::cross(toCamera, right);

    // Build the billboard transform matrix.
    // This matrix is constructed in column-major order:
    //  - first column: scaled right vector (half-size)
    //  - second column: scaled up vector (half-size)
    //  - third column: forward (toCamera) (unchanged, as a direction)
    //  - fourth column: translation (icon_center)
    float mtx[16];
    mtx[0] = up.x * half_size;
    mtx[1] = up.y * half_size;
    mtx[2] = up.z * half_size;
    mtx[3] = 0.0f;
    mtx[4] = -right.x * half_size;
    mtx[5] = -right.y * half_size;
    mtx[6] = -right.z * half_size;
    mtx[7] = 0.0f;
    mtx[8] = toCamera.x;
    mtx[9] = toCamera.y;
    mtx[10] = toCamera.z;
    mtx[11] = 0.0f;
    mtx[12] = icon_center.x;
    mtx[13] = icon_center.y;
    mtx[14] = icon_center.z;
    mtx[15] = 1.0f;

    // Push our billboard transform.
    dd.pushTransform(mtx);

    // Now draw a unit quad (centered at (0,0,0) with a normal along +Z).
    // Because the transform has already positioned, rotated, and scaled the quad,
    // we pass identity parameters to drawQuad.
    dd.drawQuad(icon_texture, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 1.0f);

    // Pop the transform to restore previous state.
    dd.popTransform();
}

} // namespace gfx
