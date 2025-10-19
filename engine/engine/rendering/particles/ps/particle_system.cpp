/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>

#include "particle_system.h"
#include <graphics/utils/bgfx_utils.h>

#include <bx/easing.h>
#include <bx/handlealloc.h>
#include <math/math.h>
#include <vector>
#include <algorithm>
#include <engine/profiler/profiler.h>

struct PosColorTexCoord0Vertex
{
    float x;
    float y;
    float z;
    uint32_t abgr;
    float u;
    float v;
    float blend;
    float angle;

    static void init()
    {
        ms_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosColorTexCoord0Vertex::ms_layout;

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
    m_position = math::vec3(0.0f, 0.0f, 0.0f);
    m_angle = math::vec3(0.0f, 0.0f, 0.0f);
    m_prevPosition = math::vec3(0.0f, 0.0f, 0.0f);
    m_emissionShapeScale = math::vec3(1.0f, 1.0f, 1.0f); // Default: no scaling

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

    // Initialize blend gradient with default 2-point gradient (start -> end)
    m_blendGradient = math::gradient<frange_t>();
    m_blendGradient.add_point(frange_t(0.8f, 1.0f), 0.0f); // Start blend range
    m_blendGradient.add_point(frange_t(0.0f, 0.2f), 1.0f); // End blend range

    // Initialize scale gradient with default 2-point gradient (start -> end)
    m_scaleGradient = math::gradient<frange_t>();
    m_scaleGradient.add_point(frange_t(0.1f, 0.2f), 0.0f); // Start scale range
    m_scaleGradient.add_point(frange_t(0.3f, 0.4f), 1.0f); // End scale range

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
    m_scale = math::vec3(1.0f, 1.0f, 1.0f);                          // Default: no scaling

    m_emissionLifetime = 2.0f; // Default: 2 second emission cycle
    m_blendMultiplier = 1.0f;  // Default: no blend modification

    m_easePos = bx::Easing::Linear; // Only position easing remains
    // Generate LUTs for all gradients to optimize sampling performance
    m_velocityGradient.generate_lut(256);
    m_colorGradient.generate_lut(256);
    m_blendGradient.generate_lut(256);
    m_scaleGradient.generate_lut(256);
    m_colorBySpeedGradient.generate_lut(256);
}

namespace ps
{
struct Particle
{
    math::vec3 start;
    math::vec3 end[2];
    float blend_start;
    float blend_end;
    float scale_start;
    float scale_end;

    // Cached computed properties (updated during update, used during render)
    math::color color; // Final color with all effects applied
	math::vec3 position;
    float scale;    // Final scale with all effects applied
    float blend;    // Final blend value
    float cached_speed; // Cached particle speed to avoid redundant calculations

    float life;
    float lifeSpan;
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
        aabb_ = math::bbox(math::vec3(-1.0f), math::vec3(1.0f));
        first_update_ = true;

        rng_.reset();
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

    // Update particle properties that were previously calculated in render
    void updateParticleProperties(EmitterUniforms& uniforms_, Particle& particle,
                                  float avgSystemScale,
                                  bx::EaseFn easePos,
                                  bool hasColorBySpeed,
                                  bool hasSizeBySpeed)
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

        // Calculate blend and apply global blend multiplier
        particle.blend = math::mix(particle.blend_start, particle.blend_end, particle.life) * uniforms_.m_blendMultiplier;

        // Calculate scale with system scaling
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

		// Calculate position (this still needs to be done in render for sorting)
		const math::vec3 p0 = math::mix(particle.start, particle.end[0], ttPos);
		const math::vec3 p1 = math::mix(particle.end[0], particle.end[1], ttPos);
		const math::vec3 pos = math::mix(p0, p1, ttPos);
		particle.position = pos;
    }

    void update(EmitterUniforms* _uniforms, float _dt)
    {
		auto& uniforms_ = *_uniforms;
        // Pre-calculate per-frame constants to avoid recalculating per particle
        const float avgSystemScale = (uniforms_.m_scale.x + uniforms_.m_scale.y + uniforms_.m_scale.z) / 3.0f;
        const bx::EaseFn easePos = bx::getEaseFunc(uniforms_.m_easePos);

        // Pre-calculate speed-based effect conditions
        const bool hasColorBySpeed =
            (uniforms_.m_colorBySpeedVelocityRange.max > uniforms_.m_colorBySpeedVelocityRange.min);
        const bool hasSizeBySpeed =
            (uniforms_.m_sizeBySpeedVelocityRange.max > uniforms_.m_sizeBySpeedVelocityRange.min &&
             uniforms_.m_sizeBySpeedRange.min != uniforms_.m_sizeBySpeedRange.max);


        uint32_t num = num_particles_;

		math::bbox aabb;
		aabb.reset();

		aabb.add_point(uniforms_.m_position - math::vec3(0.5f));
		aabb.add_point(uniforms_.m_position + math::vec3(0.5f));

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
            updateParticleProperties(uniforms_, particle, avgSystemScale, easePos, hasColorBySpeed, hasSizeBySpeed);
			
			// Add particle position with some padding for scale
			math::vec3 padding(particle.scale * 0.5f);
			aabb.add_point(particle.position - padding);
			aabb.add_point(particle.position + padding);
        }

		aabb_ = aabb;

        num_particles_ = num;

        if(0.0f < uniforms_.m_emissionLifetime)
        {
            spawn(uniforms_, _dt);
        }

        // Safety check: ensure num_particles_ never exceeds max_particles_
        BX_ASSERT(num_particles_ <= max_particles_, "Particle count exceeded maximum! num_particles_=%d, max_particles_=%d", num_particles_, max_particles_);
        num_particles_ = math::min(num_particles_, max_particles_);

        if(first_update_)
        {
            first_update_ = false;
        }
    }

    void spawn(EmitterUniforms& uniforms_, float _dt)
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

        // Pre-calculate constants for new particle property calculation
        const float avgSystemScale = (uniforms_.m_scale.x + uniforms_.m_scale.y + uniforms_.m_scale.z) / 3.0f;
        const bx::EaseFn easePos = bx::getEaseFunc(uniforms_.m_easePos);
        const bool hasColorBySpeed =
            (uniforms_.m_colorBySpeedVelocityRange.max > uniforms_.m_colorBySpeedVelocityRange.min);
        const bool hasSizeBySpeed =
            (uniforms_.m_sizeBySpeedVelocityRange.max > uniforms_.m_sizeBySpeedVelocityRange.min &&
             uniforms_.m_sizeBySpeedRange.min != uniforms_.m_sizeBySpeedRange.max);

        // Pre-calculate rotation matrix (constant for all particles in this spawn call)
        const math::quat rotationQuat = math::angleAxis(uniforms_.m_angle.z, math::vec3(0, 0, 1)) *
                                        math::angleAxis(uniforms_.m_angle.y, math::vec3(0, 1, 0)) *
                                        math::angleAxis(uniforms_.m_angle.x, math::vec3(1, 0, 0));
        const math::mat3 rotationMatrix = math::mat3_cast(rotationQuat);

        // Pre-calculate common transformation components (optimization)
        const math::vec3 systemScale = uniforms_.m_scale;
        const math::vec3 emissionShapeScale = uniforms_.m_emissionShapeScale;
        const float lifeSpan = uniforms_.m_lifetime;
        const float lifeSpanSquared = lifeSpan * lifeSpan;
        const math::vec3 gravityVector = math::vec3(0.0f, -9.81f * uniforms_.m_gravityScale * lifeSpanSquared * systemScale.y, 0.0f);
        const math::vec3 forceOverLifetimeVector = uniforms_.m_forceOverLifetime * lifeSpanSquared * systemScale;
        const float velocityDampingFactor = (1.0f - uniforms_.m_velocityDamping);

        // Calculate motion delta for temporal emission gap handling
        const math::vec3 currentPos = uniforms_.m_position;
        const math::vec3 prevPos = uniforms_.m_prevPosition;

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

            math::vec3 pos;
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

            // Apply emission shape scale (use pre-calculated value)
            pos = pos * emissionShapeScale;

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
            }

            // Use pre-calculated system scale for better performance
            const math::vec3 scaledPos = systemScale * pos;
            const math::vec3 start = scaledPos;

            // Sample velocity range from gradient at particle end (t=1)
            const frange_t endVelocityRange = uniforms_.m_velocityGradient.sample(1.0f);
            const float endVelocity = math::mix(endVelocityRange.min, endVelocityRange.max, bx::frnd(&rng_));
            const math::vec3 scaledDir = systemScale * dir;
            const math::vec3 tmp1 = scaledDir * endVelocity;
            const math::vec3 end = tmp1 + start;

            particle->life = 0.0f; // Always start at 0 for new particles
            particle->lifeSpan = lifeSpan; // Use pre-calculated value

            // Calculate interpolated emitter position for temporal emission gap handling
            math::vec3 interpolatedEmitterPos = math::mix(prevPos, currentPos, emissionPhase);

            // Apply rotation first, then translation (more efficient than full matrix)
            particle->start = rotationMatrix * start + interpolatedEmitterPos;
            particle->end[0] = rotationMatrix * end + interpolatedEmitterPos;

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

            // Sample blend range from gradient at particle spawn (t=0) and end (t=1)
            const frange_t startBlendRange = uniforms_.m_blendGradient.sample(0.0f);
            const frange_t endBlendRange = uniforms_.m_blendGradient.sample(1.0f);
            particle->blend_start = math::mix(startBlendRange.min, startBlendRange.max, bx::frnd(&rng_));
            particle->blend_end = math::mix(endBlendRange.min, endBlendRange.max, bx::frnd(&rng_));

            // Sample scale range from gradient at particle spawn (t=0) and end (t=1)
            const frange_t startScaleRange = uniforms_.m_scaleGradient.sample(0.0f);
            const frange_t endScaleRange = uniforms_.m_scaleGradient.sample(1.0f);
            particle->scale_start = math::mix(startScaleRange.min, startScaleRange.max, bx::frnd(&rng_));
            particle->scale_end = math::mix(endScaleRange.min, endScaleRange.max, bx::frnd(&rng_));

            // Calculate properties immediately for new particles
            updateParticleProperties(uniforms_, *particle, avgSystemScale, easePos, hasColorBySpeed, hasSizeBySpeed);

			// Add particle position with some padding for scale
			math::vec3 padding(particle->scale * 0.5f);
			aabb_.add_point(particle->position - padding);
			aabb_.add_point(particle->position + padding);
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

    bool first_update_; // Track if this is the first update to avoid interpolation
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
        PosColorTexCoord0Vertex::init();
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
    }

    void shutdown()
    {
        bgfx::destroy(s_texColor);
        bgfx::destroy(m_quadVBH);
        bgfx::destroy(m_quadIBH);

        bx::destroyHandleAlloc(m_allocator, m_emitterAlloc);
        // bx::free(m_allocator, m_emitter);

        m_allocator = nullptr;
    }


    void renderEmitterById(EmitterHandle _handle,
                           uint8_t _view,
                           bgfx::ProgramHandle _program,
                           const float* _mtxView,
                           const math::vec3& _eye,
						   bgfx::TextureHandle _texture)
    {
        BX_ASSERT(isValid(_handle), "renderEmitterById handle %d is not valid.", _handle.idx);

        Emitter& emitter = m_emitter[_handle.idx];

        if(0 == emitter.num_particles_ || !bgfx::isValid(_texture))
        {
            return; // Skip emitters with no particles or invalid texture
        }

        // Use instanced rendering - much more efficient!
        const uint16_t instanceStride = 32; // 32 bytes per instance
        
        // Get available instance buffer space
        uint32_t maxInstances = bgfx::getAvailInstanceDataBuffer(emitter.num_particles_, instanceStride);
        
        if(maxInstances == 0)
        {
            BX_WARN(false, "No instance buffer space available.");
            return;
        }
        
        // Allocate instance data buffer
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, maxInstances, instanceStride);
        
        // Generate instance data
        generateInstanceData(emitter, idb, maxInstances, instanceStride, _eye);
        
        // Set static quad geometry
        bgfx::setVertexBuffer(0, m_quadVBH);
        bgfx::setIndexBuffer(m_quadIBH);
        
        // Set instance data
        bgfx::setInstanceDataBuffer(&idb);
        
        // Set render state and texture
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | 
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       BGFX_STATE_BLEND_NORMAL);
        bgfx::setTexture(0, s_texColor, _texture);
        
        // Single draw call for all particles!
        bgfx::submit(_view, _program);
    }

    void generateInstanceData(Emitter& emitter, bgfx::InstanceDataBuffer& idb, uint32_t maxInstances, uint16_t instanceStride, const math::vec3& _eye)
    {
        uint8_t* data = idb.data;
        
        // Sort particles for proper alpha blending (simplified - just by distance)
        for(uint32_t i = 0; i < emitter.num_particles_; ++i)
        {
            const Particle& particle = emitter.particles_[i];
            const math::vec3 tmp0 = _eye - particle.position;
            emitter.particle_sort_[i].dist = math::dot(tmp0, tmp0);
            emitter.particle_sort_[i].idx = i;
        }
        
        // Sort by distance (back to front for alpha blending)
        qsort(emitter.particle_sort_, emitter.num_particles_, sizeof(ParticleSort), particleSortFn);
        
        // Generate instance data for sorted particles
        uint32_t numToRender = math::min(emitter.num_particles_, maxInstances);
        for(uint32_t i = 0; i < numToRender; ++i)
        {
            const ParticleSort& sort = emitter.particle_sort_[i];
            const Particle& particle = emitter.particles_[sort.idx];
            
            // Position + Scale (16 bytes)
            float* posScale = (float*)data;
            posScale[0] = particle.position.x;
            posScale[1] = particle.position.y;
            posScale[2] = particle.position.z;
            posScale[3] = particle.scale;
            
            // Color + Blend + Angle + Padding (16 bytes)
            float* colorBlend = (float*)&data[16];
            colorBlend[0] = particle.color.value.r;
            colorBlend[1] = particle.color.value.g;
            colorBlend[2] = particle.color.value.b;
            colorBlend[3] = particle.color.value.a;
            
            // Speed + Padding (8 bytes)
            float* rotationBlend = (float*)&data[32];
			rotationBlend[0] = 0.0f; // angle (could add rotation later)
			rotationBlend[1] = particle.blend;
            rotationBlend[2] = 0.0f; 
            rotationBlend[3] = 0.0f; // padding
            
            data += instanceStride;
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
                           uint8_t _view, bgfx::ProgramHandle _program, 
                           const float* _mtxView, const math::vec3& _eye, 
                           bgfx::TextureHandle _texture)
    {
        if(_count == 0 || !bgfx::isValid(_texture))
        {
            return 0; // Nothing to render
        }

		APP_SCOPE_PERF("Rendering/Particle Pass/Render Batched Emitters");


        // Count total particles across all emitters
        uint32_t totalParticles = 0;
        for(uint32_t i = 0; i < _count; ++i)
        {
            if(!isValid(_handles[i]))
            {
                continue;
            }
            
            const Emitter& emitter = m_emitter[_handles[i].idx];
            totalParticles += emitter.num_particles_;
        }

        if(totalParticles == 0)
        {
            return 0; // No particles to render
        }

        // Use instanced rendering for the batch
        const uint16_t instanceStride = 48; // 48 bytes per instance
        
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
        
        // Set render state and texture
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | 
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       BGFX_STATE_BLEND_NORMAL);
        bgfx::setTexture(0, s_texColor, _texture);
        
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
                
                batchedParticles.emplace_back(distSquared, emitterIdx, particleIdx);
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
            
            // Position + Scale (16 bytes)
            float* posScale = (float*)data;
            posScale[0] = particle.position.x;
            posScale[1] = particle.position.y;
            posScale[2] = particle.position.z;
            posScale[3] = particle.scale;
            
            // Color + Blend + Angle + Padding (16 bytes)
            float* colorBlend = (float*)&data[16];
            colorBlend[0] = particle.color.value.r;
            colorBlend[1] = particle.color.value.g;
            colorBlend[2] = particle.color.value.b;
            colorBlend[3] = particle.color.value.a;
            
            // Speed + Padding (8 bytes)
            float* rotationBlend = (float*)&data[32];
			rotationBlend[0] = 0.0f; // angle (could add rotation later)
			rotationBlend[1] = particle.blend;
            rotationBlend[2] = 0.0f; 
            rotationBlend[3] = 0.0f; // padding
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
            if(emitter.first_update_)
            {
                _uniforms->m_prevPosition = _uniforms->m_position;
            }

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

void psRenderEmitter(EmitterHandle _handle,
                     uint8_t _view,
                     bgfx::ProgramHandle _program,
                     const float* _mtxView,
                     const math::vec3& _eye,
                     bgfx::TextureHandle _texture)
{
    s_ctx.renderEmitterById(_handle, _view, _program, _mtxView, _eye, _texture);
}

uint32_t psRenderEmitterBatch(const EmitterHandle* _handles,
                         uint32_t _count,
                         uint8_t _view,
                         bgfx::ProgramHandle _program,
                         const float* _mtxView,
                         const math::vec3& _eye,
                         bgfx::TextureHandle _texture)
{
    return s_ctx.renderEmitterBatch(_handles, _count, _view, _program, _mtxView, _eye, _texture);
}
