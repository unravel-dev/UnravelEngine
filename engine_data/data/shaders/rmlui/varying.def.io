vec2 a_position  : POSITION;
vec4 a_color0    : COLOR0;
vec2 a_texcoord0 : TEXCOORD0;
vec4 a_texcoord1 : TEXCOORD1;
vec4 a_texcoord2 : TEXCOORD2;
vec4 a_texcoord3 : TEXCOORD3;
vec4 a_texcoord4 : TEXCOORD4;
vec4 a_texcoord5 : TEXCOORD5;
vec4 a_texcoord6 : TEXCOORD6;

vec2 v_texcoord0 : TEXCOORD0 = vec2(0.0, 0.0);
vec4 v_color0    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec4 v_document_position : TEXCOORD1 = vec4(0.0, 0.0, 0.0, 1.0);
vec4 v_edge_distance : TEXCOORD2 = vec4(0.0, 0.0, 0.0, 0.0);
vec4 v_texture_rect : TEXCOORD3 = vec4(0.0, 0.0, 1.0, 1.0);
float v_shape_kind : TEXCOORD4 = 0.0;
