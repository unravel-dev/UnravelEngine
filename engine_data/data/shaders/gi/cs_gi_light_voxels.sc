/*
 * RADIANCE entry point of the light-voxel kernel. The whole body lives in
 * gi_light_voxels_kernel.sh, compiled twice: this program writes lighting, its sibling
 * cs_gi_light_voxels_debug.sc writes sun-tier attribution colors. The C++ pass
 * (gi_light_voxel_pass) selects which PROGRAM to dispatch - a compile-time variant rather
 * than a runtime flag, for reasons documented at the variant switch in the kernel.
 */

#include "gi/gi_light_voxels_kernel.sh"
