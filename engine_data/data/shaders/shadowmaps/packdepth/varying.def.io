vec3 a_position  : POSITION;
vec4 a_normal    : NORMAL;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_weight    : BLENDWEIGHT;
vec4 a_indices   : BLENDINDICES;

vec4 v_position    : TEXCOORD0 = vec4(0.0, 0.0, 0.0, 0.0);
vec2 v_texcoord0   : TEXCOORD1 = vec2(0.0, 0.0);

vec4 i_data0     : TEXCOORD31;
vec4 i_data1     : TEXCOORD30;
vec4 i_data2     : TEXCOORD29;
vec4 i_data3     : TEXCOORD28;

float v_depth      : FOG       = 0.0;
