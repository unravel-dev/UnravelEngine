vec3 a_position   : POSITION;
vec2 a_texcoord0  : TEXCOORD0;
vec4 a_weight     : BLENDWEIGHT;
vec4 a_indices    : BLENDINDICES;

vec2 v_texcoord0  : TEXCOORD0 = vec2(0.0, 0.0);
vec4 v_curr_pos   : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_prev_pos   : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_prev_static_pos : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 0.0);
