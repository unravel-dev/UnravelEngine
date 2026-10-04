/*
 * The geometry pass's vertex shader for the surface cache card captures: each draw brings its own world -> clip
 * transform (u_card_capture_view_proj) in place of the view's, so one pass draws every capture of a frame, each into
 * its tile of the capture atlas.
 */

#define DEFERRED_GEOM_CARD_CAPTURE 1
#include "deferred_geom/vs_deferred_geom.sc"
