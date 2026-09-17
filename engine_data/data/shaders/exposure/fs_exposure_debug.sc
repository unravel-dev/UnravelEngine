$input v_texcoord0

/*
 * EXPOSURE DEBUG OVERLAY (UE's Visualize HDR, reduced to what this engine's auto exposure
 * actually owns). Drawn over the finished image as a blended panel, so the lit frame stays
 * readable underneath - the pass cannot sample its own output, and a scissored blend needs no
 * copy.
 *
 * Two bands, sharing the panel:
 *
 *   TOP - the exposure trace. One column per frame of the 256-frame history ring, oldest at
 *   the left: log2 adapted exposure (green) against log2 target exposure (yellow). A gap
 *   between them IS the adaptation in flight; the vertical span is fixed at
 *   TRACE_SPAN_STOPS around the current adapted value, so the slope reads as stops per frame.
 *
 *   BOTTOM - this frame's metering histogram on a log2 luminance axis (the same axis the
 *   histogram pass bins on, pre-exposure removed). Bar height is the bin's share of the
 *   metered weight against BAR_FULL_SHARE. The markers are what the average shader decided:
 *   white = the metered log luminance the percentile trim produced, cyan = the neutral point
 *   (EV100 == compensation, where exposure is exactly 1), red = the min / max EV100 clamps.
 *   A white marker sitting on a clamp is the exposure being held, not measured.
 */

#include "../common.sh"

SAMPLER2D(s_exposure, 0);
SAMPLER2D(s_exposure_history, 1);
SAMPLER2D(s_exposure_histogram, 2);

/// xy = panel min in uv, zw = panel size in uv.
uniform vec4 u_exposure_debug_rect;
/// x = min log2 luminance, y = max log2 luminance, z = history ring length,
/// w = the ring's next write slot (so the oldest sample is at w).
uniform vec4 u_exposure_debug_range;
/// x = exposure compensation (EV), y = min EV100, z = max EV100, w = histogram bin count.
uniform vec4 u_exposure_debug_settings;

/// Share of the metered weight a full-height bar stands for.
#define BAR_FULL_SHARE 0.05
/// Half-height of the trace band in stops.
#define TRACE_SPAN_STOPS 6.0
/// Fraction of the panel the trace band takes; the histogram takes the rest.
#define TRACE_BAND 0.45
/// Line half-thickness, in panel heights.
#define LINE_HALF_WIDTH 0.006
#define MARKER_HALF_WIDTH 0.0022
#define PANEL_ALPHA 0.62
/// log2(0.18): EV100 0 sits here on the luminance axis.
#define LOG2_MIDDLE_GREY (-2.4739311883324122)

/// The luminance axis position (0..1 across the panel) of a log2 luminance.
float GetAxisPosition(float log_luminance)
{
	return saturate((log_luminance - u_exposure_debug_range.x) /
	                max(u_exposure_debug_range.y - u_exposure_debug_range.x, 1e-4));
}

void main()
{
	vec2 local = (v_texcoord0 - u_exposure_debug_rect.xy) / max(u_exposure_debug_rect.zw, vec2_splat(1e-6));
	if(any(lessThan(local, vec2_splat(0.0))) || any(greaterThan(local, vec2_splat(1.0))))
	{
		discard;
	}
	vec4 exposure = texture2DLod(s_exposure, vec2_splat(0.5), 0.0);
	float ring_length = max(u_exposure_debug_range.z, 1.0);
	float next_slot = u_exposure_debug_range.w;
	// The newest sample is the slot before the next write; the oldest is the next write itself.
	float newest_slot = mod(next_slot + ring_length - 1.0, ring_length);
	vec4 newest = texture2DLod(s_exposure_history, vec2((newest_slot + 0.5) / ring_length, 0.5), 0.0);

	vec3 color = vec3(0.02, 0.02, 0.03);

	if(local.y < TRACE_BAND)
	{
		// TRACE. Oldest sample at the left edge.
		float band = local.y / TRACE_BAND;
		float step_index = floor(local.x * (ring_length - 1.0) + 0.5);
		float slot = mod(next_slot + step_index, ring_length);
		vec4 sample_frame = texture2DLod(s_exposure_history, vec2((slot + 0.5) / ring_length, 0.5), 0.0);
		float center = newest.x;
		// Stops above the centre are drawn upward, so band 0 is the top of the span.
		float adapted_y = saturate(0.5 - (sample_frame.x - center) / (2.0 * TRACE_SPAN_STOPS));
		float target_y = saturate(0.5 - (sample_frame.y - center) / (2.0 * TRACE_SPAN_STOPS));
		if(abs(band - 0.5) < MARKER_HALF_WIDTH * 2.0)
		{
			// The centre line: the current adapted exposure.
			color = vec3(0.18, 0.18, 0.20);
		}
		if(abs(band - target_y) < LINE_HALF_WIDTH)
		{
			color = vec3(0.95, 0.85, 0.15);
		}
		if(abs(band - adapted_y) < LINE_HALF_WIDTH)
		{
			color = vec3(0.20, 0.95, 0.35);
		}
	}
	else
	{
		// HISTOGRAM. Bin 0 is the black bucket and is never plotted: the metering drops it.
		float band = (local.y - TRACE_BAND) / (1.0 - TRACE_BAND);
		float height = 1.0 - band;
		float bins = max(u_exposure_debug_settings.w, 2.0);
		float bin = 1.0 + local.x * (bins - 2.0);
		float share = texture2DLod(s_exposure_histogram, vec2((bin + 0.5) / bins, 0.5), 0.0).x;
		if(height <= saturate(share / BAR_FULL_SHARE))
		{
			color = vec3(0.35, 0.45, 0.60);
		}
		// The metered luminance the trim produced, this frame's measurement.
		float metered = GetAxisPosition(newest.z);
		// The neutral point and the two EV100 clamps, in luminance.
		float neutral = GetAxisPosition(LOG2_MIDDLE_GREY + u_exposure_debug_settings.x);
		float min_clamp = GetAxisPosition(LOG2_MIDDLE_GREY + u_exposure_debug_settings.y);
		float max_clamp = GetAxisPosition(LOG2_MIDDLE_GREY + u_exposure_debug_settings.z);
		if(abs(local.x - min_clamp) < MARKER_HALF_WIDTH || abs(local.x - max_clamp) < MARKER_HALF_WIDTH)
		{
			color = vec3(0.85, 0.15, 0.10);
		}
		if(abs(local.x - neutral) < MARKER_HALF_WIDTH)
		{
			color = vec3(0.15, 0.80, 0.90);
		}
		if(abs(local.x - metered) < MARKER_HALF_WIDTH * 1.5)
		{
			color = vec3_splat(1.0);
		}
	}
	// A one-pixel-ish frame so the panel reads as an overlay, not as image content.
	if(local.x < 0.002 || local.x > 0.998 || local.y < 0.003 || local.y > 0.997)
	{
		color = vec3(0.45, 0.45, 0.50);
	}
	gl_FragColor = vec4(color, PANEL_ALPHA);
}
