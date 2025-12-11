/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include "particle_system.h"
#include "bx/bx.h"
#include <graphics/utils/bgfx_utils.h>

#include <bx/easing.h>
#include <bx/handlealloc.h>
#include <math/math.h>
#include <glm/gtc/random.hpp>
#include <vector>
#include <algorithm>
#include <engine/profiler/profiler.h>
#include <core/logging/logging.h>
// New instanced particle vertex structure (just position and UV)
struct ParticleVertex
{
    float x;
    float y;
    float z;
    float u;
    float v;

    static void init()
    {
        ms_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout ParticleVertex::ms_layout;

// Static quad geometry for instanced particles
static ParticleVertex s_quadVertices[4] = {
    {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f}, // Bottom-left
    { 0.5f, -0.5f, 0.0f, 1.0f, 1.0f}, // Bottom-right
    { 0.5f,  0.5f, 0.0f, 1.0f, 0.0f}, // Top-right
    {-0.5f,  0.5f, 0.0f, 0.0f, 0.0f}  // Top-left
};

static const uint16_t s_quadIndices[6] = {
    0, 1, 2, 2, 3, 0
};

void EmitterUniforms::reset()
{
    // Initialize simulation method and transforms
    m_simulationSpace = SimulationSpace::World; // Default to world simulation
    m_transform = math::transform(); // Identity transform
    m_prevTransform = math::transform(); // Identity transform
    
    // Initialize emission shape properties
    m_emissionShapePosition = math::vec3(0.0f, 0.0f, 0.0f); // Default: no offset
    m_emissionShapeScale = math::vec3(1.0f, 1.0f, 1.0f);    // Default: no scaling

    // Initialize spawn location
    m_spawnLocation = EmitterSpawnLocation::Inside; // Default: spawn inside shape

    // Initialize velocity gradient with default 2-point gradient (start -> end)
    m_velocityGradient = math::gradient<frange_t>();
    m_velocityGradient.add_point(frange_t(0.0f, 1.0f), 0.0f); // Start velocity range
    m_velocityGradient.add_point(frange_t(2.0f, 3.0f), 1.0f); // End velocity range

    // Initialize color gradient with default 5-point gradient (transparent -> white -> white -> white -> transparent)
    m_colorGradient = math::gradient<math::color>();
    m_colorGradient.add_point(math::color(0x00ffffff), 0.0f);  // Transparent white at start
    m_colorGradient.add_point(math::color(0xffffffff), 0.25f); // Opaque white
    m_colorGradient.add_point(math::color(0xffffffff), 0.5f);  // Opaque white
    m_colorGradient.add_point(math::color(0xffffffff), 0.75f); // Opaque white
    m_colorGradient.add_point(math::color(0x00ffffff), 1.0f);  // Transparent white at end

    // Initialize scale gradient with default 2-point gradient (start -> end)
    m_scaleGradient = math::gradient<frange_t>();
    m_scaleGradient.add_point(frange_t(0.1f, 0.2f), 0.0f); // Start scale range
    m_scaleGradient.add_point(frange_t(0.3f, 0.4f), 1.0f); // End scale range
    
    m_initialScale3D = math::vec3(1.0f, 1.0f, 1.0f); // Default: uniform scale (square particles)

    m_lifetime = 1.0f;

    m_gravityScale = 0.0f;
    m_particlesPerSecond = 50.0f;                       // Default: 50 particles per second
    m_temporalMotion = 1.0f;                            // Default: full temporal interpolation
    m_velocityDamping = 0.0f;                           // Default: no damping
    m_forceOverLifetime = math::vec3(0.0f, 0.0f, 0.0f); // Default: no additional force
    m_sizeBySpeedRange = frange_t(1.0f, 1.0f);          // Default: no size change
    m_sizeBySpeedVelocityRange = frange_t(0.0f, 10.0f); // Default velocity range
    m_colorBySpeedGradient = math::gradient<math::color>();
    m_colorBySpeedGradient.add_point(math::color(0xffffffff), 0.0f); // Slow speed: white
    m_colorBySpeedGradient.add_point(math::color(0xffffffff), 1.0f); // Fast speed: white (no color change by default)
    m_colorBySpeedVelocityRange = frange_t(0.0f, 10.0f);             // Default velocity range

    // Initialize lifetime by emitter speed gradient with default 2-point gradient (no change by default)
    m_lifetimeByEmitterSpeedGradient = math::gradient<float>();
    m_lifetimeByEmitterSpeedGradient.add_point(1.0f, 0.0f); // Slow emitter: no lifetime change
    m_lifetimeByEmitterSpeedGradient.add_point(1.0f, 1.0f); // Fast emitter: no lifetime change (default)
    m_lifetimeByEmitterSpeedRange = frange_t(0.0f, 10.0f);                   // Default emitter speed range

    m_emissionLifetime = 2.0f; // Default: 2 second emission cycle
    m_opacity = 1.0f;  // Default: no opacity modification

    // Initialize playback states
    m_playing = true;  // Default: playing
    m_paused = false;  // Default: not paused
    m_loop = true;     // Default: loop continuously
    m_startDelay = 0.0f; // Default: no start delay

    m_easePos = bx::Easing::Linear; // Only position easing remains
    
    // Initialize texture mode
    m_textureMode = TextureMode::MultiChannel; // Default: standard RGBA texture
    
    // Initialize texture sheet animation
    m_texSheetTiles = math::vec2(1.0f, 1.0f); // Default: 1x1 grid (no animation)
    m_texSheetCycles = 0.0f;   // Default: 0 (disabled)
    m_texSheetRandomize = false; // Default: all particles start at frame 0
    
    m_renderMode = RenderMode::Billboard; // Default: always face camera
    m_billboardRight = math::vec3(1.0f, 0.0f, 0.0f); // Will be updated from camera
    m_billboardUp = math::vec3(0.0f, 1.0f, 0.0f);    // Will be updated from camera
    
    m_alignToDirection = false; // Default: particles don't rotate to align with direction
    m_pivot = math::vec2(0.5f, 0.5f); // Default: center pivot
    
    // Generate LUTs for all gradients to optimize sampling performance
    m_velocityGradient.generate_lut(256);
    m_colorGradient.generate_lut(256);
    m_scaleGradient.generate_lut(256);
    m_colorBySpeedGradient.generate_lut(256);
    m_lifetimeByEmitterSpeedGradient.generate_lut(1024);
}

namespace ps
{
struct Particle
{
    math::vec3 start;
    math::vec3 end[2];
    float scale_start;
    float scale_end;

    // Cached computed properties (updated during update, used during render)
    math::color color; // Final color with all effects applied
	math::vec3 position;
    float scale;    // Final uniform scale with all effects applied
    float cached_speed; // Cached particle speed to avoid redundant calculations

    float life;
    float lifeSpan;
    
    // Texture sheet animation UV data (calculated during update)
    math::vec2 uv_offset;
    math::vec2 uv_scale;
    float texsheet_random_offset; // Random offset [0-1] for texture sheet animation start frame
    
    // Rotation quaternion (calculated when align_to_direction is enabled)
    math::quat rotation; // Quaternion representing particle rotation
};

struct ParticleSort
{
    float dist;
    uint32_t idx;
};

struct Emitter
{
    void create(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles);
    void destroy();

    void reset()
    {
        dt_ = 0.0f;

        num_particles_ = 0;
        total_particles_spawned_ = 0;
        aabb_ = math::bbox(math::vec3(-1.0f), math::vec3(1.0f));
        first_update_ = true;
        start_delay_elapsed_ = 0.0f;

        rng_.reset();
        
        // Reset temporal position buffer
        temporal_position_buffer_.clear();
        temporal_time_buffer_.clear();
        
        texture_mode_ = TextureMode::MultiChannel;
        render_mode_ = RenderMode::Billboard;
        billboard_right_ = math::vec3(1.0f, 0.0f, 0.0f);
        billboard_up_ = math::vec3(0.0f, 1.0f, 0.0f);
        particle_scale_3d_ = math::vec3(1.0f, 1.0f, 1.0f);
        pivot_ = math::vec2(0.5f, 0.5f);
    }
    
    // Temporal position buffer for smooth emitter speed calculation
    // This handles physics fixed timestep discontinuities
    static constexpr size_t TEMPORAL_BUFFER_SIZE = 8;
    
    void update_temporal_buffer(const math::vec3& position, float dt)
    {
        // Add new position and time to the buffer
        temporal_position_buffer_.push_back(position);
        temporal_time_buffer_.push_back(dt);
        
        // Keep buffer size limited
        if(temporal_position_buffer_.size() > TEMPORAL_BUFFER_SIZE)
        {
            temporal_position_buffer_.erase(temporal_position_buffer_.begin());
            temporal_time_buffer_.erase(temporal_time_buffer_.begin());
        }
    }
    
    float calculate_smoothed_emitter_speed(float max_speed) const
    {
        // If buffer isn't filled yet, assume max speed to prevent lifetime inconsistencies
        // This ensures particles spawned early have shorter lifetimes matching later frames
        if(temporal_position_buffer_.size() < TEMPORAL_BUFFER_SIZE)
        {
            return max_speed;
        }
        
        // Calculate total distance and time over the buffer
        float total_distance = 0.0f;
        float total_time = 0.0f;
        
        for(size_t i = 1; i < temporal_position_buffer_.size(); ++i)
        {
            const math::vec3 delta = temporal_position_buffer_[i] - temporal_position_buffer_[i - 1];
            total_distance += math::length(delta);
            total_time += temporal_time_buffer_[i];
        }
        
        if(total_time > 0.0f)
        {
            return total_distance / total_time;
        }
        
        return 0.0f;
    }

    // Helper function to calculate approximate particle speed
    float calculateParticleSpeed(const Particle& particle, float ttPos) const
    {
        // Use trajectory-based approximation for better performance
        const math::vec3 initialVelocity = particle.end[0] - particle.start;
        const math::vec3 finalVelocity = particle.end[1] - particle.end[0];

        // Interpolate velocity based on position in trajectory
        const math::vec3 currentVelocity = math::mix(initialVelocity, finalVelocity, ttPos);

        // Scale by lifetime to get velocity per second
        const math::vec3 velocityPerSecond = currentVelocity * (1.0f / particle.lifeSpan);

        return math::length(velocityPerSecond);
    }

    // Calculate AABB bounds for a rotated particle with pivot offset
    // Returns the min and max offsets from particle position
    void calculateRotatedAABBBounds(const math::quat& rotation, const math::vec3& half_extents, const math::vec2& pivot,
                                     math::vec3& out_min, math::vec3& out_max) const
    {
        // Calculate pivot offset in local space
        // pivot (0,0) = bottom-left, (0.5,0.5) = center, (1,1) = top-right
        // Convert to offset: (0,0) -> (+0.5,+0.5), (0.5,0.5) -> (0,0), (1,1) -> (-0.5,-0.5)
        const math::vec2 pivot_offset = pivot - math::vec2(0.5f, 0.5f);
        const math::vec3 pivot_shift_local = math::vec3(
            pivot_offset.x * half_extents.x * 2.0f,
            pivot_offset.y * half_extents.y * 2.0f,
            0.0f
        );
        
        // For an identity quaternion, calculate AABB without rotation
        const float rot_len_sq = math::dot(rotation, rotation);
        if(rot_len_sq < 0.01f)
        {
            // AABB that contains both the extents and the pivot-shifted center
            out_min = pivot_shift_local - half_extents;
            out_max = pivot_shift_local + half_extents;
            return;
        }
        
        // Convert quaternion to rotation matrix
        const math::mat3 rot_matrix = math::mat3_cast(rotation);
        
        // Rotate the pivot shift
        const math::vec3 pivot_shift_rotated = rot_matrix * pivot_shift_local;
        
        // Calculate the AABB of the rotated box by taking absolute values
        // of the rotated basis vectors scaled by half-extents
        math::vec3 aabb_half_extents(0.0f);
        for(int i = 0; i < 3; ++i)
        {
            aabb_half_extents.x += math::abs(rot_matrix[i].x) * half_extents[i];
            aabb_half_extents.y += math::abs(rot_matrix[i].y) * half_extents[i];
            aabb_half_extents.z += math::abs(rot_matrix[i].z) * half_extents[i];
        }
        
        // Combine rotated extents with rotated pivot offset
        // The AABB needs to contain the pivot-shifted and rotated particle
        out_min = pivot_shift_rotated - aabb_half_extents;
        out_max = pivot_shift_rotated + aabb_half_extents;
    }

    // Update particle properties that were previously calculated in render
    void updateParticleProperties(EmitterUniforms& uniforms_, Particle& particle,
                                  float avgSystemScale,
                                  bx::EaseFn easePos,
                                  bool hasColorBySpeed,
                                  bool hasSizeBySpeed,
                                  const math::mat4& effectiveTransform)
    {
        const float ttPos = easePos(particle.life);

        // Calculate particle speed for speed-based effects and cache it
        const float particleSpeed  = calculateParticleSpeed(particle, ttPos);
		particle.cached_speed = particleSpeed;
        // Sample color from gradient based on particle life
        math::color sampledColor = uniforms_.m_colorGradient.sample(particle.life);

        // Apply color by speed if enabled
        if(hasColorBySpeed)
        {
            const float speedFactor =
                bx::clamp((particleSpeed - uniforms_.m_colorBySpeedVelocityRange.min) /
                              (uniforms_.m_colorBySpeedVelocityRange.max - uniforms_.m_colorBySpeedVelocityRange.min),
                          0.0f,
                          1.0f);

            const math::color speedColor = uniforms_.m_colorBySpeedGradient.sample(speedFactor);

            // Blend the speed color with the original color (multiply blend)
            sampledColor.value *= speedColor.value;
        }

        // Cache final color
        particle.color = sampledColor;
        particle.color.value.a *= uniforms_.m_opacity;

        // Calculate uniform scale with system scaling
        float scale = math::mix(particle.scale_start, particle.scale_end, particle.life) * avgSystemScale;

        // Apply size by speed if enabled
        if(hasSizeBySpeed)
        {
            const float speedFactor =
                bx::clamp((particleSpeed - uniforms_.m_sizeBySpeedVelocityRange.min) /
                              (uniforms_.m_sizeBySpeedVelocityRange.max - uniforms_.m_sizeBySpeedVelocityRange.min),
                          0.0f,
                          1.0f);

            const float sizeMultiplier =
                math::mix(uniforms_.m_sizeBySpeedRange.min, uniforms_.m_sizeBySpeedRange.max, speedFactor);
            scale *= sizeMultiplier;
        }

        // Cache final scale
        particle.scale = scale;

		// Calculate position - apply transform for local simulation
		const math::vec3 p0 = math::mix(particle.start, particle.end[0], ttPos);
		const math::vec3 p1 = math::mix(particle.end[0], particle.end[1], ttPos);
		const math::vec3 localPos = math::mix(p0, p1, ttPos);
		
		if(uniforms_.m_simulationSpace == SimulationSpace::Local)
		{
			// Transform local space position to world space
			const math::vec4 worldPos4 = effectiveTransform * math::vec4(localPos, 1.0f);
			particle.position = math::vec3(worldPos4.x, worldPos4.y, worldPos4.z);
		}
		else
		{
			// Already in world space
			particle.position = localPos;
		}
		
		// Calculate rotation if align to direction is enabled
		if(uniforms_.m_alignToDirection)
		{
			// Calculate particle velocity direction from trajectory
			// Use the derivative of the bezier curve at current position
			const math::vec3 velocity0 = particle.end[0] - particle.start;
			const math::vec3 velocity1 = particle.end[1] - particle.end[0];
			const math::vec3 current_velocity = math::mix(velocity0, velocity1, ttPos);
			
			const float velocity_len_sq = math::dot(current_velocity, current_velocity);
			if(velocity_len_sq > 0.0001f)
			{
				// Normalize velocity to get direction
				const math::vec3 direction = math::normalize(current_velocity);
				
				// Create rotation that aligns particle's forward (+Z) with velocity direction
				// Use world up as reference, but fall back to world right if velocity is nearly vertical
				math::vec3 up_ref(0.0f, 1.0f, 0.0f);
				if(math::abs(math::dot(direction, up_ref)) > 0.99f)
				{
					up_ref = math::vec3(1.0f, 0.0f, 0.0f);
				}
				
				particle.rotation = math::look_rotation(direction, up_ref);
			}
			else
			{
				// No meaningful velocity - use emitter rotation
				particle.rotation = uniforms_.m_transform.get_rotation();
			}
		}
		else
		{
			// No rotation - use identity
			particle.rotation = math::identity<math::quat>();
		}
		
		// Calculate texture sheet animation UV offset and scale
		if(uniforms_.m_texSheetCycles > 0.0f && uniforms_.m_texSheetTiles.x > 0 && uniforms_.m_texSheetTiles.y > 0)
		{
			const float uvScaleX = 1.0f / float(uniforms_.m_texSheetTiles.x);
			const float uvScaleY = 1.0f / float(uniforms_.m_texSheetTiles.y);
			const uint32_t totalFrames = uniforms_.m_texSheetTiles.x * uniforms_.m_texSheetTiles.y;
			
			// Calculate current frame based on particle life and cycles
			float animProgress = particle.life * uniforms_.m_texSheetCycles;
			
			// Add random offset if randomization is enabled
			if(uniforms_.m_texSheetRandomize)
			{
				animProgress += particle.texsheet_random_offset;
			}
			
			animProgress = math::fmod(animProgress, 1.0f);
			const uint32_t currentFrame = uint32_t(animProgress * float(totalFrames)) % totalFrames;
			
			// Calculate tile position in grid (row-major order)
			const uint32_t tileX = currentFrame % uint32_t(uniforms_.m_texSheetTiles.x);
			const uint32_t tileY = currentFrame / uint32_t(uniforms_.m_texSheetTiles.x);
			
			// Store UV offset and scale
			particle.uv_offset = math::vec2(float(tileX) * uvScaleX, float(tileY) * uvScaleY);
			particle.uv_scale = math::vec2(uvScaleX, uvScaleY);
		}
		else
		{
			// No animation - use full texture
			particle.uv_offset = math::vec2(0.0f, 0.0f);
			particle.uv_scale = math::vec2(1.0f, 1.0f);
		}
    }

    void update(EmitterUniforms* _uniforms, float _dt)
    {
		auto& uniforms_ = *_uniforms;
		
		// Cache texture mode, render mode, 3D scale, and pivot for rendering
		texture_mode_ = uniforms_.m_textureMode;
		render_mode_ = uniforms_.m_renderMode;
		particle_scale_3d_ = uniforms_.m_initialScale3D;
		pivot_ = uniforms_.m_pivot;

        if(first_update_)
        {
            uniforms_.m_prevTransform = uniforms_.m_transform;
        }

        bool was_playing = playing_;
        bool was_loop = loop_;
        playing_ = uniforms_.m_playing;
        loop_ = uniforms_.m_loop;

        if(was_playing != playing_ || was_loop != loop_)
        {
            total_particles_spawned_ = 0;
            // Reset start delay when emitter starts playing
            if(playing_ && !was_playing)
            {
                start_delay_elapsed_ = 0.0f;
            }
        }
        
        // Update start delay elapsed time if playing and not paused
        if(playing_ && !uniforms_.m_paused)
        {
            start_delay_elapsed_ += _dt;
        }

		
		if(uniforms_.m_paused)
		{
			// If paused, set delta time to 0 (particles don't advance but remain visible)
			_dt = 0.0f;
		}
		
		// Update temporal position buffer for smooth emitter speed calculation
		const math::vec3 currentPos = uniforms_.m_transform.get_position();
		update_temporal_buffer(currentPos, _dt);

        if(!uniforms_.m_loop && total_particles_spawned_ >= max_particles_)
        {
            uniforms_.m_playing = false;
            total_particles_spawned_ = 0;
        }

		// Get effective transform properties based on simulation method
		math::vec3 effectivePosition, effectiveScale, effectiveEmissionShapeScale;
		math::mat4 effectiveTransform;
		getEffectiveTransform(uniforms_, effectivePosition, effectiveScale, effectiveEmissionShapeScale, effectiveTransform);

		math::bbox aabb;
		aabb.reset();

		aabb.add_point(effectivePosition - math::vec3(0.5f));
		aabb.add_point(effectivePosition + math::vec3(0.5f));

		uint32_t num = num_particles_;

		// Pre-calculate per-frame constants to avoid recalculating per particle
		const float avgSystemScale = (effectiveScale.x + effectiveScale.y + effectiveScale.z) / 3.0f;
		const bx::EaseFn easePos = bx::getEaseFunc(uniforms_.m_easePos);

		// Pre-calculate speed-based effect conditions
		const bool hasColorBySpeed =
			(uniforms_.m_colorBySpeedVelocityRange.max > uniforms_.m_colorBySpeedVelocityRange.min);
		const bool hasSizeBySpeed =
			(uniforms_.m_sizeBySpeedVelocityRange.max > uniforms_.m_sizeBySpeedVelocityRange.min &&
			uniforms_.m_sizeBySpeedRange.min != uniforms_.m_sizeBySpeedRange.max);



		for(uint32_t ii = 0; ii < num; ++ii)
		{
			Particle& particle = particles_[ii];
			particle.life += _dt * 1.0f / particle.lifeSpan;

			if(particle.life > 1.0f)
			{
				if(ii != num - 1)
				{
					bx::memCopy(&particle, &particles_[num - 1], sizeof(Particle));
					--ii;
				}

				--num;
				continue; // Skip processing for dead particles
			}

		// Update cached properties for living particles
		updateParticleProperties(uniforms_, particle, avgSystemScale, easePos, hasColorBySpeed, hasSizeBySpeed, effectiveTransform);
		
		// Add particle position with bounds that account for rotation and pivot
		const math::vec3 local_half_extents = particle_scale_3d_ * particle.scale * 0.5f;
		math::vec3 aabb_min, aabb_max;
		calculateRotatedAABBBounds(particle.rotation, local_half_extents, pivot_, aabb_min, aabb_max);
		
		aabb.add_point(particle.position + aabb_min);
		aabb.add_point(particle.position + aabb_max);
		}   

		num_particles_ = num;

		if(0.0f < uniforms_.m_emissionLifetime && uniforms_.m_playing)
		{
			// Check if start delay has elapsed
			bool start_delay_elapsed = start_delay_elapsed_ >= uniforms_.m_startDelay;
			
			// For looping emitters, always spawn (after start delay)
			// For non-looping emitters, only spawn if initial emission hasn't completed (after start delay)
            bool initial_emission_complete = total_particles_spawned_ >= max_particles_;
			if(start_delay_elapsed && (uniforms_.m_loop || !initial_emission_complete))
			{
				spawn(uniforms_, aabb,_dt);
			}
		}

		// Safety check: ensure num_particles_ never exceeds max_particles_
		BX_ASSERT(num_particles_ <= max_particles_, "Particle count exceeded maximum! num_particles_=%d, max_particles_=%d", num_particles_, max_particles_);
		num_particles_ = math::min(num_particles_, max_particles_);
	

        if(first_update_)
        {
            first_update_ = false;
        }

		
		aabb_ = aabb;

    }

    // Helper function to get effective transform properties (now unified for both simulation methods)
    void getEffectiveTransform(const EmitterUniforms& uniforms_, 
                              math::vec3& outPosition, 
                              math::vec3& outScale, 
                              math::vec3& outEmissionShapeScale,
                              math::mat4& outTransformMatrix) const
    {
        // Extract transform components directly (efficient for both simulation methods)
        outPosition = uniforms_.m_transform.get_position();
        outScale = uniforms_.m_transform.get_scale();
        outEmissionShapeScale = uniforms_.m_emissionShapeScale; // Apply transform scale to emission shape
        outTransformMatrix = uniforms_.m_transform; // Implicit conversion to mat4
    }

    void spawn(EmitterUniforms& uniforms_, math::bbox& aabb, float _dt)
    {
        // Skip emission if rate is zero or negative
        if(uniforms_.m_particlesPerSecond <= 0.0f)
        {
            return;
        }

        // Calculate time per particle and accumulate time
        const float timePerParticle = 1.0f / uniforms_.m_particlesPerSecond;
        dt_ += _dt;

        // Calculate how many particles to emit this frame
        const uint32_t numParticlesToEmit = uint32_t(dt_ / timePerParticle);
        dt_ -= numParticlesToEmit * timePerParticle; // Remove emitted time from accumulator

        // Don't emit more particles than we have space for
        const uint32_t maxEmittable = max_particles_ - num_particles_;
        const uint32_t actualEmitCount = math::min(numParticlesToEmit, maxEmittable);

        if(actualEmitCount == 0)
        {
            return;
        }

        // Get effective transform properties based on simulation method
        math::vec3 effectivePosition, effectiveScale, effectiveEmissionShapeScale;
        math::mat4 effectiveTransform;
        getEffectiveTransform(uniforms_, effectivePosition, effectiveScale, effectiveEmissionShapeScale, effectiveTransform);

        // Pre-calculate constants for new particle property calculation
        const float avgSystemScale = (effectiveScale.x + effectiveScale.y + effectiveScale.z) / 3.0f;
        const bx::EaseFn easePos = bx::getEaseFunc(uniforms_.m_easePos);
        const bool hasColorBySpeed =
            (uniforms_.m_colorBySpeedVelocityRange.max > uniforms_.m_colorBySpeedVelocityRange.min);
        const bool hasSizeBySpeed =
            (uniforms_.m_sizeBySpeedVelocityRange.max > uniforms_.m_sizeBySpeedVelocityRange.min &&
             uniforms_.m_sizeBySpeedRange.min != uniforms_.m_sizeBySpeedRange.max);

        // Pre-calculate emitter speed for lifetime by emitter speed effect
        const bool hasLifetimeByEmitterSpeed =
            (uniforms_.m_lifetimeByEmitterSpeedRange.max > uniforms_.m_lifetimeByEmitterSpeedRange.min);
        float emitterSpeed = 0.0f;
        float lifetimeMultiplier = 1.0f;
        if(hasLifetimeByEmitterSpeed)
        {
            // Use smoothed emitter speed from temporal buffer
            // This handles physics fixed timestep discontinuities gracefully
            // Pass max speed so buffer assumes max speed until filled, preventing lifetime inconsistencies at spawn
            emitterSpeed = calculate_smoothed_emitter_speed(uniforms_.m_lifetimeByEmitterSpeedRange.max);

            // Calculate speed factor and sample gradient
            const float speedFactor = 
                bx::clamp((emitterSpeed - uniforms_.m_lifetimeByEmitterSpeedRange.min) /
                          (uniforms_.m_lifetimeByEmitterSpeedRange.max - uniforms_.m_lifetimeByEmitterSpeedRange.min),
                          0.0f, 1.0f);

            lifetimeMultiplier = uniforms_.m_lifetimeByEmitterSpeedGradient.sample(speedFactor);
        }

        // Extract rotation matrix from effective transform
        const math::mat3 rotationMatrix = math::mat3(effectiveTransform);

        // Pre-calculate common transformation components (optimization)
        const math::vec3 systemScale = effectiveScale;
        const math::vec3 emissionShapeScale = effectiveEmissionShapeScale;
        const float lifeSpan = uniforms_.m_lifetime * lifetimeMultiplier;
        const float lifeSpanSquared = lifeSpan * lifeSpan;
        math::vec3 gravityVector = math::vec3(0.0f, -9.81f * uniforms_.m_gravityScale * lifeSpanSquared, 0.0f);
        math::vec3 forceOverLifetimeVector = uniforms_.m_forceOverLifetime * lifeSpanSquared;
        
        if(uniforms_.m_simulationSpace == SimulationSpace::World)
        {
            gravityVector.y *= systemScale.y;
            forceOverLifetimeVector *= systemScale;
        }
        const float velocityDampingFactor = (1.0f - uniforms_.m_velocityDamping);

        // Calculate motion delta for temporal emission gap handling using temporal buffer
        const math::vec3 currentPos = effectivePosition;
        math::vec3 prevPos = uniforms_.m_prevTransform.get_position();
        
        // Use temporal buffer to get a better previous position estimate
        // This helps when physics doesn't update every frame
        if(temporal_position_buffer_.size() >= 2)
        {
            // Use the position from 2 frames ago for better interpolation
            const size_t prevIndex = temporal_position_buffer_.size() - 2;
            prevPos = temporal_position_buffer_[prevIndex];
        }

        const math::vec3 up = math::vec3(0.0f, 1.0f, 0.0f);

        // Emit particles with temporal interpolation
        for(uint32_t ii = 0; ii < actualEmitCount; ++ii)
        {
            // Calculate emission phase for temporal motion interpolation
            // Distribute particles evenly across the frame, scaled by temporal motion factor
            const float baseEmissionPhase = float(ii) / float(actualEmitCount);
            const float emissionPhase = baseEmissionPhase * uniforms_.m_temporalMotion;

            // Find next available particle slot
            Particle* particle = &particles_[num_particles_];
            num_particles_++;
            total_particles_spawned_++;


            math::vec3 pos;
            if(uniforms_.m_spawnLocation == EmitterSpawnLocation::Surface)
            {
                // Surface spawning
                switch(shape_)
                {
                    default:
                    case EmitterShape::Sphere:
                    {
                        // Use sphericalRand for surface of sphere
                        pos = glm::sphericalRand(1.0f);
                    }
                    break;

                    case EmitterShape::Hemisphere:
                    {
                        // Use sphericalRand and ensure Y >= 0 for hemisphere surface
                        math::vec3 spherePos = glm::sphericalRand(1.0f);
                        if(spherePos.y < 0.0f)
                            spherePos.y = -spherePos.y;
                        pos = spherePos;
                    }
                    break;

                    case EmitterShape::Circle:
                    {
                        // Use circularRand for circle perimeter
                        math::vec2 circlePos = glm::circularRand(1.0f);
                        pos = math::vec3(circlePos.x, 0.0f, circlePos.y);
                    }
                    break;

                    case EmitterShape::Box:
                    {
                        // Spawn on surface of box - randomly select a face and position on that face
                        const float face = bx::frnd(&rng_) * 6.0f; // 0-5 for 6 faces
                        const int faceIndex = static_cast<int>(face);
                        const float u = bx::frnd(&rng_) * 2.0f - 1.0f; // -1 to 1
                        const float v = bx::frnd(&rng_) * 2.0f - 1.0f; // -1 to 1
                        
                        switch(faceIndex)
                        {
                            case 0: // +X face
                                pos = math::vec3(1.0f, u, v);
                                break;
                            case 1: // -X face
                                pos = math::vec3(-1.0f, u, v);
                                break;
                            case 2: // +Y face
                                pos = math::vec3(u, 1.0f, v);
                                break;
                            case 3: // -Y face
                                pos = math::vec3(u, -1.0f, v);
                                break;
                            case 4: // +Z face
                                pos = math::vec3(u, v, 1.0f);
                                break;
                            case 5: // -Z face
                                pos = math::vec3(u, v, -1.0f);
                                break;
                            default:
                                pos = math::vec3(1.0f, u, v);
                                break;
                        }
                    }
                    break;

                    case EmitterShape::Rect:
                    {
                        // Spawn on perimeter of rectangle - randomly select an edge
                        const float edge = bx::frnd(&rng_) * 4.0f; // 0-3 for 4 edges
                        const int edgeIndex = static_cast<int>(edge);
                        const float t = bx::frnd(&rng_) * 2.0f - 1.0f; // -1 to 1 along edge
                        
                        switch(edgeIndex)
                        {
                            case 0: // Top edge (+Z)
                                pos = math::vec3(t, 0.0f, 1.0f);
                                break;
                            case 1: // Right edge (+X)
                                pos = math::vec3(1.0f, 0.0f, t);
                                break;
                            case 2: // Bottom edge (-Z)
                                pos = math::vec3(t, 0.0f, -1.0f);
                                break;
                            case 3: // Left edge (-X)
                                pos = math::vec3(-1.0f, 0.0f, t);
                                break;
                            default:
                                pos = math::vec3(t, 0.0f, 1.0f);
                                break;
                        }
                    }
                    break;
                }
            }
            else
            {
                // Inside spawning (current behavior)
                switch(shape_)
                {
                    default:
                    case EmitterShape::Sphere:
                        pos = math::ballRand(1.0f);
                        break;

                    case EmitterShape::Hemisphere:
                    {
                        math::vec3 spherePos = math::ballRand(1.0f);
                        if(math::dot(spherePos, up) < 0.0f)
                            spherePos = -spherePos;
                        pos = spherePos;
                    }
                    break;

                    case EmitterShape::Circle:
                    {
                        math::vec2 circlePos = math::diskRand(1.0f);
                        pos = math::vec3(circlePos.x, 0.0f, circlePos.y);
                    }
                    break;

                    case EmitterShape::Box:
                        pos = math::vec3(math::linearRand(-1.0f, 1.0f),
                                         math::linearRand(-1.0f, 1.0f),
                                         math::linearRand(-1.0f, 1.0f));
                        break;

                    case EmitterShape::Rect:
                        pos = math::vec3(math::linearRand(-1.0f, 1.0f), 0.0f, math::linearRand(-1.0f, 1.0f));
                        break;
                }
            }

            // Apply emission shape scale (use pre-calculated value)
            pos = (uniforms_.m_emissionShapePosition + pos) * emissionShapeScale;
            

            math::vec3 dir;
            switch(direction_)
            {
                default:
                case EmitterDirection::Up:
                    dir = up;
                    break;

                case EmitterDirection::Outward:
                    dir = math::normalize(pos);
                    break;

                case EmitterDirection::Inward:
                    dir = math::normalize(pos);
                    break;
            }

            // Use pre-calculated system scale for better performance
            math::vec3 start = pos;

            // Sample velocity range from gradient at particle end (t=1)
            const frange_t endVelocityRange = uniforms_.m_velocityGradient.sample(1.0f);
            const float endVelocity = math::mix(endVelocityRange.min, endVelocityRange.max, bx::frnd(&rng_));
            const math::vec3 scaledDir = systemScale * dir;
            const math::vec3 tmp1 = dir * endVelocity;
            math::vec3 end = tmp1 + start;

            if(direction_ == EmitterDirection::Inward)
            {
                std::swap(start, end);
                dir *= -1.0f;
            }

            particle->lifeSpan = lifeSpan;
            particle->life = 0.0f;

            // Fast-forward life so the particle starts at the time it would have been
            // emitted within this frame. This keeps temporal interpolation smooth
            // even when lifetime is scaled by emitter speed.
            // const float spawnTimeOffset = emissionPhase * _dt;
            // const float invLifeSpan = particle->lifeSpan > 0.0f ? 1.0f / particle->lifeSpan : 0.0f;
            // particle->life = bx::clamp(spawnTimeOffset * invLifeSpan, 0.0f, 1.0f);
            // Calculate interpolated emitter position for temporal emission gap handling
            math::vec3 interpolatedEmitterPos = math::mix(prevPos, currentPos, emissionPhase);

            if(uniforms_.m_simulationSpace == SimulationSpace::Local)
            {
                // For local simulation, store particles in local space (no transform applied)
                // The transform will be applied during rendering
                particle->start = start; // Local space position
                particle->end[0] = end;  // Local space end position
            }
            else
            {
                // For world simulation, apply rotation and translation as before
                particle->start = rotationMatrix * start + interpolatedEmitterPos;
                particle->end[0] = rotationMatrix * end + interpolatedEmitterPos;
            }

            // Apply damping to the velocity (use pre-calculated damping factor)
            if(uniforms_.m_velocityDamping > 0.0f)
            {
                const math::vec3 velocity = particle->end[0] - particle->start;
                const math::vec3 dampedVelocity = velocity * velocityDampingFactor;
                particle->end[0] = particle->start + dampedVelocity;
            }

            // Use pre-calculated force vectors
            const math::vec3 totalForce = gravityVector + forceOverLifetimeVector;
            particle->end[1] = particle->end[0] + totalForce;

            // Color will be sampled from gradient during rendering - no need to copy here

            // Sample scale range from gradient at particle spawn (t=0) and end (t=1)
            const frange_t startScaleRange = uniforms_.m_scaleGradient.sample(0.0f);
            const frange_t endScaleRange = uniforms_.m_scaleGradient.sample(1.0f);
            particle->scale_start = math::mix(startScaleRange.min, startScaleRange.max, bx::frnd(&rng_));
            particle->scale_end = math::mix(endScaleRange.min, endScaleRange.max, bx::frnd(&rng_));
            
            // Initialize texture sheet animation random offset
            particle->texsheet_random_offset = bx::frnd(&rng_);

            // Calculate properties immediately for new particles
            updateParticleProperties(uniforms_, *particle, avgSystemScale, easePos, hasColorBySpeed, hasSizeBySpeed, effectiveTransform);

		// Add particle position with bounds that account for rotation and pivot
		const math::vec3 local_half_extents = particle_scale_3d_ * particle->scale * 0.5f;
		math::vec3 aabb_min, aabb_max;
		calculateRotatedAABBBounds(particle->rotation, local_half_extents, pivot_, aabb_min, aabb_max);
		
		aabb.add_point(particle->position + aabb_min);
		aabb.add_point(particle->position + aabb_max);
        }

    }

    EmitterShape::Enum shape_;
    EmitterDirection::Enum direction_;

    float dt_;
    bx::RngMwc rng_;

    math::bbox aabb_;

    Particle* particles_;
    ParticleSort* particle_sort_;
    uint32_t num_particles_;
    uint32_t max_particles_;
    uint32_t total_particles_spawned_;

    bool playing_;
    bool loop_;
    float start_delay_elapsed_; // Elapsed time since emitter started playing (for start delay)

    bool first_update_; // Track if this is the first update to avoid interpolation
    
    // Temporal position buffer for smooth emitter speed calculation
    std::vector<math::vec3> temporal_position_buffer_;
    std::vector<float> temporal_time_buffer_;
    
    // Cached texture mode for rendering (determines which shader to use)
    TextureMode::Enum texture_mode_;
    
    // Cached render mode for rendering
    RenderMode::Enum render_mode_;
    
    // Cached billboard vectors (calculated from render mode and camera)
    math::vec3 billboard_right_;
    math::vec3 billboard_up_;
    
    // Cached 3D particle scale (from uniforms, applied to all particles)
    math::vec3 particle_scale_3d_;
    
    // Cached pivot point (from uniforms, applied to all particles)
    math::vec2 pivot_;
};

static int32_t particleSortFn(const void* _lhs, const void* _rhs)
{
    const ParticleSort& lhs = *(const ParticleSort*)_lhs;
    const ParticleSort& rhs = *(const ParticleSort*)_rhs;
    return lhs.dist > rhs.dist ? -1 : 1;
}

struct ParticleSystem
{
    void init(uint16_t _maxEmitters, bx::AllocatorI* _allocator)
    {
        m_allocator = _allocator;

        if(nullptr == _allocator)
        {
            static bx::DefaultAllocator allocator;
            m_allocator = &allocator;
        }

        m_emitterAlloc = bx::createHandleAlloc(m_allocator, _maxEmitters);
        m_emitter.resize(_maxEmitters);

        // Initialize vertex layouts
        ParticleVertex::init();

        // Create static quad geometry for instanced rendering
        m_quadVBH = bgfx::createVertexBuffer(
            bgfx::makeRef(s_quadVertices, sizeof(s_quadVertices)),
            ParticleVertex::ms_layout
        );

        m_quadIBH = bgfx::createIndexBuffer(
            bgfx::makeRef(s_quadIndices, sizeof(s_quadIndices))
        );

        s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        u_billboardRight = bgfx::createUniform("u_billboardRight", bgfx::UniformType::Vec4);
        u_billboardUp = bgfx::createUniform("u_billboardUp", bgfx::UniformType::Vec4);
        u_eyePos = bgfx::createUniform("u_eyePos", bgfx::UniformType::Vec4);
    }

    void shutdown()
    {
        bgfx::destroy(s_texColor);
        bgfx::destroy(u_billboardRight);
        bgfx::destroy(u_billboardUp);
        bgfx::destroy(u_eyePos);
        bgfx::destroy(m_quadVBH);
        bgfx::destroy(m_quadIBH);

        bx::destroyHandleAlloc(m_allocator, m_emitterAlloc);
        // bx::free(m_allocator, m_emitter);

        m_allocator = nullptr;
    }
    
    // Calculate billboard vectors based on render mode and view matrix
    void calculateBillboardVectors(RenderMode::Enum renderMode, const float* mtxView, math::vec3& outRight, math::vec3& outUp)
    {

        // Extract camera vectors from view matrix (column-major order)
        // View matrix columns represent right, up, forward, position
        math::vec3 cameraRight(mtxView[0], mtxView[4], mtxView[8]);
        math::vec3 cameraUp(mtxView[1], mtxView[5], mtxView[9]);
        math::vec3 cameraForward(mtxView[2], mtxView[6], mtxView[10]);
        
        if(renderMode == RenderMode::HorizontalBillboard)
        {
            // Horizontal billboard: rotate around Y axis only, stay horizontal (parallel to ground)
            // Remove Y component from camera right and normalize, use world up for vertical
            math::vec3 worldUp(0.0f, 0.0f, 1.0f);
            math::vec3 rightNoY(1.0f, 0.0f, 0.0f);

            outRight = rightNoY;
            outUp = worldUp;
        }
        else if(renderMode == RenderMode::VerticalBillboard)
        {
            // Vertical billboard: particles stay vertical (upright), rotate around Y axis to face camera
            // This is what the current "horizontal" implementation does - it's correct for vertical
            // Right vector: camera right projected to XZ plane (removes Y component)
            // Up vector: always world up (0, 1, 0) to keep particles vertical/upright
            math::vec3 worldUp(0.0f, 1.0f, 0.0f);
            math::vec3 rightNoY = math::normalize(math::vec3(cameraRight.x, 0.0f, cameraRight.z));
            outRight = rightNoY;
            outUp = worldUp;
        }
        else // RenderMode::Billboard (default)
        {
            // Standard billboard: always face camera (full 3D rotation)
            outRight = math::normalize(cameraRight);
            outUp = math::normalize(cameraUp);
        }
    }

    // Batch rendering support structures and functions
    struct BatchedParticle
    {
        float dist; // Squared distance from camera for sorting
        uint32_t emitter_idx; // Which emitter this particle belongs to
        uint32_t particle_idx; // Index within that emitter's particle array
    };

    static int32_t batchedParticleSortFn(const void* _lhs, const void* _rhs)
    {
        const BatchedParticle& lhs = *(const BatchedParticle*)_lhs;
        const BatchedParticle& rhs = *(const BatchedParticle*)_rhs;
        // Sort by squared distance (back to front for proper alpha blending)
        return lhs.dist > rhs.dist ? -1 : 1;
    }

    uint32_t renderEmitterBatch(const EmitterHandle* _handles, uint32_t _count, 
                           uint8_t _view, bgfx::ProgramHandle _programMultiChannel,
                           bgfx::ProgramHandle _programMask,
                           const float* _mtxView, const math::vec3& _eye, 
                           bgfx::TextureHandle _texture)
    {
        if(_count == 0 || !bgfx::isValid(_texture))
        {
            return 0; // Nothing to render
        }

		APP_SCOPE_PERF("Rendering/Particle Pass/Render Batched Emitters");

        // Separate emitters by texture mode for batching
        std::vector<EmitterHandle> multiChannelEmitters;
        std::vector<EmitterHandle> maskEmitters;
        
        uint32_t totalParticles = 0;
        
        for(uint32_t i = 0; i < _count; ++i)
        {
            if(!isValid(_handles[i]))
            {
                continue;
            }
            
            const Emitter& emitter = m_emitter[_handles[i].idx];
            
            // Use cached texture mode to group emitters
            if(emitter.texture_mode_ == TextureMode::Mask)
            {
                maskEmitters.push_back(_handles[i]);
            }
            else
            {
                multiChannelEmitters.push_back(_handles[i]);
            }
            
            totalParticles += emitter.num_particles_;
        }

        if(totalParticles == 0)
        {
            return 0; // No particles to render
        }

        uint32_t renderedParticles = 0;
        
        // Render MultiChannel emitters
        if(!multiChannelEmitters.empty())
        {
            renderedParticles += renderEmitterBatchByMode(
                multiChannelEmitters.data(), 
                static_cast<uint32_t>(multiChannelEmitters.size()),
                _view, _programMultiChannel, _mtxView, _eye, _texture
            );
        }
        
        // Render Mask emitters
        if(!maskEmitters.empty())
        {
            renderedParticles += renderEmitterBatchByMode(
                maskEmitters.data(), 
                static_cast<uint32_t>(maskEmitters.size()),
                _view, _programMask, _mtxView, _eye, _texture
            );
        }

		return renderedParticles;
    }
    
    uint32_t renderEmitterBatchByMode(const EmitterHandle* _handles, uint32_t _count, 
                           uint8_t _view, bgfx::ProgramHandle _program, 
                           const float* _mtxView, const math::vec3& _eye, 
                           bgfx::TextureHandle _texture)
    {
        // Count total particles for this batch
        uint32_t totalParticles = 0;
        for(uint32_t i = 0; i < _count; ++i)
        {
            const Emitter& emitter = m_emitter[_handles[i].idx];
            totalParticles += emitter.num_particles_;
        }

        if(totalParticles == 0)
        {
            return 0;
        }

        // Use instanced rendering for the batch
        // Instance data layout (80 bytes total):
        // i_data0: vec4 (position.xyz, unused)           - 16 bytes
        // i_data1: vec4 (rotation quaternion xyzw)        - 16 bytes
        // i_data2: vec4 (scale3d.xyz, unused)              - 16 bytes
        // i_data3: vec4 (uvOffset.xy, uvScale.xy)         - 16 bytes
        // i_data4: vec4 (color.rgba)                       - 16 bytes
        const uint16_t instanceStride = 80; // 80 bytes per instance (5 * 16 bytes)
        
        // Get available instance buffer space
        uint32_t maxInstances = bgfx::getAvailInstanceDataBuffer(totalParticles, instanceStride);
        
        if(maxInstances == 0)
        {
            BX_WARN(false, "No instance buffer space available for batch rendering.");
            return 0;
        }
        
        // Allocate instance data buffer
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, maxInstances, instanceStride);
        
        // Generate batched instance data
        generateBatchedInstanceData(_handles, _count, idb, maxInstances, instanceStride, _eye);
        
        // Set static quad geometry
        bgfx::setVertexBuffer(0, m_quadVBH);
        bgfx::setIndexBuffer(m_quadIBH);
        
        // Set instance data
        bgfx::setInstanceDataBuffer(&idb);
        
        // Calculate billboard vectors based on first emitter's render mode (for batch)
        math::vec3 billboardRight, billboardUp;
        if(_count > 0)
        {
            const Emitter& firstEmitter = m_emitter[_handles[0].idx];
            calculateBillboardVectors(firstEmitter.render_mode_, _mtxView, billboardRight, billboardUp);
        }
        else
        {
            // Fallback to standard billboard
            billboardRight = math::vec3(1.0f, 0.0f, 0.0f);
            billboardUp = math::vec3(0.0f, 1.0f, 0.0f);
        }
        
        // Set render state and texture
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | 
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       BGFX_STATE_BLEND_NORMAL);
        bgfx::setTexture(0, s_texColor, _texture);
        
        // Set billboard vectors as uniforms (vec4 for alignment)
        float billboardRightVec4[4] = { billboardRight.x, billboardRight.y, billboardRight.z, 0.0f };
        float billboardUpVec4[4] = { billboardUp.x, billboardUp.y, billboardUp.z, 0.0f };
        bgfx::setUniform(u_billboardRight, billboardRightVec4);
        bgfx::setUniform(u_billboardUp, billboardUpVec4);
        
        // Set eye position with render mode in w component
        // w = 0: Billboard, w = 1: HorizontalBillboard, w = 2: VerticalBillboard
        RenderMode::Enum renderMode = RenderMode::Billboard;
        if(_count > 0)
        {
            renderMode = m_emitter[_handles[0].idx].render_mode_;
        }
        float eyePosVec4[4] = { _eye.x, _eye.y, _eye.z, float(renderMode) };
        bgfx::setUniform(u_eyePos, eyePosVec4);
        
        // Single draw call for all particles from all emitters!
        bgfx::submit(_view, _program);

		return totalParticles;
    }

    void generateBatchedInstanceData(const EmitterHandle* _handles, uint32_t _count,
                                   bgfx::InstanceDataBuffer& idb, uint32_t maxInstances, 
                                   uint16_t instanceStride, const math::vec3& _eye)
    {
        // First, collect all particles from all emitters and calculate distances
        static std::vector<BatchedParticle> batchedParticles; // Static to avoid allocations
        batchedParticles.clear();
        
        for(uint32_t emitterIdx = 0; emitterIdx < _count; ++emitterIdx)
        {
            if(!isValid(_handles[emitterIdx]))
            {
                continue;
            }
            
            const Emitter& emitter = m_emitter[_handles[emitterIdx].idx];
            
			batchedParticles.reserve(batchedParticles.size() + emitter.num_particles_);
            for(uint32_t particleIdx = 0; particleIdx < emitter.num_particles_; ++particleIdx)
            {
                const Particle& particle = emitter.particles_[particleIdx];
                const math::vec3 tmp0 = _eye - particle.position;
                const float distSquared = math::dot(tmp0, tmp0);
                
                batchedParticles.emplace_back(BatchedParticle{distSquared, emitterIdx, particleIdx});
            }
        }
        
        // Sort all particles by distance (back to front for alpha blending)
        std::sort(batchedParticles.begin(), batchedParticles.end(), 
                 [](const BatchedParticle& a, const BatchedParticle& b) {
                     return a.dist > b.dist; // Back to front
                 });
        
        // Generate instance data for sorted particles
        uint8_t* data = idb.data;
        uint32_t numToRender = math::min(static_cast<uint32_t>(batchedParticles.size()), maxInstances);
        
        for(uint32_t i = 0; i < numToRender; ++i)
        {
            const BatchedParticle& batchedParticle = batchedParticles[i];
            const Emitter& emitter = m_emitter[_handles[batchedParticle.emitter_idx].idx];
            const Particle& particle = emitter.particles_[batchedParticle.particle_idx];
            
            // Position (16 bytes) - i_data0
            float* pos = (float*)data;
            pos[0] = particle.position.x;
            pos[1] = particle.position.y;
            pos[2] = particle.position.z;
            pos[3] = emitter.pivot_.x; // Pivot X
            
            // Rotation quaternion (16 bytes) - i_data1
            float* rot = (float*)&data[16];
            rot[0] = particle.rotation.x;
            rot[1] = particle.rotation.y;
            rot[2] = particle.rotation.z;
            rot[3] = particle.rotation.w;
            
            // 3D Scale (16 bytes) - i_data2
            // Multiply uniform scale by cached 3D scale from emitter
            float* scale3d = (float*)&data[32];
            scale3d[0] = particle.scale * emitter.particle_scale_3d_.x;
            scale3d[1] = particle.scale * emitter.particle_scale_3d_.y;
            scale3d[2] = particle.scale * emitter.particle_scale_3d_.z;
            scale3d[3] = emitter.pivot_.y; // Pivot Y
            
            // UV Offset + UV Scale (16 bytes) - i_data3
            float* uvData = (float*)&data[48];
            uvData[0] = particle.uv_offset.x; // UV offset X
            uvData[1] = particle.uv_offset.y; // UV offset Y
            uvData[2] = particle.uv_scale.x;  // UV scale X
            uvData[3] = particle.uv_scale.y;  // UV scale Y
            
            // Color (16 bytes) - i_data4
            float* color = (float*)&data[64];
            color[0] = particle.color.value.r;
            color[1] = particle.color.value.g;
            color[2] = particle.color.value.b;
            color[3] = particle.color.value.a;
            
            data += instanceStride;
        }
    }

    EmitterHandle createEmitter(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles)
    {
        EmitterHandle handle = {m_emitterAlloc->alloc()};

        if(UINT16_MAX != handle.idx)
        {
            m_emitter[handle.idx].create(_shape, _direction, _maxParticles);
        }

        return handle;
    }

    void updateEmitter(EmitterHandle _handle, float _dt, EmitterUniforms* _uniforms)
    {
        BX_ASSERT(isValid(_handle), "destroyEmitter handle %d is not valid.", _handle.idx);

        Emitter& emitter = m_emitter[_handle.idx];

        if(nullptr == _uniforms)
        {
            emitter.reset();
        }
        else
        {

			emitter.update(_uniforms, _dt);
        }
    }

    void getAabb(EmitterHandle _handle, math::bbox& _outAabb)
    {
        BX_ASSERT(isValid(_handle), "getAabb handle %d is not valid.", _handle.idx);
        _outAabb = m_emitter[_handle.idx].aabb_;
    }
    uint32_t getNumParticles(EmitterHandle _handle)
    {
        BX_ASSERT(isValid(_handle), "getNumParticles handle %d is not valid.", _handle.idx);
        return m_emitter[_handle.idx].num_particles_;
    }

    bool hasUpdated(EmitterHandle _handle)
    {
        BX_ASSERT(isValid(_handle), "hasUpdated handle %d is not valid.", _handle.idx);
        return !m_emitter[_handle.idx].first_update_;
    }

    void destroyEmitter(EmitterHandle _handle)
    {
        BX_ASSERT(isValid(_handle), "destroyEmitter handle %d is not valid.", _handle.idx);

        m_emitter[_handle.idx].destroy();
        m_emitterAlloc->free(_handle.idx);
    }

    bx::AllocatorI* m_allocator;

    bx::HandleAlloc* m_emitterAlloc;
    std::vector<Emitter> m_emitter;

    // Static geometry for instanced rendering
    bgfx::VertexBufferHandle m_quadVBH;
    bgfx::IndexBufferHandle m_quadIBH;

    bgfx::UniformHandle s_texColor;
    bgfx::UniformHandle u_billboardRight;
    bgfx::UniformHandle u_billboardUp;
    bgfx::UniformHandle u_eyePos;
};

static ParticleSystem s_ctx;

void Emitter::create(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles)
{
    reset();

    shape_ = _shape;
    direction_ = _direction;
    max_particles_ = _maxParticles;
    particles_ = (Particle*)bx::alloc(s_ctx.m_allocator, max_particles_ * sizeof(Particle));
    particle_sort_ = (ParticleSort*)bx::alloc(s_ctx.m_allocator, max_particles_ * sizeof(ParticleSort));
}

void Emitter::destroy()
{
    bx::free(s_ctx.m_allocator, particles_);
    particles_ = nullptr;
    bx::free(s_ctx.m_allocator, particle_sort_);
    particle_sort_ = nullptr;
}

} // namespace ps

using namespace ps;

void psInit(uint16_t _maxEmitters, bx::AllocatorI* _allocator)
{
    s_ctx.init(_maxEmitters, _allocator);
}

void psShutdown()
{
    s_ctx.shutdown();
}

// Sprite functions removed - use bgfx::TextureHandle directly in EmitterUniforms

EmitterHandle psCreateEmitter(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles)
{
    return s_ctx.createEmitter(_shape, _direction, _maxParticles);
}

void psUpdateEmitter(EmitterHandle _handle, float _dt, EmitterUniforms* _uniforms)
{
    s_ctx.updateEmitter(_handle, _dt, _uniforms);
}

void psResetEmitter(EmitterHandle _handle)
{
    BX_ASSERT(isValid(_handle), "psResetEmitter handle %d is not valid.", _handle.idx);

    s_ctx.m_emitter[_handle.idx].reset();
}

void psGetAabb(EmitterHandle _handle, math::bbox& _outAabb)
{
    s_ctx.getAabb(_handle, _outAabb);
}

uint32_t psGetNumParticles(EmitterHandle _handle)
{
    return s_ctx.getNumParticles(_handle);
}

bool psHasUpdated(EmitterHandle _handle)
{
    return s_ctx.hasUpdated(_handle);
}

void psDestroyEmitter(EmitterHandle _handle)
{
    s_ctx.destroyEmitter(_handle);
}

uint32_t psRenderEmitterBatch(const EmitterHandle* _handles,
                         uint32_t _count,
                         uint8_t _view,
                         bgfx::ProgramHandle _programMultiChannel,
                         bgfx::ProgramHandle _programMask,
                         const float* _mtxView,
                         const math::vec3& _eye,
                         bgfx::TextureHandle _texture)
{
    return s_ctx.renderEmitterBatch(_handles, _count, _view, _programMultiChannel, _programMask, _mtxView, _eye, _texture);
}
