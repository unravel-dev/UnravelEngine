#ifndef LIGHT_ATTENUATION_SH_HEADER_GUARD
#define LIGHT_ATTENUATION_SH_HEADER_GUARD

/*
 * Local light attenuation and emitter geometry (an inverse-squared falloff), shared by the
 * per-light deferred passes (fs_pbr_lighting.sh) and the GI surface-cache lighting
 * (gi/gpu_lights.sh), so the direct light and its bounce cannot drift apart.
 *
 * A point or spot light's intensity is luminous intensity: the illuminance it delivers at
 * distance d (world units) is intensity / d^2, cut to zero at its range by a smooth window. The
 * emitter is a capsule - a sphere of a source radius swept along a source length - whose size
 * changes the light's shape, never how much light it emits (the intensity stays constant as the
 * source size changes).
 *
 * Only built-ins here: gi/gpu_lights.sh includes this without lighting.sh.
 */

/// Squared distance added to the light distance in the falloff (1 cm^2, in m^2),
/// so the falloff stays finite at the light.
#define LOCAL_LIGHT_DISTANCE_BIAS_SQR 0.0001

/**
 * The window that takes a local light to zero at its range: (1 - (d / range)^4)^2.
 * @param ToLight Light position minus the shaded position.
 * @param InvRange 1 / range.
 */
float LocalLightRangeMask(vec3 ToLight, float InvRange)
{
    float DistanceSqrRatio = dot(ToLight, ToLight) * InvRange * InvRange;
    float Window = saturate(1.0 - DistanceSqrRatio * DistanceSqrRatio);
    return Window * Window;
}

/**
 * A spot light's cone: the squared ramp from the outer to the inner half angle.
 * @param L Unit direction from the shaded position to the light.
 * @param SpotDirection The light's axis (unit).
 * @param SpotAngles (cos outer half angle, 1 / (cos inner half angle - cos outer half angle)).
 */
float LocalLightSpotMask(vec3 L, vec3 SpotDirection, vec2 SpotAngles)
{
    float Cone = saturate((dot(L, -SpotDirection) - SpotAngles.x) * SpotAngles.y);
    return Cone * Cone;
}

/// A local light's emitter relative to the shaded position: a sphere of
/// Radius swept along the segment Line0 - Line1. Length 0 is a sphere, Radius 0 too a point.
struct CapsuleLight
{
    vec3 Line0;
    vec3 Line1;
    float Length;
    float Radius;
};

/**
 * @param ToLight Light position minus the shaded position.
 * @param Axis The segment's direction scaled by its length (zero for a sphere or a point).
 * @param Radius The sphere's radius.
 */
CapsuleLight MakeCapsuleLight(vec3 ToLight, vec3 Axis, float Radius)
{
    CapsuleLight Capsule;
    Capsule.Line0 = ToLight - 0.5 * Axis;
    Capsule.Line1 = ToLight + 0.5 * Axis;
    Capsule.Length = length(Axis);
    Capsule.Radius = Radius;
    return Capsule;
}

/// Diffuse arrival of a capsule's light at a surface: its falloff and the diffuse cosine.
struct CapsuleIrradiance
{
    /// Inverse-squared falloff: 1 / (d^2 + bias) for a sphere, the segment's normalized line
    /// integral for a tube.
    float Falloff;
    /// Cosine of the angle the segment subtends at the shaded position (1 for a sphere).
    float CosSubtended;
    /// Unit direction of the arriving light (the vector irradiance's direction).
    vec3 DiffuseL;
    /// The clamped cosine the diffuse light takes: N.L, wrapped past the horizon by the sphere
    /// (a sphere still lights a surface that faces just away from its centre).
    float NoL;
};

/**
 * N.L of a spherical cap of half angle alpha, wrapped past the horizon by a Hermite spline; fairly
 * accurate while sin(alpha) < 0.8. Zero with zero slope at -sin(alpha), sin(alpha) with slope 1
 * at sin(alpha). @p SinAlphaSqr must be positive.
 */
float SphereHorizonCosWrap(float NoL, float SinAlphaSqr)
{
    float SinAlpha = sqrt(SinAlphaSqr);
    if(NoL < SinAlpha)
    {
        float Wrapped = max(NoL, -SinAlpha) + SinAlpha;
        NoL = Wrapped * Wrapped / (4.0 * SinAlpha);
    }
    return NoL;
}

/**
 * The capsule's falloff and diffuse cosine at a surface of normal @p N.
 */
CapsuleIrradiance EvaluateCapsuleIrradiance(CapsuleLight Capsule, vec3 N)
{
    CapsuleIrradiance Result;
    BRANCH
    if(Capsule.Length > 0.0)
    {
        float InvLength0 = inversesqrt(dot(Capsule.Line0, Capsule.Line0));
        float InvLength1 = inversesqrt(dot(Capsule.Line1, Capsule.Line1));
        float InvLength01 = InvLength0 * InvLength1;
        Result.CosSubtended = dot(Capsule.Line0, Capsule.Line1) * InvLength01;
        Result.Falloff = InvLength01 / (Result.CosSubtended * 0.5 + 0.5 + LOCAL_LIGHT_DISTANCE_BIAS_SQR * InvLength01);
        // The mean of the two end directions is the segment's vector irradiance direction; its
        // length (the cosine of half the subtended angle) belongs in N.L, so N.L is taken first.
        vec3 VectorIrradiance = 0.5 * (Capsule.Line0 * InvLength0 + Capsule.Line1 * InvLength1);
        Result.NoL = dot(N, VectorIrradiance);
        Result.DiffuseL = normalize(VectorIrradiance);
    }
    else
    {
        float DistanceSqr = dot(Capsule.Line0, Capsule.Line0);
        Result.CosSubtended = 1.0;
        Result.Falloff = 1.0 / (DistanceSqr + LOCAL_LIGHT_DISTANCE_BIAS_SQR);
        Result.DiffuseL = Capsule.Line0 * inversesqrt(DistanceSqr);
        Result.NoL = dot(N, Result.DiffuseL);
    }
    if(Capsule.Radius > 0.0)
    {
        float SinAlphaSqr = saturate(Capsule.Radius * Capsule.Radius * Result.Falloff);
        Result.NoL = SphereHorizonCosWrap(Result.NoL, SinAlphaSqr);
    }
    Result.NoL = saturate(Result.NoL);
    return Result;
}

#endif // LIGHT_ATTENUATION_SH_HEADER_GUARD
