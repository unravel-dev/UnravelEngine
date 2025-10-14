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

	float m_position[3];
	float m_angle[3];
	
	// Previous position for motion interpolation (set internally)
	float m_prevPosition[3];

	float m_blendStart[2];
	float m_blendEnd[2];
	float m_velocityStart[2];
	float m_velocityEnd[2];
	float m_scaleStart[2];
	float m_scaleEnd[2];
	float m_lifetime;
	float m_gravityScale;
	float m_particlesPerSecond; // Emission rate in particles per second
	float m_temporalMotion; // Temporal motion interpolation factor (0.0 = no interpolation, 1.0 = full interpolation)
	float m_velocityDamping; // Velocity damping factor (0.0 = no damping, 1.0 = full damping)
	float m_forceOverLifetime[3]; // Additional force applied over particle lifetime (x, y, z)
	float m_sizeBySpeedRange[2]; // Size multiplier range [min_multiplier, max_multiplier]
	float m_sizeBySpeedVelocityRange[2]; // Velocity range for size mapping [min_speed, max_speed]
	uint32_t m_colorBySpeedColors[2]; // Color gradient [slow_color, fast_color]
	float m_colorBySpeedVelocityRange[2]; // Velocity range for color mapping [min_speed, max_speed]
	float m_scale[3]; // 3D scale for the entire particle system (x, y, z)

	uint32_t m_rgba[5];
	float m_emissionLifetime; // Duration of one emission cycle

	bx::Easing::Enum m_easePos;
	bx::Easing::Enum m_easeRgba;
	bx::Easing::Enum m_easeBlend;
	bx::Easing::Enum m_easeScale;

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
