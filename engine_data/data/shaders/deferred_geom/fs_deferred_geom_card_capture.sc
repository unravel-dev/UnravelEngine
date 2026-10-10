/*
 * The geometry pass's fragment shader for the surface cache card captures: a masked material (alpha_mode::mask) is not
 * alpha-clipped, so its cards cover the whole surface the mesh shows them.
 */

#define DEFERRED_GEOM_CARD_CAPTURE 1
#include "deferred_geom/fs_deferred_geom.sc"
