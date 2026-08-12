/*
 * SUN-TIER DEBUG entry point of the light-voxel kernel: identical dispatch shape to
 * cs_gi_light_voxels.sc, but every visited face writes tier-attribution colors
 * (GiDebugSunTierColor) with the 0.5 provenance alpha instead of radiance. Selected by
 * gi_light_voxel_pass while the sun_tiers debug view is active; the volume relights within
 * its usual rotation once the radiance program takes over again.
 */

#define GI_SUN_TIER_DEBUG_VARIANT
#include "gi/gi_light_voxels_kernel.sh"
