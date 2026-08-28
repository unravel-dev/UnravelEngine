vec3 a_position   : POSITION;
vec2 a_texcoord0  : TEXCOORD0;
vec4 a_weight     : BLENDWEIGHT;
vec4 a_indices    : BLENDINDICES;

vec4 i_data0      : TEXCOORD31;
vec4 i_data1      : TEXCOORD30;
vec4 i_data2      : TEXCOORD29;
vec4 i_data3      : TEXCOORD28;
vec4 i_data4      : TEXCOORD27;
vec4 i_data5      : TEXCOORD26;
vec4 i_data6      : TEXCOORD25;
vec4 i_data7      : TEXCOORD24;

vec2 v_texcoord0  : TEXCOORD0 = vec2(0.0, 0.0);
vec4 v_curr_pos   : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_prev_pos   : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_prev_static_pos : TEXCOORD3 = vec4(0.0, 0.0, 0.0, 0.0);
