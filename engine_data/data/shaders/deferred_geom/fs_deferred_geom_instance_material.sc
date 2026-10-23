/*
 * The geometry pass's fragment shader for batches whose instances carry per-instance material values: the material
 * constants come per instance from vs_deferred_geom_instanced_material.sc instead of the material uniforms.
 */

#define DEFERRED_GEOM_INSTANCE_MATERIAL 1
#include "deferred_geom/fs_deferred_geom.sc"
