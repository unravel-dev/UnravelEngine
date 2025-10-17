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
		Disc,
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
void psUpdateEmitter(EmitterHandle _handle, float _dt, const EmitterUniforms* _uniforms = nullptr);

///
void psResetEmitter(EmitterHandle _handle);

///
void psGetAabb(EmitterHandle _handle, bx::Aabb& _outAabb);

uint32_t psGetNumParticles(EmitterHandle _handle);

///
void psDestroyEmitter(EmitterHandle _handle);

///
void psRenderEmitter(EmitterHandle _handle, uint8_t _view, bgfx::ProgramHandle _program, const float* _mtxView, const bx::Vec3& _eye);

#endif // PARTICLE_SYSTEM_H_HEADER_GUARD
