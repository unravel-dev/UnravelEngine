#ifndef FOG_SH_HEADER_GUARD
#define FOG_SH_HEADER_GUARD

/*
 * Exponential height fog (engine/engine/rendering/pipeline/passes/fog_pass.h). The full-screen composite
 * (fog_composite.sh) fogs the lit scene; the particles (particles/particle_fragment.sh) fog themselves with the same
 * model and uniforms; the froxel passes (cs_fog_lighting.sc, cs_fog_integrate.sc) light and integrate the
 * volumetric layer near the camera.
 *
 * The extinction is sigma0 at and below the base height and sigma0 * exp(-(h - base) / H) above it. Its integral
 * along a ray has a closed form; the analytic in-scatter is exact for that medium when the light it scatters is the
 * same along the ray, which holds for the unshadowed sun (one scattering angle per ray) and for the sky SH. The
 * froxel column covers view depths up to u_fog_volume.x with shadowed light, and the analytic fog continues past it
 * from the same density. Fog travels as a premultiplied pair: scene * transmittance + in-scatter.
 *
 * Fog volumes (u_fog_volumes, fog_volume_component) add boxes and ellipsoids of fog of their own color. The froxels
 * sample them per froxel (fog_volume_profile); the analytic fog integrates each volume's chord along the ray
 * (fog_volume_chord_depth) and inserts it at its entry, in front of the height fog behind it. Both read the same
 * edge profile, so a volume looks the same on either side of the froxel column's end.
 *
 * An includer that reads the froxel column declares its sampler3D and defines FOG_VOLUME_SAMPLER as its name before
 * including this header.
 */

/// x = extinction at and below the base height (1/m), y = base height (m), z = 1 / scale height (1/m),
/// w = the least transmittance the fog leaves (1 - max opacity). x = 0: no fog.
uniform vec4 u_fog_density;
/// x = start distance (m), y = sky distance (m), z = sky affect, w = phase anisotropy g.
uniform vec4 u_fog_range;
/// rgb = sun illuminance x sun scattering (lux, linear, absolute), w = ambient scattering (the scale on the sky
/// radiance): the light a unit of scattering receives, before any albedo.
uniform vec4 u_fog_light;
/// xyz = direction toward the sun.
uniform vec4 u_fog_sun_direction;
/// rgb = the height fog's albedo (linear).
uniform vec4 u_fog_albedo;
/// xyz = camera world position.
uniform vec4 u_fog_camera;
/// xyz = the camera's forward axis (world, unit): a camera-relative offset's view depth is its dot product with it.
uniform vec4 u_fog_forward;
/// The froxel column: x = view depth it covers (m; 0 = no volumetric fog), y = slice distribution constant c,
/// z = 1 / log(c x + 1), w = slices.
uniform vec4 u_fog_volume;

#define FOG_PI 3.14159265
/// Below this height difference (m) a ray counts as horizontal.
#define FOG_HORIZONTAL_HEIGHT_EPSILON 1e-4
/// Below this exponent (1 - exp(-u)) / u takes its series.
#define FOG_SERIES_THRESHOLD 1e-2
/// Largest finite value of the half-float froxel volumes.
#define FOG_HALF_MAX 65504.0

/// Fog volumes of u_fog_volumes (fog_pass::max_fog_volumes) and the vec4s each takes.
#define FOG_MAX_VOLUMES 16
#define FOG_VOLUME_VEC4S 5
/// x = fog volumes in u_fog_volumes.
uniform vec4 u_fog_volume_info;
/// Per volume, the farthest from the camera first: three rows of the camera-relative world to unit-shape matrix
/// (unit = row . (offset, 1): the box spans -1..1, the ellipsoid is the unit sphere), (extinction (1/m), 1 / edge fade
/// band, shape (0 box, 1 ellipsoid) + 2 when it subtracts, 1 / noise size (1/m)), (albedo, noise strength).
uniform vec4 u_fog_volumes[80];

bool fog_is_enabled()
{
	return u_fog_density.x > 0.0;
}

bool fog_is_volumetric()
{
	return u_fog_volume.x > 0.0;
}

/// (1 - exp(-u)) / u for u >= 0, without the cancellation near 0.
float fog_exp_average(float u)
{
	if(u < FOG_SERIES_THRESHOLD)
	{
		return 1.0 - u * 0.5 + u * u * (1.0 / 6.0);
	}
	return (1.0 - exp(-u)) / u;
}

/**
 * The optical depth of the fog along a segment of @p segment_length metres starting at height @p start_height, whose
 * direction rises @p direction_y per metre. The segment splits into its part below the base height (constant
 * density) and its part above; the exponential part is measured from its lower end, so every exponent stays at
 * or below 0 (no overflow on long rays, no 0 / 0 on horizontal ones).
 */
float fog_optical_depth(float start_height, float direction_y, float segment_length)
{
	float base_height = u_fog_density.y;
	float inverse_scale_height = u_fog_density.z;
	float end_height = start_height + direction_y * segment_length;
	float low = min(start_height, end_height);
	float high = max(start_height, end_height);
	float span = high - low;
	float constant_length;
	if(span > FOG_HORIZONTAL_HEIGHT_EPSILON)
	{
		constant_length = segment_length * clamp((base_height - low) / span, 0.0, 1.0);
	}
	else
	{
		constant_length = low <= base_height ? segment_length : 0.0;
	}
	float falloff_length = segment_length - constant_length;
	float falloff_low = max(low, base_height);
	float falloff_exponent = inverse_scale_height * (high - falloff_low);
	float falloff_depth = exp(-inverse_scale_height * (falloff_low - base_height)) * falloff_length * fog_exp_average(max(falloff_exponent, 0.0));
	return u_fog_density.x * (constant_length + falloff_depth);
}

/// The extinction (1/m) at world height @p height.
float fog_extinction_at(float height)
{
	return u_fog_density.x * exp(-u_fog_density.z * max(height - u_fog_density.y, 0.0));
}

/// Henyey-Greenstein phase function (1/sr) of anisotropy @p g at the cosine @p cos_theta between the light's
/// direction of travel and the scattered light's.
float fog_phase_hg(float cos_theta, float g)
{
	float g2 = g * g;
	float denominator = max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4);
	return (1.0 - g2) / (4.0 * FOG_PI * denominator * sqrt(denominator));
}

/**
 * The sky radiance the fog scatters toward the camera along @p view_direction: the environment SH (texels 0..8 of
 * @p environment, radiance, lighting.sh's basis) convolved with the Henyey-Greenstein phase. The phase's Legendre
 * coefficients are g^l (Funk-Hecke), so band l scales by g^l.
 */
vec3 fog_sky_inscatter(sampler2D environment, vec3 view_direction)
{
	float g = u_fog_range.w;
	float x = view_direction.x;
	float y = view_direction.y;
	float z = view_direction.z;
	vec3 band0 = texelFetch(environment, ivec2(0, 0), 0).rgb * 0.282095;
	vec3 band1 = texelFetch(environment, ivec2(1, 0), 0).rgb * (0.488603 * y) +
	             texelFetch(environment, ivec2(2, 0), 0).rgb * (0.488603 * z) +
	             texelFetch(environment, ivec2(3, 0), 0).rgb * (0.488603 * x);
	vec3 band2 = texelFetch(environment, ivec2(4, 0), 0).rgb * (1.092548 * x * z) +
	             texelFetch(environment, ivec2(5, 0), 0).rgb * (1.092548 * y * z) +
	             texelFetch(environment, ivec2(6, 0), 0).rgb * (1.092548 * x * y) +
	             texelFetch(environment, ivec2(7, 0), 0).rgb * (0.315392 * (3.0 * z * z - 1.0)) +
	             texelFetch(environment, ivec2(8, 0), 0).rgb * (0.546274 * (x * x - y * y));
	return max(band0 + band1 * g + band2 * (g * g), vec3_splat(0.0));
}

/// The light a unit of scattering sends toward the camera along @p view_direction from the unshadowed sun and the sky,
/// before any albedo.
vec3 fog_unit_light(sampler2D environment, vec3 view_direction)
{
	float sun_phase = fog_phase_hg(dot(view_direction, u_fog_sun_direction.xyz), u_fog_range.w);
	return u_fog_light.rgb * sun_phase + u_fog_light.w * fog_sky_inscatter(environment, view_direction);
}

/// The view depth (m) at slice coordinate @p coordinate (0 at the camera, 1 at the far end of the froxel column):
/// z = ((c d + 1)^e - 1) / c, slices packed toward the camera by c.
float fog_slice_depth(float coordinate)
{
	return (exp(coordinate / u_fog_volume.z) - 1.0) / u_fog_volume.y;
}

/// The slice coordinate (0..1 inside the column) of view depth @p view_depth: the inverse of fog_slice_depth.
float fog_slice_coordinate(float view_depth)
{
	return log(u_fog_volume.y * max(view_depth, 0.0) + 1.0) * u_fog_volume.z;
}

/// @p near seen in front of @p far: both premultiplied pairs.
vec4 fog_compose(vec4 near, vec4 far)
{
	return vec4(near.rgb + near.a * far.rgb, near.a * far.a);
}

/**
 * The height fog alone along the unit @p view_direction from @p begin to @p end metres from the camera (from the start
 * distance at the nearest), scattering @p unit_light (fog_unit_light): rgb = in-scatter times @p pre_exposure,
 * a = transmittance.
 */
vec4 fog_height_segment(vec3 view_direction, vec3 unit_light, float begin, float end, float pre_exposure)
{
	float first = max(begin, u_fog_range.x);
	float segment_length = end - first;
	if(segment_length <= 0.0)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	float start_height = u_fog_camera.y + view_direction.y * first;
	float transmittance = exp(-fog_optical_depth(start_height, view_direction.y, segment_length));
	return vec4(u_fog_albedo.rgb * unit_light * ((1.0 - transmittance) * pre_exposure), transmittance);
}

/// Fog volume @p index's position in its unit shape, at the camera-relative @p offset.
vec3 fog_volume_unit_position(int index, vec3 offset)
{
	int base = index * FOG_VOLUME_VEC4S;
	vec4 offset_point = vec4(offset, 1.0);
	return vec3(dot(u_fog_volumes[base], offset_point), dot(u_fog_volumes[base + 1], offset_point), dot(u_fog_volumes[base + 2], offset_point));
}

bool fog_volume_is_ellipsoid(vec4 params)
{
	return mod(params.z, 2.0) > 0.5;
}

bool fog_volume_subtracts(vec4 params)
{
	return params.z > 1.5;
}

/**
 * Fog volume density profile at @p unit (in its unit shape) of shape and fade @p params: 1 deep inside, falling to 0
 * at the surface across the fade band. A box fades with the distance to its nearest face; an ellipsoid with
 * 1 - |unit|^2, which its chord integral (fog_volume_chord_depth) has in closed form.
 */
float fog_volume_profile(vec4 params, vec3 unit)
{
	vec3 distance_to_faces = abs(unit);
	float inside = fog_volume_is_ellipsoid(params) ? dot(unit, unit) : max(distance_to_faces.x, max(distance_to_faces.y, distance_to_faces.z));
	return clamp((1.0 - inside) * params.y, 0.0, 1.0);
}

/// The integral of (1 - q(t)) * fade over @p t, q(t) = a t^2 + b t + c: the ellipsoid profile's fade band.
float fog_ellipsoid_band_integral(vec3 q, float fade, float t)
{
	return (t - (q.x * t * t * t / 3.0 + q.y * t * t * 0.5 + q.z * t)) * fade;
}

/// The roots of q(t) = level, q = (a, b, c); x > y when there are none.
vec2 fog_quadratic_roots(vec3 q, float level)
{
	float discriminant = q.y * q.y - 4.0 * q.x * (q.z - level);
	if(discriminant <= 0.0 || q.x <= 0.0)
	{
		return vec2(1.0, 0.0);
	}
	float root = sqrt(discriminant);
	return vec2(-q.y - root, -q.y + root) / (2.0 * q.x);
}

/**
 * The integral of fog volume @p index's density profile along the unit @p view_direction from @p begin to @p end
 * metres, and in @p entry where the ray enters it (inside that range). An ellipsoid's is exact: |unit|^2 is a
 * quadratic in the distance, so the fade band integrates as a cubic and the core (profile 1) as its length. A box's
 * profile is piecewise linear along the chord; four Gauss-Legendre points integrate it closely.
 */
float fog_volume_chord_depth(int index, vec3 view_direction, float begin, float end, out float entry)
{
	int base = index * FOG_VOLUME_VEC4S;
	vec4 params = u_fog_volumes[base + 3];
	// The camera is the offset's origin.
	vec3 origin = vec3(u_fog_volumes[base].w, u_fog_volumes[base + 1].w, u_fog_volumes[base + 2].w);
	vec3 direction = vec3(dot(u_fog_volumes[base].xyz, view_direction), dot(u_fog_volumes[base + 1].xyz, view_direction), dot(u_fog_volumes[base + 2].xyz, view_direction));
	entry = end;
	if(fog_volume_is_ellipsoid(params))
	{
		vec3 q = vec3(dot(direction, direction), 2.0 * dot(origin, direction), dot(origin, origin));
		vec2 chord = fog_quadratic_roots(q, 1.0);
		float first = max(chord.x, begin);
		float last = min(chord.y, end);
		if(last <= first)
		{
			return 0.0;
		}
		entry = first;
		float depth = fog_ellipsoid_band_integral(q, params.y, last) - fog_ellipsoid_band_integral(q, params.y, first);
		// Inside the core the band's (1 - q) * fade passes 1: the profile is 1 there.
		vec2 core = fog_quadratic_roots(q, 1.0 - 1.0 / params.y);
		float core_first = max(core.x, first);
		float core_last = min(core.y, last);
		if(core_last > core_first)
		{
			depth += (core_last - core_first) - (fog_ellipsoid_band_integral(q, params.y, core_last) - fog_ellipsoid_band_integral(q, params.y, core_first));
		}
		return depth;
	}
	// A ray parallel to a pair of faces divides by a tiny value instead of 0: the planes land far away.
	vec3 safe_direction = mix(direction, vec3_splat(1e-9), vec3(lessThan(abs(direction), vec3_splat(1e-9))));
	vec3 inverse_direction = 1.0 / safe_direction;
	vec3 near_planes = (-1.0 - origin) * inverse_direction;
	vec3 far_planes = (1.0 - origin) * inverse_direction;
	vec3 enters = min(near_planes, far_planes);
	vec3 exits = max(near_planes, far_planes);
	float first = max(max(enters.x, max(enters.y, enters.z)), begin);
	float last = min(min(exits.x, min(exits.y, exits.z)), end);
	if(last <= first)
	{
		return 0.0;
	}
	entry = first;
	// Gauss-Legendre nodes and weights on [-1, 1].
	vec4 nodes = vec4(-0.861136, -0.339981, 0.339981, 0.861136);
	vec4 weights = vec4(0.347855, 0.652145, 0.652145, 0.347855);
	float half_length = 0.5 * (last - first);
	float middle = 0.5 * (last + first);
	float depth = 0.0;
	for(int i = 0; i < 4; ++i)
	{
		depth += weights[i] * fog_volume_profile(params, origin + direction * (middle + nodes[i] * half_length));
	}
	return depth * half_length;
}

/**
 * The analytic fog along the unit @p view_direction from @p begin to @p end metres from the camera (from the start
 * distance at the nearest): the height fog with the adding fog volumes inserted where the ray enters them, farthest
 * first (subtracting volumes carve only the volumetric fog). rgb = in-scatter times @p pre_exposure, a =
 * transmittance, before the max opacity and the sky affect (fog_finish).
 */
vec4 fog_analytic(sampler2D environment, vec3 view_direction, float begin, float end, float pre_exposure)
{
	if(end <= begin)
	{
		return vec4(0.0, 0.0, 0.0, 1.0);
	}
	vec3 unit_light = fog_unit_light(environment, view_direction);
	vec4 fog = fog_height_segment(view_direction, unit_light, begin, end, pre_exposure);
	int count = int(u_fog_volume_info.x);
	for(int i = 0; i < FOG_MAX_VOLUMES; ++i)
	{
		if(i >= count)
		{
			break;
		}
		vec4 params = u_fog_volumes[i * FOG_VOLUME_VEC4S + 3];
		if(fog_volume_subtracts(params))
		{
			continue;
		}
		float entry;
		float depth = fog_volume_chord_depth(i, view_direction, max(begin, u_fog_range.x), end, entry);
		if(depth <= 0.0)
		{
			continue;
		}
		vec4 front = fog_height_segment(view_direction, unit_light, begin, entry, pre_exposure);
		if(front.a < 1e-4)
		{
			continue;
		}
		float volume_transmittance = exp(-params.x * depth);
		vec3 albedo = u_fog_volumes[i * FOG_VOLUME_VEC4S + 4].rgb;
		vec4 volume = vec4(albedo * unit_light * ((1.0 - volume_transmittance) * pre_exposure), volume_transmittance);
		// What lies behind the entry: the fog with the height fog in front of the entry taken back out.
		vec4 behind = vec4((fog.rgb - front.rgb) / front.a, fog.a / front.a);
		fog = fog_compose(front, fog_compose(volume, behind));
	}
	return fog;
}

/// The fog with its transmittance floor (max opacity: the in-scatter scales with the opacity it keeps) and
/// @p sky_affect applied.
vec4 fog_finish(vec4 fog, float sky_affect)
{
	float floor_transmittance = u_fog_density.w;
	if(fog.a < floor_transmittance)
	{
		fog.rgb *= (1.0 - floor_transmittance) / max(1.0 - fog.a, 1e-6);
		fog.a = floor_transmittance;
	}
	return vec4(fog.rgb * sky_affect, 1.0 - (1.0 - fog.a) * sky_affect);
}

#ifdef FOG_VOLUME_SAMPLER
/**
 * The integrated froxel column at screen @p uv from the camera to @p view_depth (inside the column): rgb = pre-exposed
 * in-scatter, a = transmittance. Texel k holds the column to the far side of slice k, so depth d samples between the
 * texels around slice coordinate(d) x slices - 1; inside the first slice the first texel scales toward the camera.
 */
vec4 fog_volume_sample(vec2 uv, float view_depth)
{
	float slices = u_fog_volume.w;
	float passed = fog_slice_coordinate(view_depth) * slices;
	float w = clamp((passed - 0.5) / slices, 0.5 / slices, 1.0 - 0.5 / slices);
	vec4 column = texture3DLod(FOG_VOLUME_SAMPLER, vec3(uv, w), 0.0);
	float first_slice_share = clamp(passed, 0.0, 1.0);
	return vec4(column.rgb * first_slice_share, mix(1.0, column.a, first_slice_share));
}
#endif // FOG_VOLUME_SAMPLER

/**
 * The fog between the camera and a point @p point_distance metres along the unit @p view_direction, seen at screen
 * @p uv: the froxel column up to the point (when the includer reads it and the volumetric fog is on), then the
 * analytic fog past the column. @p is_sky: the point is the sky, fogged at the sky distance and by the sky affect.
 * rgb = in-scatter scaled by @p pre_exposure, a = transmittance.
 */
vec4 fog_to_point(sampler2D environment, vec2 uv, vec3 view_direction, float point_distance, bool is_sky, float pre_exposure)
{
	float end = is_sky ? max(u_fog_range.y, u_fog_range.x) : point_distance;
	float sky_affect = is_sky ? u_fog_range.z : 1.0;
#ifdef FOG_VOLUME_SAMPLER
	if(fog_is_volumetric())
	{
		float depth_per_metre = max(dot(view_direction, u_fog_forward.xyz), 1e-4);
		float column_end = u_fog_volume.x / depth_per_metre;
		vec4 near = fog_volume_sample(uv, min(end, column_end) * depth_per_metre);
		vec4 far = fog_analytic(environment, view_direction, column_end, end, pre_exposure);
		return fog_finish(fog_compose(near, far), sky_affect);
	}
#endif // FOG_VOLUME_SAMPLER
	return fog_finish(fog_analytic(environment, view_direction, 0.0, end, pre_exposure), sky_affect);
}

#endif // FOG_SH_HEADER_GUARD
