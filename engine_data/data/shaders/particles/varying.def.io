vec4 v_color0    : COLOR0    = vec4(1.0, 0.0, 0.0, 1.0);
vec3 v_texcoord0 : TEXCOORD0 = vec3(0.0, 0.0, 0.0);
vec2 v_texcoord1 : TEXCOORD1 = vec2(0.0, 0.0);
vec4 v_particle  : TEXCOORD2 = vec4(0.0, 0.0, 1.0, 0.0);
vec4 v_prev_clip : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 1.0);
vec3 v_light_ambient : TEXCOORD4 = vec3(1.0, 1.0, 1.0);
vec4 v_light_sun     : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 0.0);
vec3 v_sun_local     : TEXCOORD6 = vec3(0.0, 0.0, 1.0);
vec2 v_quad_uv       : TEXCOORD7 = vec2(0.0, 0.0);
vec4 v_curr_clip        : TEXCOORD8 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_prev_static_clip : TEXCOORD9 = vec4(0.0, 0.0, 0.0, 1.0);

vec3 a_position  : POSITION;
vec2 a_texcoord0 : TEXCOORD0;
