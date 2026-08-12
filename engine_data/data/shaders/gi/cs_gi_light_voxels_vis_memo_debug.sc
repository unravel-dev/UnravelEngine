/*
 * VIS-MEMO DEBUG entry point of the light-voxel kernel: identical dispatch shape to
 * cs_gi_light_voxels.sc, but every visited face runs the REAL bounce visibility-memo
 * transaction and writes its outcome as a categorical color (green = hit, red = miss +
 * restamp, blue = generation 0 at the kernel, dark grey = no covering cage) with the 0.5
 * provenance alpha instead of radiance. Selected by gi_light_voxel_pass while the vis_memo
 * debug view is active; the volume relights within its usual rotation once the radiance
 * program takes over again.
 */

#define GI_VIS_MEMO_DEBUG_VARIANT
#include "gi/gi_light_voxels_kernel.sh"
