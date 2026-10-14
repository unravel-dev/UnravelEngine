$input v_color0, v_texcoord0, v_texcoord1, v_particle, v_curr_clip, v_prev_clip, v_prev_static_clip

/*
 * Motion vectors of nearly opaque particles: texels at least v_texcoord0.z opaque replace the scene's
 * motion with the particle's (velocity_encoding.sh, as velocity/fs_velocity.sc writes it); the rest keep it.
 */

#include <bgfx_shader.sh>
#include "particle_fragment.sh"

vec2 particle_clip_to_uv(vec4 clip)
{
	return clipTransform(clip.xyz / clip.w).xy * 0.5 + 0.5;
}

void main()
{
	vec4 texel = particle_sample_texture(v_texcoord0.xy, v_texcoord1.xy, v_particle.x);
	// Mask textures: the luminance is the opacity.
	float opacity = particle_is_mask_texture() ? dot(texel.rgb, vec3(0.299, 0.587, 0.114)) : texel.a;
	float alpha = particle_erode(opacity, v_particle.w) * v_color0.a;
	if(alpha < v_texcoord0.z)
	{
		discard;
	}
	// Behind either camera plane there is no meaningful previous place: still, as the scene's pass writes it.
	if(v_curr_clip.w <= 0.0 || v_prev_clip.w <= 0.0 || v_prev_static_clip.w <= 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec2 previous_uv = particle_clip_to_uv(v_prev_clip);
	float object_pixels = length((particle_clip_to_uv(v_prev_static_clip) - previous_uv) * u_viewRect.zw);
	gl_FragColor = vec4(particle_clip_to_uv(v_curr_clip) - previous_uv, object_pixels, v_prev_clip.w);
}
