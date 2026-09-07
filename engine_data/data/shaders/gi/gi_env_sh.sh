#ifndef __GI_ENV_SH_SH__
#define __GI_ENV_SH_SH__

/*
 * The environment radiance SH basis, for kernels that read the nine coefficients from a
 * buffer block instead of the IRRADIANCE_SH texture (lighting.sh's eval_radiance_sh takes
 * the sampler). The screen-probe trace and the reflection trace have no sampler stage left,
 * so their args passes stage the coefficients into the buffers those kernels already bind
 * (GI_ENV_SH_COEFFS); the kernel sums coefficient k x GiEnvShBasis(k, dir) and clamps the
 * ringing negative, exactly as eval_radiance_sh does.
 */

/// The k-th real SH basis function (the ordering eval_radiance_sh uses) at @p dir.
float GiEnvShBasis(int k, vec3 dir)
{
	float x = dir.x;
	float y = dir.y;
	float z = dir.z;
	if(k == 0) return 0.282095;
	if(k == 1) return 0.488603 * y;
	if(k == 2) return 0.488603 * z;
	if(k == 3) return 0.488603 * x;
	if(k == 4) return 1.092548 * x * z;
	if(k == 5) return 1.092548 * y * z;
	if(k == 6) return 1.092548 * x * y;
	if(k == 7) return 0.315392 * (3.0 * z * z - 1.0);
	return 0.546274 * (x * x - y * y);
}

#endif // __GI_ENV_SH_SH__
