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
    uint32_t color; // Final color with all effects applied
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

        // Calculate blend
        particle.blend = math::mix(particle.blend_start, particle.blend_end, particle.life);

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
        }

        num_particles_ = num;

        if(0.0f < uniforms_.m_emissionLifetime)
        {
            spawn(uniforms_, _dt);
        }

        // Safety check: ensure num_particles_ never exceeds max_particles_
        BX_ASSERT(num_particles_ <= max_particles_, "Particle count exceeded maximum! num_particles_=%d, max_particles_=%d", num_particles_, max_particles_);
        num_particles_ = math::min(num_particles_, max_particles_);

        // Update AABB - use worst-case estimation on first update, then render will keep it updated
        if(first_update_)
        {
            updateWorstCaseAabb(uniforms_);
            first_update_ = false;
        }
    }

    void updateWorstCaseAabb(EmitterUniforms& uniforms_)
    {
        // Calculate worst-case AABB based on emitter parameters
        // This gives us conservative bounds for culling before any particles are rendered

        // Start with emitter position
        const math::vec3 emitterPos = uniforms_.m_position;
        const float maxLifeSpan = uniforms_.m_lifetime;
        const math::vec3 systemScale = uniforms_.m_scale;

        // Sample maximum velocity from gradient (conservative estimate using end points)
        const frange_t startVelRange = uniforms_.m_velocityGradient.sample(0.0f);
        const frange_t endVelRange = uniforms_.m_velocityGradient.sample(1.0f);
        const float maxVelocity =
            math::max(math::max(startVelRange.max, endVelRange.max), math::max(startVelRange.min, endVelRange.min));

        // Sample maximum scale from gradient (conservative estimate using end points)
        const frange_t startScaleRange = uniforms_.m_scaleGradient.sample(0.0f);
        const frange_t endScaleRange = uniforms_.m_scaleGradient.sample(1.0f);
        const float maxScale = math::max(math::max(startScaleRange.max, endScaleRange.max),
                                         math::max(startScaleRange.min, endScaleRange.min));

        // Calculate per-axis extents for more accurate bounds
        math::vec3 maxExtent = math::vec3(0.0f);

        // 1. Emission shape extent (scaled by emission shape scale)
        maxExtent += uniforms_.m_emissionShapeScale;

        // 2. Velocity-based travel distance (per-axis)
        const math::vec3 velocityExtent = systemScale * maxVelocity * maxLifeSpan;
        maxExtent += velocityExtent;

        // 3. Gravity effect (only affects Y axis)
        if(uniforms_.m_gravityScale != 0.0f)
        {
            const float gravityDistance =
                0.5f * 9.81f * math::abs(uniforms_.m_gravityScale) * (maxLifeSpan * maxLifeSpan) * systemScale.y;
            maxExtent.y += gravityDistance;
        }

        // 4. Force over lifetime effect (per-axis)
        const math::vec3 forceExtent =
            math::abs(uniforms_.m_forceOverLifetime) * (maxLifeSpan * maxLifeSpan) * systemScale;
        maxExtent += forceExtent;

        // 5. Particle scale padding (affects all axes equally)
        const float maxSystemScale = math::max(math::max(systemScale.x, systemScale.y), systemScale.z);
        const float scalePadding = maxScale * maxSystemScale;
        maxExtent += math::vec3(scalePadding);

        // Apply system scale to final extent and create AABB
        aabb_.min = emitterPos - maxExtent;
        aabb_.max = emitterPos + maxExtent;
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
        }
    }

    uint32_t render(const float* _mtxView,
                    const math::vec3& _eye,
                    uint32_t _first,
                    uint32_t _max,
                    ParticleSort* _outSort,
                    PosColorTexCoord0Vertex* _outVertices)
    {
        // Safety check: ensure num_particles_ is within bounds
        BX_ASSERT(num_particles_ <= max_particles_, "Render: Particle count exceeded maximum! num_particles_=%d, max_particles_=%d", num_particles_, max_particles_);
        const uint32_t safeNum = math::min(num_particles_, max_particles_);

        math::bbox aabb;
        aabb.reset();

        const uint32_t numToRender = math::min(safeNum, _max);
        for(uint32_t jj = 0; jj < numToRender; ++jj)
        {
            const Particle& particle = particles_[jj];
            const math::vec3& pos = particle.position;

            // Calculate distance for sorting
            ParticleSort& sort = _outSort[jj];
            const math::vec3 tmp0 = _eye - pos;
            sort.dist = math::length(tmp0);
            sort.idx = jj;

            // Use cached properties (calculated in update)
            const uint32_t abgr = particle.color;
            const float blend = particle.blend;
            const float scale = particle.scale;

            const math::vec3 udir = math::vec3(_mtxView[0] * scale, _mtxView[4] * scale, _mtxView[8] * scale);
            const math::vec3 vdir = math::vec3(_mtxView[1] * scale, _mtxView[5] * scale, _mtxView[9] * scale);

            PosColorTexCoord0Vertex* vertex = &_outVertices[jj * 4];

            const math::vec3 ul = pos - udir - vdir;
            vertex->x = ul.x;
            vertex->y = ul.y;
            vertex->z = ul.z;
            aabb.add_point(ul);
            vertex->abgr = abgr;
            vertex->u = 0.0f;
            vertex->v = 0.0f;
            vertex->blend = blend;
            ++vertex;

            const math::vec3 ur = pos + udir - vdir;
            vertex->x = ur.x;
            vertex->y = ur.y;
            vertex->z = ur.z;
            aabb.add_point(ur);
            vertex->abgr = abgr;
            vertex->u = 1.0f;
            vertex->v = 0.0f;
            vertex->blend = blend;
            ++vertex;

            const math::vec3 br = pos + udir + vdir;
            vertex->x = br.x;
            vertex->y = br.y;
            vertex->z = br.z;
            aabb.add_point(br);
            vertex->abgr = abgr;
            vertex->u = 1.0f;
            vertex->v = 1.0f;
            vertex->blend = blend;
            ++vertex;

            const math::vec3 bl = pos - udir + vdir;
            vertex->x = bl.x;
            vertex->y = bl.y;
            vertex->z = bl.z;
            aabb.add_point(bl);
            vertex->abgr = abgr;
            vertex->u = 0.0f;
            vertex->v = 1.0f;
            vertex->blend = blend;
            ++vertex;
        }

        if(numToRender > 0)
        {
            aabb_ = aabb;
        }

        return numToRender;
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

        PosColorTexCoord0Vertex::init();

        s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    }

    void shutdown()
    {
        bgfx::destroy(s_texColor);

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

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        if(false == checkAvailTransientBuffers(emitter.num_particles_ * 4 + 1024 * 100,
                                               PosColorTexCoord0Vertex::ms_layout,
                                               emitter.num_particles_ * 6))
        {
            BX_WARN(false, "Particle budget exceeded.");
            return; // Skip if no transient buffer space available
        }

        const uint32_t numVertices =
            bgfx::getAvailTransientVertexBuffer(emitter.num_particles_ * 4, PosColorTexCoord0Vertex::ms_layout);
        const uint32_t numIndices = bgfx::getAvailTransientIndexBuffer(emitter.num_particles_ * 6);
        const uint32_t max = bx::uint32_min(numVertices / 4, numIndices / 6);

        BX_WARN(emitter.num_particles_ == max,
                "Truncating transient buffer for emitter particles to maximum available (requested %d, available %d).",
                emitter.num_particles_,
                max);

        bgfx::allocTransientBuffers(&tvb, PosColorTexCoord0Vertex::ms_layout, max * 4, &tib, max * 6);
        PosColorTexCoord0Vertex* vertices = (PosColorTexCoord0Vertex*)tvb.data;

        // Render particles for this emitter only
        const uint32_t particleCount = emitter.render(_mtxView, _eye, 0, max, emitter.particle_sort_, vertices);

        // Sort particles within this emitter for proper alpha blending
        qsort(emitter.particle_sort_, particleCount, sizeof(ParticleSort), particleSortFn);

        // Generate indices for sorted particles
        uint16_t* indices = (uint16_t*)tib.data;
        for(uint32_t jj = 0; jj < particleCount; ++jj)
        {
            const ParticleSort& sort = emitter.particle_sort_[jj];
            uint16_t* index = &indices[jj * 6];
            uint16_t vertexIdx = (uint16_t)sort.idx;
            index[0] = vertexIdx * 4 + 0;
            index[1] = vertexIdx * 4 + 1;
            index[2] = vertexIdx * 4 + 2;
            index[3] = vertexIdx * 4 + 2;
            index[4] = vertexIdx * 4 + 3;
            index[5] = vertexIdx * 4 + 0;
        }

        // Render this emitter with its specific texture
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       BGFX_STATE_BLEND_NORMAL);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);	
        bgfx::setTexture(0, s_texColor, _texture);
        bgfx::submit(_view, _program);
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
