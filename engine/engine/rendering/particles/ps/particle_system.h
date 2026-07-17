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
		Inward,

		Count
	};
};

struct EmitterSpawnLocation
{
	enum Enum
	{
		Inside,  // Particles spawn inside the shape (current behavior)
		Surface, // Particles spawn on the surface of the shape

		Count
	};
};

struct SimulationSpace
{
	enum Enum
	{
		World, // Particles are simulated in world space (current behavior)
		Local, // Particles are simulated in local space and transformed during rendering

		Count
	};
};

struct TextureMode
{
	enum Enum
	{
		MultiChannel, // Standard RGBA texture (default)
		Mask,         // Black/white mask texture (black = transparent, white = opaque)

		Count
	};
};

struct RenderMode
{
	enum Enum
	{
		Billboard,        // Always faces camera (default)
		HorizontalBillboard,       // Rotates around Y axis only, stays horizontal (parallel to ground)
		VerticalBillboard,         // Rotates around X/Z axis, stays vertical (perpendicular to ground)

		Count
	};
};

struct BlendMode
{
	enum Enum
	{
		Normal,
		Additive,
		Multiply,
	};
};

struct EmitterUniforms
{
	void reset();

	// Simulation space determines how particles are transformed
	SimulationSpace::Enum m_simulationSpace;
	
	// Transform for both local and world simulation
	// Using math::transform for better performance - keeps components separate and combines into matrix when needed
	math::transform m_transform;
	math::transform m_prevTransform; // Previous transform for motion interpolation (set internally)

	// Emission shape properties (separate from transform for flexibility)
	math::vec3 m_emissionShapePosition; // Position offset for the emission shape (relative to transform)
	math::vec3 m_emissionShapeScale;    // 3D scale for the emission shape (x, y, z)

	// Spawn location determines where particles spawn within the emission shape
	EmitterSpawnLocation::Enum m_spawnLocation; // Inside or Surface

	math::gradient<frange_t> m_velocityGradient; // Velocity gradient over particle lifetime
	math::gradient<frange_t> m_scaleGradient;    // Scale gradient over particle lifetime
	math::vec3 m_initialScale3D; // 3D particle scale (allows rectangular particles, default: 1,1,1)
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

	math::gradient<float> m_lifetimeByEmitterSpeedGradient; // Lifetime multiplier gradient based on emitter speed
	frange_t m_lifetimeByEmitterSpeedRange; // Emitter speed range for lifetime mapping [min_speed, max_speed]

	math::gradient<math::color> m_colorGradient; // Color gradient over particle lifetime
	float m_emissionLifetime; // Duration of one emission cycle
	float m_opacity; // Global opacity for all particles (0.0 = fully transparent, 1.0 = no change)
	float m_colorIntensity; // HDR multiplier for particle color RGB (1.0 = no change, >1.0 = glow)

	// Playback control states
	bool m_playing; // Whether the emitter is currently playing/active
	bool m_paused;  // Whether the emitter is paused (playing but with dt = 0)
	bool m_loop;    // Whether the emitter loops continuously (true) or emits only once (false)
	float m_startDelay; // Delay before particle emission starts (in seconds, similar to Unity's start delay)

	bx::Easing::Enum m_easePos; // Only position easing remains - others handled by gradients

	TextureMode::Enum m_textureMode; // Texture mode (MultiChannel or Mask)
	RenderMode::Enum m_renderMode; // Render mode (Billboard, Horizontal, or Vertical)
	BlendMode::Enum m_blendMode;   // Blend mode (Normal, Additive, Multiply)
	
	// Billboard vectors (calculated from render mode and camera)
	math::vec3 m_billboardRight; // Right vector for billboarding
	math::vec3 m_billboardUp;    // Up vector for billboarding
	
	// Texture sheet animation parameters
	math::vec2 m_texSheetTiles; // Number of tiles in the texture sheet grid (X columns, Y rows)
	float m_texSheetCycles;    // Number of times the animation loops over particle lifetime (0 = disabled)
	bool m_texSheetRandomize;  // Start each particle at a random frame in the animation
	
	// Rotation control
	bool m_alignToDirection; // If true, particles rotate to align with their velocity direction
	
	// Pivot control (0,0 = bottom-left, 0.5,0.5 = center, 1,1 = top-right)
	math::vec2 m_pivot; // Pivot point for particle rotation and positioning (default: 0.5, 0.5 = center)
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
/// Submit one homogeneous particle batch: same texture, @ref TextureMode (mask vs multi-channel), and
/// @ref BlendMode. @ref RenderMode is per-instance. The pipeline groups emitters by material.
/// When @p _sort_by_depth is true, instances are sorted back-to-front (sequential for small counts,
/// parallel above a threshold) then gathered into the instance buffer. When false, particles are
/// streamed directly from each emitter's array in emission order (additive / order-independent
/// blends) with no sort-key scratch. May issue multiple submits if the transient instance buffer
/// is smaller than the particle count.
uint32_t psRenderEmitterBatch(const EmitterHandle* _handles,
                              uint32_t _count,
                              uint8_t _view,
                              bgfx::ProgramHandle _program,
                              const float* _mtxView,
                              const math::vec3& _eye,
                              bgfx::TextureHandle _texture,
                              uint64_t _blend_state,
                              bool _sort_by_depth = true);

#endif // PARTICLE_SYSTEM_H_HEADER_GUARD
