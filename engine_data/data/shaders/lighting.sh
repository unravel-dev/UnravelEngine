#ifndef __LIGHTING_SH__
#define __LIGHTING_SH__

#include <bgfx_shader.sh>


struct GBufferDataColorAndAO
{
    vec3 base_color;
    float ambient_occlusion;
};

struct GBufferDataNormalMetalRoughness
{
    vec3 world_normal;
    float metalness;
    float roughness;
};

struct GBufferDataEmissive
{
    vec3 emissive_color;
};

struct GBufferDataSubsurface
{
    vec3 subsurface_color;
    float subsurface_opacity;
};

struct GBufferDataDepth
{
    float depth;
    float depth01;
};

struct GBufferData
{
    vec3 base_color;
    float ambient_occlusion;

    vec3 world_normal;
    float roughness;
    vec3 emissive_color;
    float metalness;
    vec3 subsurface_color;
    float subsurface_opacity;
    float depth;
    float depth01;

    vec3 diffuse_color;
    vec3 specular_color;
};

float DielectricSpecularToF0(float Specular)
{
    return float(0.08f * Specular);
}

vec3 ComputeF0(float Specular, vec3 BaseColor, float Metallic)
{
    return mix(vec3_splat(DielectricSpecularToF0(Specular)), BaseColor, Metallic);
}

vec3 ComputeF90(vec3 F0, vec3 EdgeColor, float Metallic)
{
    return mix(vec3_splat(1.0), EdgeColor, Metallic);
}

vec3 ComputeDiffuseColor(vec3 BaseColor, float Metallic)
{
    return BaseColor - BaseColor * Metallic;
}

void EncodeGBuffer(in GBufferData data, inout vec4 result[4])
{
    result[0] = vec4(data.base_color, data.ambient_occlusion);
    result[1] = vec4(encodeNormalOctahedron(data.world_normal), data.metalness, data.roughness);
    result[2] = vec4(data.emissive_color, 0.0f);
    result[3] = vec4(data.subsurface_color, data.subsurface_opacity);
}


GBufferDataColorAndAO DecodeGBufferColorAndAO(
    vec2 texcoord,
    sampler2D colorTex)
{
    GBufferDataColorAndAO data;
    vec4 data0 = texture2D(colorTex, texcoord);   // base_color, ao

    data.base_color = data0.xyz;
    data.ambient_occlusion = data0.w;

    return data;
}

GBufferDataColorAndAO DecodeGBufferColorAndAOLod(
    vec2 texcoord,
    sampler2D colorTex,
    float lod)
{
    GBufferDataColorAndAO data;
    vec4 data0 = texture2DLod(colorTex, texcoord, lod);   // base_color, ao

    data.base_color = data0.xyz;
    data.ambient_occlusion = data0.w;

    return data;
}

GBufferDataNormalMetalRoughness DecodeGBufferNormalMetalRoughness(
    vec2 texcoord,
    sampler2D normalMetalRoughnessTex)
{
    GBufferDataNormalMetalRoughness data;
    vec4 data1 = texture2D(normalMetalRoughnessTex, texcoord);   // normal, metalness, roughness

    data.world_normal = decodeNormalOctahedron(data1.xy);
    data.metalness = data1.z;
    data.roughness = data1.w;

    return data;
}

GBufferDataNormalMetalRoughness DecodeGBufferNormalMetalRoughnessLod(
    vec2 texcoord,
    sampler2D normalMetalRoughnessTex,
    float lod)
{
    GBufferDataNormalMetalRoughness data;
    vec4 data1 = texture2DLod(normalMetalRoughnessTex, texcoord, lod);   // normal, metalness, roughness

    data.world_normal = decodeNormalOctahedron(data1.xy);
    data.metalness = data1.z;
    data.roughness = data1.w;

    return data;
}

GBufferDataEmissive DecodeGBufferEmissive(
    vec2 texcoord,
    sampler2D emissiveTex)
{
    GBufferDataEmissive data;
    vec4 data2 = texture2D(emissiveTex, texcoord);   // emissive_color

    data.emissive_color = data2.xyz;

    return data;
}

GBufferDataSubsurface DecodeGBufferSubsurface(
    vec2 texcoord,
    sampler2D subsurfaceTex)
{
    GBufferDataSubsurface data;
    vec4 data3 = texture2D(subsurfaceTex, texcoord);   // subsurface_color, subsurface_opacity

    data.subsurface_color = data3.xyz;
    data.subsurface_opacity = data3.w;

    return data;
}

GBufferDataDepth DecodeGBufferDepth(
    vec2 texcoord,
    sampler2D depthTex) 
{
    GBufferDataDepth data;
    float deviceDepth = texture2D(depthTex, texcoord).x;

    data.depth = toClipSpaceDepth(deviceDepth);
    data.depth01 = deviceDepth;

    return data;
}

GBufferDataDepth DecodeGBufferDepthLod(
    vec2 texcoord,
    sampler2D depthTex,
    float lod) 
{
    GBufferDataDepth data;
    float deviceDepth = texture2DLod(depthTex, texcoord, lod).x;

    data.depth = toClipSpaceDepth(deviceDepth);
    data.depth01 = deviceDepth;

    return data;
}

GBufferData DecodeGBuffer(vec2 texcoord, sampler2D tex0, sampler2D tex1, sampler2D tex2, sampler2D tex3, sampler2D tex4)
{
    GBufferData data;

    GBufferDataColorAndAO data0 = DecodeGBufferColorAndAO(texcoord, tex0);
    GBufferDataNormalMetalRoughness data1 = DecodeGBufferNormalMetalRoughness(texcoord, tex1);
    GBufferDataEmissive data2 = DecodeGBufferEmissive(texcoord, tex2);
    GBufferDataSubsurface data3 = DecodeGBufferSubsurface(texcoord, tex3);
    GBufferDataDepth data4 = DecodeGBufferDepth(texcoord, tex4);

    data.base_color = data0.base_color;    
    data.ambient_occlusion = data0.ambient_occlusion;
    data.world_normal = data1.world_normal;
    data.metalness = data1.metalness;
    data.roughness = data1.roughness;
    data.emissive_color = data2.emissive_color;
    data.subsurface_color = data3.subsurface_color;
    data.subsurface_opacity = data3.subsurface_opacity;
    data.depth01 = data4.depth01;
    data.depth = data4.depth;
    data.diffuse_color = ComputeDiffuseColor(data.base_color, data.metalness);
    data.specular_color = ComputeF0(0.5f, data.base_color, data.metalness);
    return data;
}

/**
 * Simplified GBuffer decode for SSR and similar passes.
 * Only uses color, normal, and depth samplers.
 *
 * @param texcoord   Texture coordinates
 * @param colorTex   GBuffer color/ao texture (tex0)
 * @param normalTex  GBuffer normal/metalness/roughness texture (tex1)
 * @param depthTex   GBuffer depth texture (tex4)
 * @return           GBufferData with only fields that can be filled from these samplers
 */
GBufferData DecodeGBufferSimple(
    vec2 texcoord,
    sampler2D tex0,
    sampler2D tex1,
    sampler2D tex4) // depth texture    
{
    GBufferData data;
    GBufferDataColorAndAO data0 = DecodeGBufferColorAndAO(texcoord, tex0);
    GBufferDataNormalMetalRoughness data1 = DecodeGBufferNormalMetalRoughness(texcoord, tex1);
    GBufferDataDepth data4 = DecodeGBufferDepth(texcoord, tex4);

    data.base_color = data0.base_color;
    data.ambient_occlusion = data0.ambient_occlusion;
    data.world_normal = data1.world_normal;
    data.metalness = data1.metalness;
    data.roughness = data1.roughness;
    data.depth01 = data4.depth01;
    data.depth = data4.depth;
    data.diffuse_color = ComputeDiffuseColor(data.base_color, data.metalness);
    data.specular_color = ComputeF0(0.5f, data.base_color, data.metalness);
    // The following fields are not filled: emissive_color, subsurface_color, subsurface_opacity
    return data;
}



float BiasedNDotL(float NDotLWithoutSaturate )
{
    return saturate(NDotLWithoutSaturate * 1.08f - 0.08f);
}

float Square( float x )
{
    return x*x;
}

vec2 Square( vec2 x )
{
    return x*x;
}

vec3 Square( vec3 x )
{
    return x*x;
}

vec4 Square( vec4 x )
{
    return x*x;
}

float Pow5( float x )
{
    float xx = x*x;
    return xx * xx * x;
}

vec2 Pow5( vec2 x )
{
    vec2 xx = x*x;
    return xx * xx * x;
}

vec3 Pow5( vec3 x )
{
    vec3 xx = x*x;
    return xx * xx * x;
}

vec4 Pow5( vec4 x )
{
    vec4 xx = x*x;
    return xx * xx * x;
}

float UnClampedPow(float X, float Y)
{
    return pow(X, Y);
}

float ClampedPow(float X,float Y)
{
    return pow(max(abs(X),0.000001f),Y);
}
float PhongShadingPow(float X, float Y)
{
    // The following clamping is done to prevent NaN being the result of the specular power computation.
    // Clamping has a minor performance cost.

    // In HLSL pow(a, b) is implemented as exp2(log2(a) * b).

    // For a=0 this becomes exp2(-inf * 0) = exp2(NaN) = NaN.

    // In order to avoid platform differences and rarely occuring image atrifacts we clamp the base.

    // Note: Clamping the exponent seemed to fix the issue mentioned TTP but we decided to fix the root and accept the
    // minor performance cost.

    return ClampedPow(X, Y);
}

float RadialAttenuation(vec3 WorldLightVector, float FalloffExponent)
{
    float NormalizeDistanceSquared = dot(WorldLightVector, WorldLightVector);

#if 1
    return pow(1.0f - saturate(NormalizeDistanceSquared), FalloffExponent);
#else
    // light less than x % is considered 0
    const float CutoffPercentage = 30.0f;

    float CutoffFraction = CutoffPercentage * 0.01f;

    // those could be computed on C++ side
    float PreCompX = 1.0f - CutoffFraction;
    float PreCompY = CutoffFraction;
    float PreCompZ = CutoffFraction / PreCompX;

    return (1.0f / ( saturate(NormalizeDistanceSquared) * PreCompX + PreCompY) - 1.0f) * PreCompZ;
#endif

}

/**
 * Calculates attenuation for a spot light.
 * WorldLightVector is the vector from the position being shaded to the light, divided by the radius of the light.
 * SpotDirection is the direction of the spot light.
 * SpotAngles.x is CosOuterCone, SpotAngles.y is InvCosConeDifference.
 */
float SpotAttenuation(vec3 WorldLightVector, vec3 SpotDirection, vec2 SpotAngles)
{
    float ConeAngleFalloff = Square(saturate((dot(normalize(WorldLightVector), -SpotDirection) - SpotAngles.x) * SpotAngles.y));
    return ConeAngleFalloff;
}

// Find representative incoming light direction and energy modification
vec3 AreaLightSpecular( float SourceRadius, float SourceLength, vec3 LightDirection, vec3 LobeRoughness, inout vec3 ToLight, inout vec3 L, vec3 V, vec3 N )
{
    vec3 LobeEnergy = vec3(1.0f, 1.0f, 1.0f);

#if 0
    vec3 m = LobeRoughness * LobeRoughness;
    vec3 R = reflect( -V, N );
    float InvDistToLight = 1.0f / ( dot( ToLight, ToLight ) );

    if( SourceLength > 0.0f )
    {
        // Energy conservation
        // asin(x) is angle to sphere, atan(x) is angle to disk, saturate(x) is free and in the middle
        float LineAngle = saturate( SourceLength * InvDistToLight );
        LobeEnergy *= m / saturate( m + 0.5f * LineAngle );

        // Closest point on line segment to ray
        vec3 L01 = LightDirection * SourceLength;
        vec3 L0 = ToLight - 0.5f * L01;
        vec3 L1 = ToLight + 0.5f * L01;

#if 1
        // Shortest distance
        float a = Square( SourceLength );
        float b = dot( R, L01 );
        float t = saturate( dot( L0, b*R - L01 ) / (a - b*b) );
#else
        // Smallest angle
        float A = Square( SourceLength );
        float B = 2.0f * dot( L0, L01 );
        float C = dot( L0, L0 );
        float D = dot( R, L0 );
        float E = dot( R, L01 );
        float t = saturate( (B*D - 2.0f*C*E) / (B*E - 2.0f*A*D) );
#endif

        ToLight = L0 + t * L01;
    }

    if( SourceRadius > 0.0f )
    {
        // Energy conservation
        // asin(x) is angle to sphere, atan(x) is angle to disk, saturate(x) is free and in the middle
        float SphereAngle = saturate( SourceRadius * InvDistToLight );
        LobeEnergy *= Square( m / saturate( m + 0.5f * SphereAngle ) );

        // Closest point on sphere to ray
        vec3 ClosestPointOnRay = dot( ToLight, R ) * R;
        vec3 CenterToRay = ClosestPointOnRay - ToLight;
        vec3 ClosestPointOnSphere = ToLight + CenterToRay * saturate( SourceRadius / sqrt( dot( CenterToRay, CenterToRay ) ) );
        ToLight = ClosestPointOnSphere;
    }
#endif
    L = normalize( ToLight );

    return LobeEnergy;
}

struct BxDFContext
{
    float NoV;
    float NoL;
    float VoL;
    float NoH;
    float VoH;
    float XoV;
    float XoL;
    float XoH;
    float YoV;
    float YoL;
    float YoH;
};

void Init( inout BxDFContext Context, vec3 N, vec3 V, vec3 L )
{
    Context.NoL = saturate( dot(N, L) );
    Context.NoV = saturate( abs( dot(N, V) ) + 1e-5 );
    Context.VoL = dot(V, L);
    float InvLenH = inversesqrt( 2 + 2 * Context.VoL );
    Context.NoH = saturate( ( Context.NoL + Context.NoV ) * InvLenH );
    Context.VoH = saturate( InvLenH + InvLenH * Context.VoL );

    Context.XoV = 0.0f;
    Context.XoL = 0.0f;
    Context.XoH = 0.0f;
    Context.YoV = 0.0f;
    Context.YoL = 0.0f;
    Context.YoH = 0.0f;
}

void Init( inout BxDFContext Context, vec3 N, vec3 X, vec3 Y, vec3 V, vec3 L )
{
    Context.NoL = saturate( dot(N, L) );
    Context.NoV = saturate( abs( dot(N, V) ) + 1e-5 );
    Context.VoL = dot(V, L);
    float InvLenH = inversesqrt( 2 + 2 * Context.VoL );
    Context.NoH = saturate( ( Context.NoL + Context.NoV ) * InvLenH );
    Context.VoH = saturate( InvLenH + InvLenH * Context.VoL );
    //NoL = saturate( NoL );
    //NoV = saturate( abs( NoV ) + 1e-5 );

    Context.XoV = dot(X, V);
    Context.XoL = dot(X, L);
    Context.XoH = (Context.XoL + Context.XoV) * InvLenH;
    Context.YoV = dot(Y, V);
    Context.YoL = dot(Y, L);
    Context.YoH = (Context.YoL + Context.YoV) * InvLenH;
}

void InitMobile(inout BxDFContext Context, vec3 N, vec3 V, vec3 L, float NoL)
{
    Context.NoL = saturate(NoL);
    Context.NoV = saturate( abs( dot(N, V) ) + 1e-5 );
    Context.VoL = dot(V, L);
    vec3 H = normalize(vec3(V + L));
    Context.NoH = max(0, dot(N, H));
    Context.VoH = max(0, dot(V, H));

    //NoL = saturate( NoL );
    //NoV = saturate( abs( NoV ) + 1e-5 );

    Context.XoV = 0.0f;
    Context.XoL = 0.0f;
    Context.XoH = 0.0f;
    Context.YoV = 0.0f;
    Context.YoL = 0.0f;
    Context.YoH = 0.0f;
}


/*=============================================================================
    BRDF: Bidirectional reflectance distribution functions.
=============================================================================*/
// Physically based shading model
// parameterized with the below options
// [ Karis 2013, "Real Shading in Unreal Engine 4" slide 11 ]

// E = Random sample for BRDF.
// N = Normal of the macro surface.
// H = Normal of the micro surface.
// V = View vector going from surface's position towards the view's origin.
// L = Light ray direction

// D = Microfacet NDF
// G = Shadowing and masking
// F = Fresnel

// Vis = G / (4*NoL*NoV)
// f = Microfacet specular BRDF = D*G*F / (4*NoL*NoV) = D*Vis*F

// Diffuse model
// 0: Lambert
// 1: Burley
// 2: Oren-Nayar
// 3: Gotanda
#define PHYSICAL_DIFFUSE	0

// Microfacet distribution function
// 0: Blinn
// 1: Beckmann
// 2: GGX
#define PHYSICAL_SPEC_D		2

// Geometric attenuation or shadowing
// 0: Implicit
// 1: Neumann
// 2: Kelemen
// 3: Schlick
// 4: Smith (matched to GGX)
// 5: SmithJointApprox
// 6: CookTorrance
// 7: Cloth
// 8: Asikhimin
// 9: Charlie
#define PHYSICAL_SPEC_V		4

// Fresnel
// 0: None
// 1: Schlick
// 2: Fresnel
#define PHYSICAL_SPEC_F		1


// Energy
// 0: None
// 1: Simple Analytical Fit(avoids building a table)
#define USE_ENERGY_CONSERVATION 1


#define PI 3.1415926535
#define RECIP_PI 1.0 / PI
#define RADIANS_PER_DEGREE 0.0174532925
#define DEGREES_PER_RADIAN 57.2957795
/*=============================================================================
    BRDF: Diffuse functions.
=============================================================================*/
vec3 Diffuse_Lambert( vec3 DiffuseColor )
{
    return DiffuseColor * RECIP_PI;
}

// [Burley 2012, "Physically-Based Shading at Disney"]
// [Lagrade et al. 2014, "Moving Frostbite to Physically Based Rendering"]
vec3 Diffuse_Burley( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
    float FD90 = 0.5f + 2.0f * VoH * VoH * Roughness;
    float FdV = 1.0f + (FD90 - 1.0f) * Pow5( 1.0f - NoV );
    float FdL = 1.0f + (FD90 - 1.0f) * Pow5( 1.0f - NoL );
    return DiffuseColor * ( (1.0f / PI) * FdV * FdL );
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
vec3 Diffuse_OrenNayar( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
    float a = Roughness * Roughness;
    float s = a;// / ( 1.29 + 0.5 * a );
    float s2 = s * s;
    float VoL = 2.0f * VoH * VoH - 1.0f;		// double angle identity
    float Cosri = VoL - NoV * NoL;
    float C1 = 1.0f - 0.5f * s2 / (s2 + 0.33f);
    float C2 = 0.45f * s2 / (s2 + 0.09f) * Cosri * ( Cosri >= 0.0f ? rcp( max( NoL, NoV ) ) : 1.0f );
    return DiffuseColor / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5f );
}

// [Gotanda 2014, "Designing Reflectance Models for New Consoles"]
vec3 Diffuse_Gotanda( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
    float a = Roughness * Roughness;
    float a2 = a * a;
    float F0 = 0.04;
    float VoL = 2 * VoH * VoH - 1;		// double angle identity
    float Cosri = VoL - NoV * NoL;
#if 1
    float a2_13 = a2 + 1.36053;
    float Fr = ( 1 - ( 0.542026*a2 + 0.303573*a ) / a2_13 ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( ( -0.733996*a2*a + 1.50912*a2 - 1.16402*a ) * pow( 1 - NoV, 1 + rcp(39*a2*a2+1) ) + 1 );
    //float Fr = ( 1 - 0.36 * a ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( -2.5 * Roughness * ( 1 - NoV ) + 1 );
    float Lm = ( max( 1 - 2*a, 0 ) * ( 1 - Pow5( 1 - NoL ) ) + min( 2*a, 1 ) ) * ( 1 - 0.5*a * (NoL - 1) ) * NoL;
    float Vd = ( a2 / ( (a2 + 0.09) * (1.31072 + 0.995584 * NoV) ) ) * ( 1 - pow( 1 - NoL, ( 1 - 0.3726732 * NoV * NoV ) / ( 0.188566 + 0.38841 * NoV ) ) );
    float Bp = Cosri < 0 ? 1.4 * NoV * NoL * Cosri : Cosri;
    float Lr = (21.0 / 20.0) * (1 - F0) * ( Fr * Lm + Vd + Bp );
    return DiffuseColor / PI * Lr;
#else
    float a2_13 = a2 + 1.36053;
    float Fr = ( 1 - ( 0.542026*a2 + 0.303573*a ) / a2_13 ) * ( 1 - pow( 1 - NoV, 5 - 4*a2 ) / a2_13 ) * ( ( -0.733996*a2*a + 1.50912*a2 - 1.16402*a ) * pow( 1 - NoV, 1 + rcp(39*a2*a2+1) ) + 1 );
    float Lm = ( max( 1 - 2*a, 0 ) * ( 1 - Pow5( 1 - NoL ) ) + min( 2*a, 1 ) ) * ( 1 - 0.5*a + 0.5*a * NoL );
    float Vd = ( a2 / ( (a2 + 0.09) * (1.31072 + 0.995584 * NoV) ) ) * ( 1 - pow( 1 - NoL, ( 1 - 0.3726732 * NoV * NoV ) / ( 0.188566 + 0.38841 * NoV ) ) );
    float Bp = Cosri < 0 ? 1.4 * NoV * Cosri : Cosri / max( NoL, 1e-8 );
    float Lr = (21.0 / 20.0) * (1 - F0) * ( Fr * Lm + Vd + Bp );
    return DiffuseColor / PI * Lr;
#endif
}

// [ Chan 2018, "Material Advances in Call of Duty: WWII" ]
// It has been extended here to fade out retro reflectivity contribution from area light in order to avoid visual artefacts.
vec3 Diffuse_Chan( vec3 DiffuseColor, float a2, float NoV, float NoL, float VoH, float NoH, float RetroReflectivityWeight)
{
    // We saturate each input to avoid out of range negative values which would result in weird darkening at the edge of meshes (resulting from tangent space interpolation).
    NoV = saturate(NoV);
    NoL = saturate(NoL);
    VoH = saturate(VoH);
    NoH = saturate(NoH);

    // a2 = 2 / ( 1 + exp2( 18 * g )
    float g = saturate( (1.0 / 18.0) * log2( 2 * rcp(a2) - 1 ) );

    float F0 = VoH + Pow5( 1 - VoH );
    float FdV = 1 - 0.75 * Pow5( 1 - NoV );
    float FdL = 1 - 0.75 * Pow5( 1 - NoL );

    // Rough (F0) to smooth (FdV * FdL) response interpolation
    float Fd = mix( F0, FdV * FdL, saturate( 2.2 * g - 0.5 ) );

    // Retro reflectivity contribution.
    float Fb = ( (34.5 * g - 59 ) * g + 24.5 ) * VoH * exp2( -max( 73.2 * g - 21.2, 8.9 ) * sqrt( NoH ) );
    // It fades out when lights become area lights in order to avoid visual artefacts.
    Fb *= RetroReflectivityWeight;

    float Lobe = (1 / PI) * (Fd + Fb);

    // We clamp the BRDF lobe value to an arbitrary value of 1 to get some practical benefits at high roughness:
    // - This is to avoid too bright edges when using normal map on a mesh and the local bases, L, N and V ends up in an top emisphere setup.
    // - This maintains the full proper rough look of a sphere when not using normal maps.
    // - This also fixes the furnace test returning too much energy at the edge of a mesh.
    Lobe = min(1.0, Lobe);

    return DiffuseColor * Lobe;
}

vec3 Diffuse( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
#if   PHYSICAL_DIFFUSE == 0
    return Diffuse_Lambert( DiffuseColor );
#elif PHYSICAL_DIFFUSE == 1
    return Diffuse_Burley( DiffuseColor, Roughness, NoV, NoL, VoH );
#elif PHYSICAL_DIFFUSE == 2
    return Diffuse_OrenNayar( DiffuseColor, Roughness, NoV, NoL, VoH );
#elif PHYSICAL_DIFFUSE == 3
    return Diffuse_Gotanda( DiffuseColor, Roughness, NoV, NoL, VoH );
//#elif PHYSICAL_DIFFUSE == 4
//    return Diffuse_Chan( DiffuseColor, Roughness, NoV, NoL, VoH );
#endif
}

/*=============================================================================
    BRDF: Distribution functions.
=============================================================================*/

// [Blinn 1977, "Models of light reflection for computer synthesized pictures"]
float D_Blinn( float Roughness, float NoH )
{
    float m = Roughness * Roughness;
    float m2 = m * m;
    float n = 2.0f / m2 - 2.0f;
    return (n+2) / (2.0f*PI) * PhongShadingPow( NoH, n );		// 1 mad, 1 exp, 1 mul, 1 log
}

// [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
float D_Beckmann( float Roughness, float NoH )
{
    float m = Roughness * Roughness;
    float m2 = m * m;
    float NoH2 = NoH * NoH;
    return exp( (NoH2 - 1.0f) / (m2 * NoH2) ) / ( PI * m2 * NoH2 * NoH2 );
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX( float Roughness, float NoH )
{
    float m = Roughness * Roughness;
    float m2 = m * m;
    float d = ( NoH * m2 - NoH ) * NoH + 1.0f;	// 2 mad
    return m2 / ( PI*d*d );					// 4 mul, 1 rcp
}

// Anisotropic GGX
// [Burley 2012, "Physically-Based Shading at Disney"]
float D_GGXaniso( float RoughnessX, float RoughnessY, float NoH, vec3 H, vec3 X, vec3 Y )
{
    float mx = RoughnessX * RoughnessX;
    float my = RoughnessY * RoughnessY;
    float XoH = dot( X, H );
    float YoH = dot( Y, H );
    float d = XoH*XoH / (mx*mx) + YoH*YoH / (my*my) + NoH*NoH;
    return 1.0f / ( PI * mx*my * d*d );
}

float Distribution( float Roughness, float NoH )
{
#if   PHYSICAL_SPEC_D == 0
    return D_Blinn( Roughness, NoH );
#elif PHYSICAL_SPEC_D == 1
    return D_Beckmann( Roughness, NoH );
#elif PHYSICAL_SPEC_D == 2
    return D_GGX( Roughness, NoH );
#endif
}

/*=============================================================================
    BRDF: Visibility functions.
=============================================================================*/

float Vis_Implicit( )
{
    return 0.25f;
}

// [Neumann et al. 1999, "Compact metallic reflectance models"]
float Vis_Neumann( float NoV, float NoL )
{
    return 1.0f / ( 4.0f * max( NoL, NoV ) );
}

// [Kelemen 2001, "A microfacet based coupled specular-matte brdf model with importance sampling"]
float Vis_Kelemen( float VoH )
{
    return rcp( 4.0f * VoH * VoH );
}

// Tuned to match behavior of G_Smith
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float Vis_Schlick( float Roughness, float NoV, float NoL )
{
    float k = Square( Roughness ) * 0.5f;
    float Vis_SchlickV = NoV * (1.0f - k) + k;
    float Vis_SchlickL = NoL * (1.0f - k) + k;
    return 0.25f / ( Vis_SchlickV * Vis_SchlickL );
}

// Smith term for GGX modified by Disney to be less "hot" for small roughness values
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
// [Burley 2012, "Physically-Based Shading at Disney"]
float Vis_Smith( float Roughness, float NoV, float NoL )
{
    float a = Square( Roughness );
    float a2 = a*a;

    float Vis_SmithV = NoV + sqrt( NoV * (NoV - NoV * a2) + a2 );
    float Vis_SmithL = NoL + sqrt( NoL * (NoL - NoL * a2) + a2 );
    return rcp( Vis_SmithV * Vis_SmithL );
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox( float Roughness, float NoV, float NoL )
{
    float a = Square( Roughness );
    float Vis_SmithV = NoL * ( NoV * ( 1.0f - a ) + a );
    float Vis_SmithL = NoV * ( NoL * ( 1.0f - a ) + a );
    return 0.5f * rcp( Vis_SmithV + Vis_SmithL );
}

float Vis_CookTorrance(float Roughness, float NoV, float NoL, float VoH, float NoH )
{
    float a = Square( Roughness );
    return min(1.0f, min((2.0f * NoH * NoV)/VoH, (2.0f * NoH * NoL)/ VoH));
}


float Vis_Cloth( float NoV, float NoL )
{
    return rcp( 4 * ( NoL + NoV - NoL * NoV ) );
}

// [Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"]
float Vis_Charlie_L(float x, float r)
{
    r = saturate(r);
    r = 1.0 - (1. - r) * (1. - r);

    float a = mix(25.3245 , 21.5473 , r);
    float b = mix( 3.32435,  3.82987, r);
    float c = mix( 0.16801,  0.19823, r);
    float d = mix(-1.27393, -1.97760, r);
    float e = mix(-4.85967, -4.32054, r);

    return a * rcp( (1 + b * pow(x, c)) + d * x + e);
}
float Vis_Charlie(float Roughness, float NoV, float NoL)
{
    float VisV = NoV < 0.5 ? exp(Vis_Charlie_L(NoV, Roughness)) : exp(2 * Vis_Charlie_L(0.5, Roughness) - Vis_Charlie_L(1 - NoV, Roughness));
    float VisL = NoL < 0.5 ? exp(Vis_Charlie_L(NoL, Roughness)) : exp(2 * Vis_Charlie_L(0.5, Roughness) - Vis_Charlie_L(1 - NoL, Roughness));

    return rcp(((1 + VisV + VisL) * (4 * NoV * NoL)));
}

float Vis_Ashikhmin(float NoV, float NoL)
{
    return rcp(4 * (NoL + NoV - NoL * NoV));
}

// Vis = G / (4*NoL*NoV)
float Visibility( float Roughness, float NoV, float NoL, float VoH, float NoH )
{
#if   PHYSICAL_SPEC_V == 0
    return Vis_Implicit( );
#elif PHYSICAL_SPEC_V == 1
    return Vis_Neumann( NoV, NoL );
#elif PHYSICAL_SPEC_V == 2
    return Vis_Kelemen( VoH );
#elif PHYSICAL_SPEC_V == 3
    return Vis_Schlick( Roughness, NoV, NoL );
#elif PHYSICAL_SPEC_V == 4
    return Vis_Smith( Roughness, NoV, NoL );
#elif PHYSICAL_SPEC_V == 5
    return Vis_SmithJointApprox( Roughness, NoV, NoL );
#elif PHYSICAL_SPEC_V == 6
    return Vis_CookTorrance( Roughness, NoV, NoL, VoH, NoH );
#elif PHYSICAL_SPEC_V == 7
    return Vis_Cloth( NoV, NoL );
#elif PHYSICAL_SPEC_V == 8
    return Vis_Ashikhmin( NoV, NoL );
#elif PHYSICAL_SPEC_V == 9
    return Vis_Charlie( Roughness, NoV, NoL );
#endif
}

/*=============================================================================
    BRDF: Fresnel functions.
=============================================================================*/
vec3 F_None( vec3 SpecularColor )
{
    return SpecularColor;
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// [Lagarde 2012, "Spherical Gaussian approximation for Blinn-Phong, Phong and Fresnel"]
vec3 F_Schlick( vec3 SpecularColor, float VoH )
{
    float Fc = Pow5( 1.0f - VoH );					// 1 sub, 3 mul
    //return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad

    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    return saturate( 50.0f * SpecularColor.g ) * Fc + (1.0f - Fc) * SpecularColor;
}

vec3 F_Fresnel( vec3 SpecularColor, float VoH )
{

    vec3 SpecularColorSqrt = sqrt( clamp( vec3(0.0f, 0.0f, 0.0f), vec3(0.99f, 0.99f, 0.99f), SpecularColor ) );
    vec3 n = ( 1.0f + SpecularColorSqrt ) / ( 1.0f - SpecularColorSqrt );
    vec3 g = sqrt( n*n + VoH*VoH - 1.0f );
    return 0.5f * Square( (g - VoH) / (g + VoH) ) * ( 1.0f + Square( ((g+VoH)*VoH - 1.0f) / ((g-VoH)*VoH + 1.0f) ) );
}

vec3 F_AdobeF82(vec3 F0, vec3 F82, float VoH)
{
    // [Kutz et al. 2021, "Novel aspects of the Adobe Standard Material" ]
    // See Section 2.3 (note the formulas in the paper do not match the code, the code is the correct version)
    // The constants below are derived by just constant folding the terms dependent on CosThetaMax=1/7
    float Fc = Pow5(1 - VoH);
    float K = 49.0 / 46656.0;
    vec3 b = (K - K * F82) * (7776.0 + 9031.0 * F0);
    return saturate(F0 + Fc * ((1 - F0) - b * (VoH - VoH * VoH)));
}
// ----------------------------------------------------------------------------

vec3 F_Roughness(vec3 SpecularColor, float Roughness, float VoH)
{
    // Sclick using roughness to attenuate fresnel.
    return (SpecularColor + (max(vec3(1.0f-Roughness, 1.0f-Roughness, 1.0f-Roughness), SpecularColor) - SpecularColor) * pow((1.0f - VoH), 5.0f));
}


vec3 Fresnel( vec3 SpecularColor, float VoH )
{
#if   PHYSICAL_SPEC_F == 0
    return F_None( SpecularColor );
#elif PHYSICAL_SPEC_F == 1
    return F_Schlick( SpecularColor, VoH );
#elif PHYSICAL_SPEC_F == 2
    return F_Fresnel( SpecularColor, VoH );
#endif
}

/*=============================================================================
    BRDF: Env functions.
=============================================================================*/
vec3 EnvBRDF(vec3 F0, vec3 F90, float Roughness, float NoV, sampler2D BRDFIntegrationMap)
{
    // Importance sampled preintegrated G * F
    vec2 AB = texture2DLod( BRDFIntegrationMap, vec2( NoV, Roughness ), 0 ).xy;
    vec3 GF = F0 * AB.x + F90 * AB.y;
    return GF;
}

// [Karis 2013, "Real Shading in Unreal Engine 4" slide 11]
vec3 EnvBRDF( vec3 SpecularColor, float Roughness, float NoV, sampler2D BRDFIntegrationMap )
{
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    // Note: this is needed for the 'specular' show flag to work, since it uses a SpecularColor of 0
    float F90 = saturate( 50.0 * SpecularColor.g );

    return EnvBRDF(SpecularColor, vec3_splat(F90), Roughness, NoV, BRDFIntegrationMap);
}


vec2 EnvBRDFApproxLazarov(float Roughness, float NoV)
{
    // [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
    // Adaptation to fit our G term.
    const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
    vec4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}


vec3 EnvBRDFApprox(vec3 F0, vec3 F90, float Roughness, float NoV)
{
    vec2 AB = EnvBRDFApproxLazarov(Roughness, NoV);
    return F0 * AB.x + F90 * AB.y;
}


vec3 EnvBRDFApprox( vec3 SpecularColor, float Roughness, float NoV )
{
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    // Note: this is needed for the 'specular' show flag to work, since it uses a SpecularColor of 0
    float F90 = saturate( 50.0 * SpecularColor.g );

    return EnvBRDFApprox(SpecularColor, vec3_splat(F90), Roughness, NoV);
}

float EnvBRDFApproxNonmetal( float Roughness, float NoV )
{
    // Same as EnvBRDFApprox( 0.04, Roughness, NoV )
    const vec2 c0 = vec2( -1, -0.0275 );
    const vec2 c1 = vec2( 1, 0.0425 );
    vec2 r = Roughness * c0 + c1;
    return min( r.x * r.x, exp2( -9.28 * NoV ) ) * r.x + r.y;
}

void EnvBRDFApproxFullyRough(inout vec3 DiffuseColor, inout vec3 SpecularColor)
{
    // Factors derived from EnvBRDFApprox( SpecularColor, 1, 1 ) == SpecularColor * 0.4524 - 0.0024
    DiffuseColor += SpecularColor * 0.45;
    SpecularColor = vec3_splat(0);
    // We do not modify Roughness here as this is done differently at different places.
}
void EnvBRDFApproxFullyRough(inout vec3 DiffuseColor, inout float SpecularColor)
{
    DiffuseColor += SpecularColor * 0.45;
    SpecularColor = 0;
}
void EnvBRDFApproxFullyRough(inout vec3 DiffuseColor, inout vec3 F0, inout vec3 F90)
{
    DiffuseColor += F0 * 0.45;
    F0 = F90 = vec3_splat(0);
}


vec3 GetEnvBRDF(vec3 SpecularColor, float Roughness, float NoV, sampler2D BRDFIntegrationMap)
{
#if FULLY_ROUGH
    return vec3_splat(0.0f);
#elif MOBILE_USE_PREINTEGRATED_GF
    return EnvBRDF(SpecularColor, Roughness, NoV, BRDFIntegrationMap);
#elif NONMETAL
    // If nothing is hooked up to Metalic and Specular,
    // then defaults are the same as a non-metal,
    // so this define is safe.
    return EnvBRDFApproxNonmetal(Roughness, NoV).xxx;
#else
    return EnvBRDFApprox(SpecularColor, Roughness, NoV);
#endif
}

float GetSpecularOcclusion(float NoV, float RoughnessSq, float AO)
{
    return saturate( pow( NoV + AO, RoughnessSq ) - 1 + AO );
}

vec2 GGXEnergyLookup(float Roughness, float NoV)
{
#if USE_ENERGY_CONSERVATION == 1
    float r = Roughness;
    float c = NoV;
    float E = 1.0 - saturate(pow(r, c / r) * ((r * c + 0.0266916) / (0.466495 + c)));
    float Ef = Pow5(1 - c) * pow(2.36651 * pow(c, 4.7703 * r) + 0.0387332, r);
    return vec2(E, Ef);
#else
    return vec2(1, 0);
#endif
}

float DiffuseEnergyLookup(float Roughness, float NoV)
{
#if USE_ENERGY_CONSERVATION == 1
    //return View.ShadingEnergyDiffuseTexture.SampleLevel(View.ShadingEnergySampler, vec2(NoV, Roughness), 0);
    // For now we do not apply Chan diffuse energy preservation on diffuse ambiant.
    // This is because Chan is built for F=0.04 and unfortunately this causes ambient to darken a grazing angles.
    // SUBSTRATE_TODO Apply the inverse of Fresnel with F=0.04 on Chan when building the table.
    return 1.0f;
#elif USE_ENERGY_CONSERVATION == 2
    // TODO
    return 1.0f;
#else
    return 1.0f;
#endif
}

struct FBxDFEnergyTerms
{
    vec3 W; // overall weight to scale the lobe BxDF by to ensure energy conservation
    vec3 E; // Directional albedo of the lobe for energy preservation and lobe picking
};

vec3 GetF0F90(vec3 InF0)
{
#if IS_BXDF_ENERGY_TYPE_ACHROMATIC
    return max3(InF0.x, InF0.y, InF0.z);
#else
    return InF0;
#endif
}

// Given a split-sum approximation of directional albedo for a BxDF, compute multiple scattering weight and multiple scattering directional albedo
// while taking into account the fresnel term (assumed to be F_Schlick)
FBxDFEnergyTerms ComputeFresnelEnergyTerms(vec2 E, vec3 InF0, vec3 InF90)
{
    vec3 F0  = GetF0F90(InF0);
    vec3 F90 = GetF0F90(InF90);

    FBxDFEnergyTerms Result;
    // [2] Eq 16: this restores the missing energy of the bsdf, while also accounting for the fact that the fresnel term causes some energy to be absorbed
    // NOTE: using F0 here is an approximation, but for schlick fresnel Favg is almost exactly equal to F0
    Result.W = 1.0 + F0 * ((1 - E.x) / E.x);

    // Now estimate the amount of energy reflected off this specular lobe so that we can remove it from underlying BxDF layers (like diffuse)
    // This relies on the split-sum approximation as in [3] Sec 4.
    // This term can also be useful to compute the probability of choosing among lobes
    Result.E = Result.W * (E.x * F0 + E.y * (F90 - F0));
    return Result;
}

FBxDFEnergyTerms ComputeGGXSpecEnergyTerms(float Roughness, float NoV, vec3 F0, vec3 F90)
{
    FBxDFEnergyTerms Out;
#if USE_ENERGY_CONSERVATION > 0
    {
        Out = ComputeFresnelEnergyTerms(GGXEnergyLookup(Roughness, NoV), F0, F90);
    }
#else
    {
        Out.W = vec3(1.0f, 1.0f, 1.0f);
        Out.E = GetF0F90(F0);
    }
#endif
    return Out;
}

FBxDFEnergyTerms ComputeGGXSpecEnergyTerms(float Roughness, float NoV, vec3 F0)
{
    float F90 = saturate(50.0 * F0.g); // See F_Schlick implementation
    return ComputeGGXSpecEnergyTerms(Roughness, NoV, F0, vec3_splat(F90));
}

float Luminance( vec3 LinearColor )
{
    return dot( LinearColor, vec3( 0.3, 0.59, 0.11 ) );
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Energy Preservation/Conservation

// Return the energy absorbed by upper layer (e.g., for the specular layer attenuation onto diffuse)
// Note: Use the directional albedo luminance to avoid color-shift due to metallic specular (for which the energy should be absorbed, not transmitted)
float ComputeEnergyPreservation(FBxDFEnergyTerms EnergyTerms)
{
#if USE_ENERGY_CONSERVATION > 0

    return 1.0f - Luminance(EnergyTerms.E);
#else
    return 1.0f;
#endif
}

// Return the energy conservation weight factor for account energy loss in the BSDF model (i.e. due to micro-facet multiple scattering)
vec3 ComputeEnergyConservation(FBxDFEnergyTerms EnergyTerms)
{
    return EnergyTerms.W;
}

struct SurfaceShading
{
    vec3 direct;
    vec3 indirect;
};

SurfaceShading StandardShading(
 vec3 DiffuseColor,
 vec3 IndirectDiffuse,
 vec3 SpecularColor,
 vec3 IndirectSpecular,
 sampler2D BRDFIntegrationMap,
 vec3 LobeRoughness,
 vec3 LobeEnergy,
 float Metalness,
 float AO,
 vec3 L,
 vec3 V,
 vec3 N )
{
    BxDFContext context;
    Init(context, N, V, L);

    float Roughness = LobeRoughness[1];
    // Generalized microfacet specular
    float D = Distribution( Roughness, context.NoH ) * LobeEnergy[1];
    float Vis = Visibility( Roughness, context.NoV, context.NoL, context.VoH, context.NoH );
    vec3 F = Fresnel( SpecularColor, context.VoH );

    vec3 DiffuseLighting = Diffuse( DiffuseColor, Roughness, context.NoV, context.NoL, context.VoH ) * LobeEnergy[2];

    float RoughnessSq = Roughness * Roughness;
    float SpecularOcclusion = GetSpecularOcclusion(context.NoV, RoughnessSq, AO);


#if USE_ENERGY_CONSERVATION > 0
    FBxDFEnergyTerms SpecularEnergyTerms = ComputeGGXSpecEnergyTerms(Roughness, context.NoV, SpecularColor);
    vec3 EnvBRDFValue = SpecularEnergyTerms.E; // EnvBRDF accounting for multiple scattering when enabled
    float EnergyPreservationFactor = ComputeEnergyPreservation(SpecularEnergyTerms);
    vec3 EnergyConservationFactor = ComputeEnergyConservation(SpecularEnergyTerms);
#else
    vec3 EnvBRDFValue = GetEnvBRDF(SpecularColor, Roughness, context.NoV, BRDFIntegrationMap);
    float EnergyPreservationFactor = 1.0f;
    vec3 EnergyConservationFactor = vec3_splat(1.0f);
#endif

    SurfaceShading shading;
    shading.indirect = (DiffuseColor * IndirectDiffuse) + (IndirectSpecular * EnvBRDFValue * SpecularOcclusion);
    shading.direct = (DiffuseLighting * EnergyPreservationFactor) + (D * Vis) * F * EnergyConservationFactor;
    return shading;
}


vec3 SubsurfaceShading( vec3 SubsurfaceColor, float Opacity, float AO, vec3 L, vec3 V, vec3 N )
{
    vec3 H = normalize(V + L);
    // to get an effect when you see through the material
    // hard coded pow constant
    float InScatter = pow(saturate(dot(L, -V)), 12.0f) * mix(3.0f, 0.1f, Opacity);
    // wrap around lighting, /(PI*2) to be energy consistent (hack do get some view dependnt and light dependent effect)
    // Opacity of 0 gives no normal dependent lighting, Opacity of 1 gives strong normal contribution
    float NormalContribution = saturate(dot(N, H) * Opacity + 1.0f - Opacity);
    float BackScatter = AO * NormalContribution / (PI * 2.0f);

    // lerp to never exceed 1 (energy conserving)
    return SubsurfaceColor * mix(BackScatter, 1.0f, InScatter);
}

vec3 SubsurfaceShadingTwoSided( vec3 SubsurfaceColor, vec3 L, vec3 V, vec3 N )
{
    // http://blog.stevemcauley.com/2011/12/03/energy-conserving-wrapped-diffuse/
    float Wrap = 0.5f;
    float NoL = saturate( ( dot(-N, L) + Wrap ) / Square( 1.0f + Wrap ) );

    // GGX scatter distribution
    float VoL = saturate( dot(V, -L) );
    float a = 0.6f;
    float a2 = a * a;
    float d = ( VoL * a2 - VoL ) * VoL + 1.0f;	// 2 mad
    float GGX = (a2 / PI) / (d * d);		// 2 mul, 1 rcp
    return NoL * GGX * SubsurfaceColor;
}

float ComputeReflectionCaptureMipFromRoughness(float Roughness, float maxMipLevels)
{
    const float REFLECTION_CAPTURE_ROUGHEST_MIP = 1.0f;
    const float REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE = 1.2f;
    // Heuristic that maps roughness to mip level
    // This is done in a way such that a certain mip level will always have the same roughness, regardless of how many mips are in the texture
    // Using more mips in the cubemap just allows sharper reflections to be supported
    // Note: this must match the logic in FilterReflectionEnvironment that generates the mip filter samples!
    float LevelFrom1x1 = REFLECTION_CAPTURE_ROUGHEST_MIP - REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE * log2(max(Roughness, 0.001));
    //// Note: must match GReflectionCaptureSize
    float HardcodedNumCaptureArrayMips = maxMipLevels;
    return HardcodedNumCaptureArrayMips - 1 - LevelFrom1x1;
}

float ComputeReflectionCaptureMipFromRoughnessEx(float Roughness, float maxMipLevels)
{
    float LodFactor = Roughness*(1.7f - 0.7f * Roughness);
    return LodFactor * maxMipLevels;
}

float ComputeReflectionCaptureRoughnessFromMip(float Mip, float CubemapMaxMip)
{
    const float REFLECTION_CAPTURE_ROUGHEST_MIP = 1.0f;
    const float REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE = 1.2f;


    float LevelFrom1x1 = CubemapMaxMip - 1 - Mip;
    return exp2( ( REFLECTION_CAPTURE_ROUGHEST_MIP - LevelFrom1x1 ) / REFLECTION_CAPTURE_ROUGHNESS_MIP_SCALE );
}

#endif // __LIGHTING_SH__
