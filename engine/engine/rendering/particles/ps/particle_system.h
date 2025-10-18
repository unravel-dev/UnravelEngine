/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef PARTICLE_SYSTEM_H_HEADER_GUARD
#define PARTICLE_SYSTEM_H_HEADER_GUARD

#include <bx/allocator.h>
#include <bx/bounds.h>
#include <bx/easing.h>
#include <bx/rng.h>
#include <bgfx/bgfx.h>
#include <math/gradient.h>
#include <math/math.h>
#include <math/bbox.h>
#include <core/base/basetypes.hpp>

struct EmitterHandle { uint16_t idx; };

template<typename Ty>
inline bool isValid(Ty _handle)
{
	return _handle.idx != UINT16_MAX;
}

struct EmitterShape
{
	enum Enum
	{
		Sphere,
		Hemisphere,
		Circle,
		Box,
		Rect,

		Count
	};
};

struct EmitterDirection
{
	enum Enum
	{
		Up,
		Outward,

		Count
	};
};

struct EmitterUniforms
{
	void reset();

	math::vec3 m_position;
	math::vec3 m_angle;
	math::vec3 m_scale; // 3D scale for the entire particle system (x, y, z)
	math::vec3 m_emissionShapeScale; // 3D scale for the emission shape (x, y, z)

	// Previous position for motion interpolation (set internally)
	math::vec3 m_prevPosition;

	math::gradient<frange_t> m_velocityGradient; // Velocity gradient over particle lifetime
	math::gradient<frange_t> m_blendGradient;    // Blend/opacity gradient over particle lifetime
	math::gradient<frange_t> m_scaleGradient;    // Scale gradient over particle lifetime
	float m_lifetime;
	float m_gravityScale;
	float m_particlesPerSecond; // Emission rate in particles per second
	float m_temporalMotion; // Temporal motion interpolation factor (0.0 = no interpolation, 1.0 = full interpolation)
	float m_velocityDamping; // Velocity damping factor (0.0 = no damping, 1.0 = full damping)
	math::vec3 m_forceOverLifetime; // Additional force applied over particle lifetime
	frange_t m_sizeBySpeedRange; // Size multiplier range [min_multiplier, max_multiplier]
	frange_t m_sizeBySpeedVelocityRange; // Velocity range for size mapping [min_speed, max_speed]
	math::gradient<math::color> m_colorBySpeedGradient; // Color gradient based on speed
	frange_t m_colorBySpeedVelocityRange; // Velocity range for color mapping [min_speed, max_speed]

	math::gradient<math::color> m_colorGradient; // Color gradient over particle lifetime
	float m_emissionLifetime; // Duration of one emission cycle

	bx::Easing::Enum m_easePos; // Only position easing remains - others handled by gradients

	bgfx::TextureHandle m_texture;
};

///
void psInit(uint16_t _maxEmitters = 64, bx::AllocatorI* _allocator = nullptr);

///
void psShutdown();

// Note: Sprite system removed - use bgfx::TextureHandle directly in EmitterUniforms

///
EmitterHandle psCreateEmitter(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles);

///
void psUpdateEmitter(EmitterHandle _handle, float _dt, EmitterUniforms* _uniforms = nullptr);

bool psHasUpdated(EmitterHandle _handle);

///
void psResetEmitter(EmitterHandle _handle);

///
void psGetAabb(EmitterHandle _handle, math::bbox& _outAabb);

uint32_t psGetNumParticles(EmitterHandle _handle);

///
void psDestroyEmitter(EmitterHandle _handle);

///
void psRenderEmitter(EmitterHandle _handle, uint8_t _view, bgfx::ProgramHandle _program, const float* _mtxView, const math::vec3& _eye, bgfx::TextureHandle _texture);

///
/// Render multiple emitters in a single batched draw call (all must use the same texture)
/// This is much more efficient than calling psRenderEmitter multiple times as it:
/// - Combines all particles into a single instance buffer
/// - Sorts all particles globally for proper alpha blending
/// - Uses only one draw call instead of multiple
/// 
/// Example usage:
///   EmitterHandle handles[] = {fire_emitter, smoke_emitter, spark_emitter};
///   psRenderEmitterBatch(handles, 3, view, program, viewMatrix, cameraPos, fireTexture);
uint32_t psRenderEmitterBatch(const EmitterHandle* _handles, uint32_t _count, uint8_t _view, bgfx::ProgramHandle _program, const float* _mtxView, const math::vec3& _eye, bgfx::TextureHandle _texture);

#endif // PARTICLE_SYSTEM_H_HEADER_GUARD
