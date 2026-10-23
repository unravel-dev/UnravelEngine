vec3 a_position   : POSITION;
vec4 a_normal     : NORMAL;
vec4 a_tangent    : TANGENT;
vec4 a_bitangent  : BITANGENT;
vec2 a_texcoord0  : TEXCOORD0;
vec4 a_weight     : BLENDWEIGHT;
vec4 a_indices    : BLENDINDICES;

vec2 v_texcoord0  : TEXCOORD0 = vec2(0.0, 0.0);
vec3 v_pos        : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
vec3 v_wpos       : TEXCOORD2 = vec3(0.0, 0.0, 0.0);
vec2 v_lod_params : TEXCOORD3 = vec2(0.0, 0.0);


vec4 i_data0      : TEXCOORD31;
vec4 i_data1      : TEXCOORD30;
vec4 i_data2      : TEXCOORD29;
vec4 i_data3      : TEXCOORD28;
vec4 i_data4      : TEXCOORD27;
vec4 i_data5      : TEXCOORD26;
vec4 i_data6      : TEXCOORD25;
vec4 i_data7      : TEXCOORD24;

// A material instance's per-instance values (vs_deferred_geom_instanced_material.sc): one per drawn instance.
flat vec4 v_material0 : TEXCOORD4 = vec4(1.0, 1.0, 1.0, 1.0);
flat vec4 v_material1 : TEXCOORD5 = vec4(0.0, 0.0, 0.0, 0.0);
flat vec4 v_material2 : TEXCOORD6 = vec4(0.0, 0.0, 0.0, 0.0);
flat vec4 v_material3 : TEXCOORD7 = vec4(0.0, 0.0, 0.0, 0.0);


vec3 v_wnormal    : NORMAL    = vec3(0.0, 0.0, 1.0);
vec3 v_wtangent   : TANGENT   = vec3(1.0, 0.0, 0.0);
vec3 v_wbitangent : BITANGENT  = vec3(0.0, 1.0, 0.0);
