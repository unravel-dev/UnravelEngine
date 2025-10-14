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

#include "vs_particle.bin.h"
#include "fs_particle.bin.h"

static const bgfx::EmbeddedShader s_embeddedShaders[] =
{
	BGFX_EMBEDDED_SHADER(vs_particle),
	BGFX_EMBEDDED_SHADER(fs_particle),

	BGFX_EMBEDDED_SHADER_END()
};

struct PosColorTexCoord0Vertex
{
	float m_x;
	float m_y;
	float m_z;
	uint32_t m_abgr;
	float m_u;
	float m_v;
	float m_blend;
	float m_angle;

	static void init()
	{
		ms_layout
			.begin()
			.add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0,    4, bgfx::AttribType::Uint8, true)
			.add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
			.end();
	}

	static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout PosColorTexCoord0Vertex::ms_layout;

void EmitterUniforms::reset()
{
	m_position[0] = 0.0f;
	m_position[1] = 0.0f;
	m_position[2] = 0.0f;

	m_angle[0] = 0.0f;
	m_angle[1] = 0.0f;
	m_angle[2] = 0.0f;

	m_prevPosition[0] = 0.0f;
	m_prevPosition[1] = 0.0f;
	m_prevPosition[2] = 0.0f;


	m_velocityStart[0] = 0.0f;
	m_velocityStart[1] = 1.0f;
	m_velocityEnd[0]   = 2.0f;
	m_velocityEnd[1]   = 3.0f;

	m_rgba[0] = 0x00ffffff;
	m_rgba[1] = UINT32_MAX;
	m_rgba[2] = UINT32_MAX;
	m_rgba[3] = UINT32_MAX;
	m_rgba[4] = 0x00ffffff;

	m_blendStart[0] = 0.8f;
	m_blendStart[1] = 1.0f;
	m_blendEnd[0]   = 0.0f;
	m_blendEnd[1]   = 0.2f;

	m_scaleStart[0] = 0.1f;
	m_scaleStart[1] = 0.2f;
	m_scaleEnd[0]   = 0.3f;
	m_scaleEnd[1]   = 0.4f;

	m_lifetime = 1.0f;

	m_gravityScale  = 0.0f;
	m_particlesPerSecond = 50.0f; // Default: 50 particles per second
	m_temporalMotion = 1.0f; // Default: full temporal interpolation
	m_scale[0] = 1.0f; // Default: no scaling
	m_scale[1] = 1.0f;
	m_scale[2] = 1.0f;

	m_emissionLifetime = 2.0f; // Default: 2 second emission cycle

	m_easePos   = bx::Easing::Linear;
	m_easeRgba  = bx::Easing::Linear;
	m_easeBlend = bx::Easing::Linear;
	m_easeScale = bx::Easing::Linear;

	m_texture = BGFX_INVALID_HANDLE;
}

namespace ps
{
	struct Particle
	{
		bx::Vec3 start;
		bx::Vec3 end[2];
		float blendStart;
		float blendEnd;
		float scaleStart;
		float scaleEnd;

		uint32_t rgba[5];

		float life;
		float lifeSpan;
	};

	struct ParticleSort
	{
		float    dist;
		uint32_t idx;
	};

	inline uint32_t toAbgr(const float* _rgba)
	{
		return 0
			| (uint8_t(_rgba[0]*255.0f)<< 0)
			| (uint8_t(_rgba[1]*255.0f)<< 8)
			| (uint8_t(_rgba[2]*255.0f)<<16)
			| (uint8_t(_rgba[3]*255.0f)<<24)
			;
	}

	inline uint32_t toAbgr(float _rr, float _gg, float _bb, float _aa)
	{
		return 0
			| (uint8_t(_rr*255.0f)<< 0)
			| (uint8_t(_gg*255.0f)<< 8)
			| (uint8_t(_bb*255.0f)<<16)
			| (uint8_t(_aa*255.0f)<<24)
			;
	}

// Sprite atlas system removed - using direct texture handles per emitter

	struct Emitter
	{
		void create(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles);
		void destroy();

		void reset()
		{
			m_dt = 0.0f;
			m_uniforms.reset();
			m_num = 0;
			bx::memSet(&m_aabb, 0, sizeof(bx::Aabb) );
			m_firstUpdate = true;

			m_rng.reset();
		}

		void update(float _dt)
		{
			uint32_t num = m_num;
			for (uint32_t ii = 0; ii < num; ++ii)
			{
				Particle& particle = m_particles[ii];
				particle.life += _dt * 1.0f/particle.lifeSpan;

				if (particle.life > 1.0f)
				{
					if (ii != num-1)
					{
						bx::memCopy(&particle, &m_particles[num-1], sizeof(Particle) );
						--ii;
					}

					--num;
				}
			}

			m_num = num;

			if (0.0f < m_uniforms.m_emissionLifetime)
			{
				spawn(_dt);
			}
			
			// Safety check: ensure m_num never exceeds m_max
			BX_ASSERT(m_num <= m_max, "Particle count exceeded maximum! m_num=%d, m_max=%d", m_num, m_max);
			m_num = bx::min(m_num, m_max);
			
			// Update AABB - use worst-case estimation on first update, then render will keep it updated
			if (m_firstUpdate)
			{
				updateWorstCaseAabb();
				m_firstUpdate = false;
			}
		}

		void updateWorstCaseAabb()
		{
			// Calculate worst-case AABB based on emitter parameters
			// This gives us conservative bounds for culling before any particles are rendered
			
			// Start with emitter position
			const bx::Vec3 emitterPos = { m_uniforms.m_position[0], m_uniforms.m_position[1], m_uniforms.m_position[2] };
			
			// Calculate maximum possible particle travel distance
			const float maxLifeSpan = m_uniforms.m_lifetime;
			const float maxVelocity = bx::max(m_uniforms.m_velocityEnd[1], m_uniforms.m_velocityStart[1]); // Maximum velocity
			const float maxScale = m_uniforms.m_scaleEnd[1]; // Maximum scale
			const bx::Vec3 systemScale = { m_uniforms.m_scale[0], m_uniforms.m_scale[1], m_uniforms.m_scale[2] }; // System-wide scale
			
			// Estimate maximum travel distance based on velocity and gravity (scaled)
			// This is a conservative estimate - particles could travel this far in any direction
			const float maxSystemScale = bx::max(bx::max(systemScale.x, systemScale.y), systemScale.z);
			float maxTravelDistance = maxVelocity * maxSystemScale;
			
			// Add gravity effect over maximum lifespan (scaled)
			if (m_uniforms.m_gravityScale != 0.0f)
			{
				const float gravityDistance = 0.5f * 9.81f * m_uniforms.m_gravityScale * bx::square(maxLifeSpan) * systemScale.y;
				maxTravelDistance += bx::abs(gravityDistance);
			}
			
			// Add some padding for particle scale (scaled)
			const float padding = maxScale * maxSystemScale * 2.0f;
			maxTravelDistance += padding;
			
			// Create conservative AABB around emitter position with per-axis scaling
			const bx::Vec3 extent = { 
				maxTravelDistance * systemScale.x, 
				maxTravelDistance * systemScale.y, 
				maxTravelDistance * systemScale.z 
			};
			m_aabb.min = bx::sub(emitterPos, extent);
			m_aabb.max = bx::add(emitterPos, extent);
		}

		void spawn(float _dt)
		{
			// Skip emission if rate is zero or negative
			if (m_uniforms.m_particlesPerSecond <= 0.0f)
			{
				return;
			}
			
			// Calculate time per particle and accumulate time
			const float timePerParticle = 1.0f / m_uniforms.m_particlesPerSecond;
			m_dt += _dt;
			
			// Calculate how many particles to emit this frame
			const uint32_t numParticlesToEmit = uint32_t(m_dt / timePerParticle);
			m_dt -= numParticlesToEmit * timePerParticle; // Remove emitted time from accumulator
			
			// Don't emit more particles than we have space for
			const uint32_t maxEmittable = m_max - m_num;
			const uint32_t actualEmitCount = bx::min(numParticlesToEmit, maxEmittable);
			
			if (actualEmitCount == 0)
			{
				return;
			}
			
			// Calculate motion delta for temporal emission gap handling
			const bx::Vec3 currentPos = { m_uniforms.m_position[0], m_uniforms.m_position[1], m_uniforms.m_position[2] };
			const bx::Vec3 prevPos = { m_uniforms.m_prevPosition[0], m_uniforms.m_prevPosition[1], m_uniforms.m_prevPosition[2] };
			const bx::Vec3 deltaPos = bx::sub(currentPos, prevPos);
			const float motionDistance = bx::length(deltaPos);

			constexpr bx::Vec3 up = { 0.0f, 1.0f, 0.0f };

			// Emit particles with temporal interpolation
			for (uint32_t ii = 0; ii < actualEmitCount; ++ii)
			{
				// Calculate emission phase for temporal motion interpolation
				// Distribute particles evenly across the frame, scaled by temporal motion factor
				const float baseEmissionPhase = float(ii) / float(actualEmitCount);
				const float emissionPhase = baseEmissionPhase * m_uniforms.m_temporalMotion;
				
				// Find next available particle slot
				Particle* particle = &m_particles[m_num];
				m_num++;

				bx::Vec3 pos(bx::InitNone);
				switch (m_shape)
				{
					default:
					case EmitterShape::Sphere:
						pos = bx::randUnitSphere(&m_rng);
						break;

					case EmitterShape::Hemisphere:
						pos = bx::randUnitHemisphere(&m_rng, up);
						break;

					case EmitterShape::Circle:
						pos = bx::randUnitCircle(&m_rng);
						break;

					case EmitterShape::Disc:
						{
							const bx::Vec3 tmp = bx::randUnitCircle(&m_rng);
							pos = bx::mul(tmp, bx::frnd(&m_rng) );
						}
						break;

					case EmitterShape::Rect:
						pos =
						{
							bx::frndh(&m_rng),
							0.0f,
							bx::frndh(&m_rng),
						};
						break;
				}

				bx::Vec3 dir(bx::InitNone);
				switch (m_direction)
				{
					default:
					case EmitterDirection::Up:
						dir = up;
						break;

					case EmitterDirection::Outward:
						dir = bx::normalize(pos);
						break;
				}

				const float startVelocity = bx::lerp(m_uniforms.m_velocityStart[0], m_uniforms.m_velocityStart[1], bx::frnd(&m_rng) );
				const bx::Vec3 systemScale = { m_uniforms.m_scale[0], m_uniforms.m_scale[1], m_uniforms.m_scale[2] };
				const bx::Vec3 scaledPos = { pos.x * systemScale.x, pos.y * systemScale.y, pos.z * systemScale.z };
				const bx::Vec3 start = bx::mul(scaledPos, startVelocity);

				const float endVelocity = bx::lerp(m_uniforms.m_velocityEnd[0], m_uniforms.m_velocityEnd[1], bx::frnd(&m_rng) );
				const bx::Vec3 scaledDir = { dir.x * systemScale.x, dir.y * systemScale.y, dir.z * systemScale.z };
				const bx::Vec3 tmp1 = bx::mul(scaledDir, endVelocity);
				const bx::Vec3 end  = bx::add(tmp1, start);

				particle->life = 0.0f; // Always start at 0 for new particles
				particle->lifeSpan = m_uniforms.m_lifetime;

				const bx::Vec3 gravity = { 0.0f, -9.81f * m_uniforms.m_gravityScale * bx::square(particle->lifeSpan) * systemScale.y, 0.0f };

				// Calculate interpolated emitter position for temporal emission gap handling
				bx::Vec3 interpolatedEmitterPos = bx::lerp(prevPos, currentPos, emissionPhase);

				// Create transformation matrix with interpolated position
				float particleMtx[16];
				bx::mtxSRT(particleMtx
					, 1.0f, 1.0f, 1.0f
					, m_uniforms.m_angle[0], m_uniforms.m_angle[1], m_uniforms.m_angle[2]
					, interpolatedEmitterPos.x, interpolatedEmitterPos.y, interpolatedEmitterPos.z
					);

				particle->start  = bx::mul(start, particleMtx);
				particle->end[0] = bx::mul(end,   particleMtx);
				particle->end[1] = bx::add(particle->end[0], gravity);

				bx::memCopy(particle->rgba, m_uniforms.m_rgba, BX_COUNTOF(m_uniforms.m_rgba)*sizeof(uint32_t) );

				particle->blendStart = bx::lerp(m_uniforms.m_blendStart[0], m_uniforms.m_blendStart[1], bx::frnd(&m_rng) );
				particle->blendEnd   = bx::lerp(m_uniforms.m_blendEnd[0],   m_uniforms.m_blendEnd[1],   bx::frnd(&m_rng) );

				particle->scaleStart = bx::lerp(m_uniforms.m_scaleStart[0], m_uniforms.m_scaleStart[1], bx::frnd(&m_rng) );
				particle->scaleEnd   = bx::lerp(m_uniforms.m_scaleEnd[0],   m_uniforms.m_scaleEnd[1],   bx::frnd(&m_rng) );
			}
		}

		uint32_t render(const float* _mtxView, const bx::Vec3& _eye, uint32_t _first, uint32_t _max, ParticleSort* _outSort, PosColorTexCoord0Vertex* _outVertices)
		{
			// Safety check: ensure m_num is within bounds
			BX_ASSERT(m_num <= m_max, "Render: Particle count exceeded maximum! m_num=%d, m_max=%d", m_num, m_max);
			const uint32_t safeNum = bx::min(m_num, m_max);
			
			bx::EaseFn easeRgba  = bx::getEaseFunc(m_uniforms.m_easeRgba);
			bx::EaseFn easePos   = bx::getEaseFunc(m_uniforms.m_easePos);
			bx::EaseFn easeBlend = bx::getEaseFunc(m_uniforms.m_easeBlend);
			bx::EaseFn easeScale = bx::getEaseFunc(m_uniforms.m_easeScale);

			bx::Aabb aabb =
			{
				{  bx::kFloatInfinity,  bx::kFloatInfinity,  bx::kFloatInfinity },
				{ -bx::kFloatInfinity, -bx::kFloatInfinity, -bx::kFloatInfinity },
			};

			const uint32_t numToRender = bx::min(safeNum, _max);
			for (uint32_t jj = 0; jj < numToRender; ++jj)
			{
				const Particle& particle = m_particles[jj];

				const float ttPos   = easePos(particle.life);
				const float ttScale = easeScale(particle.life);
				const float ttBlend = bx::clamp(easeBlend(particle.life), 0.0f, 1.0f);
				const float ttRgba  = bx::clamp(easeRgba(particle.life),  0.0f, 1.0f);

				const bx::Vec3 p0  = bx::lerp(particle.start,  particle.end[0], ttPos);
				const bx::Vec3 p1  = bx::lerp(particle.end[0], particle.end[1], ttPos);
				const bx::Vec3 pos = bx::lerp(p0, p1, ttPos);

				ParticleSort& sort = _outSort[jj]; // Use local index jj instead of global current
				const bx::Vec3 tmp0 = bx::sub(_eye, pos);
				sort.dist = bx::length(tmp0);
				sort.idx  = jj; // Use local particle index for vertex buffer indexing

				uint32_t idx = uint32_t(ttRgba*4);
				float ttmod = bx::mod(ttRgba, 0.25f)/0.25f;
				uint32_t rgbaStart = particle.rgba[idx];
				uint32_t rgbaEnd   = particle.rgba[idx+1];

				float rr = bx::lerp( ( (uint8_t*)&rgbaStart)[0], ( (uint8_t*)&rgbaEnd)[0], ttmod)/255.0f;
				float gg = bx::lerp( ( (uint8_t*)&rgbaStart)[1], ( (uint8_t*)&rgbaEnd)[1], ttmod)/255.0f;
				float bb = bx::lerp( ( (uint8_t*)&rgbaStart)[2], ( (uint8_t*)&rgbaEnd)[2], ttmod)/255.0f;
				float aa = bx::lerp( ( (uint8_t*)&rgbaStart)[3], ( (uint8_t*)&rgbaEnd)[3], ttmod)/255.0f;

				float blend = bx::lerp(particle.blendStart, particle.blendEnd, ttBlend);
				// Use average scale for uniform particle sizing
				const float avgSystemScale = (m_uniforms.m_scale[0] + m_uniforms.m_scale[1] + m_uniforms.m_scale[2]) / 3.0f;
				float scale = bx::lerp(particle.scaleStart, particle.scaleEnd, ttScale) * avgSystemScale;

				uint32_t abgr = toAbgr(rr, gg, bb, aa);

				const bx::Vec3 udir = { _mtxView[0]*scale, _mtxView[4]*scale, _mtxView[8]*scale };
				const bx::Vec3 vdir = { _mtxView[1]*scale, _mtxView[5]*scale, _mtxView[9]*scale };

				PosColorTexCoord0Vertex* vertex = &_outVertices[jj*4];

				const bx::Vec3 ul = bx::sub(bx::sub(pos, udir), vdir);
				bx::store(&vertex->m_x, ul);
				aabbExpand(aabb, ul);
				vertex->m_abgr  = abgr;
				vertex->m_u     = 0.0f;
				vertex->m_v     = 0.0f;
				vertex->m_blend = blend;
				++vertex;

				const bx::Vec3 ur = bx::sub(bx::add(pos, udir), vdir);
				bx::store(&vertex->m_x, ur);
				aabbExpand(aabb, ur);
				vertex->m_abgr  = abgr;
				vertex->m_u     = 1.0f;
				vertex->m_v     = 0.0f;
				vertex->m_blend = blend;
				++vertex;

				const bx::Vec3 br = bx::add(bx::add(pos, udir), vdir);
				bx::store(&vertex->m_x, br);
				aabbExpand(aabb, br);
				vertex->m_abgr  = abgr;
				vertex->m_u     = 1.0f;
				vertex->m_v     = 1.0f;
				vertex->m_blend = blend;
				++vertex;

				const bx::Vec3 bl = bx::add(bx::sub(pos, udir), vdir);
				bx::store(&vertex->m_x, bl);
				aabbExpand(aabb, bl);
				vertex->m_abgr  = abgr;
				vertex->m_u     = 0.0f;
				vertex->m_v     = 1.0f;
				vertex->m_blend = blend;
				++vertex;
			}

			if(numToRender > 0)
			{
				m_aabb = aabb;
			}

			return numToRender;
		}

		EmitterShape::Enum     m_shape;
		EmitterDirection::Enum m_direction;

		float           m_dt;
		bx::RngMwc      m_rng;
		EmitterUniforms m_uniforms;

		bx::Aabb m_aabb;

		Particle* m_particles;
		uint32_t m_num;
		uint32_t m_max;
		
		bool m_firstUpdate; // Track if this is the first update to avoid interpolation
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

			if (nullptr == _allocator)
			{
				static bx::DefaultAllocator allocator;
				m_allocator = &allocator;
			}

			m_emitterAlloc = bx::createHandleAlloc(m_allocator, _maxEmitters);
			m_emitter = (Emitter*)bx::alloc(m_allocator, sizeof(Emitter)*_maxEmitters);

			PosColorTexCoord0Vertex::init();

			s_texColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

			bgfx::RendererType::Enum type = bgfx::getRendererType();
			m_particleProgram = bgfx::createProgram(
				  bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_particle")
				, bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_particle")
				, true
				);
		}

		void shutdown()
		{
			bgfx::destroy(m_particleProgram);
			bgfx::destroy(s_texColor);

			bx::destroyHandleAlloc(m_allocator, m_emitterAlloc);
			bx::free(m_allocator, m_emitter);

			m_allocator = nullptr;
		}

		// Sprite system removed - textures are managed externally and passed via EmitterUniforms

		void updateEmitterById(EmitterHandle _handle, float _dt)
		{
			BX_ASSERT(isValid(_handle)
				, "updateEmitterById handle %d is not valid."
				, _handle.idx
				);

			Emitter& emitter = m_emitter[_handle.idx];
			emitter.update(_dt);
		}

		void renderEmitterById(EmitterHandle _handle, uint8_t _view, bgfx::ProgramHandle _program, const float* _mtxView, const bx::Vec3& _eye)
		{
			BX_ASSERT(isValid(_handle)
				, "renderEmitterById handle %d is not valid."
				, _handle.idx
				);

			Emitter& emitter = m_emitter[_handle.idx];

			if (0 == emitter.m_num || !bgfx::isValid(emitter.m_uniforms.m_texture))
			{
				return; // Skip emitters with no particles or invalid texture
			}

			auto program = bgfx::isValid(_program) ? _program : m_particleProgram;

			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;

	
			if (false == checkAvailTransientBuffers(emitter.m_num*4 + 1024*100, PosColorTexCoord0Vertex::ms_layout, emitter.m_num*6))
			{
				BX_WARN(false
					, "Particle budget exceeded.");
				return; // Skip if no transient buffer space available
			}

					
			const uint32_t numVertices = bgfx::getAvailTransientVertexBuffer(emitter.m_num*4, PosColorTexCoord0Vertex::ms_layout);
			const uint32_t numIndices  = bgfx::getAvailTransientIndexBuffer(emitter.m_num*6);
			const uint32_t max = bx::uint32_min(numVertices/4, numIndices/6);
			
			BX_WARN(emitter.m_num == max
				, "Truncating transient buffer for emitter particles to maximum available (requested %d, available %d)."
				, emitter.m_num
				, max
				);

			bgfx::allocTransientBuffers(&tvb
				, PosColorTexCoord0Vertex::ms_layout
				, max*4
				, &tib
				, max*6
				);
			PosColorTexCoord0Vertex* vertices = (PosColorTexCoord0Vertex*)tvb.data;

			ParticleSort* particleSort = (ParticleSort*)bx::alloc(m_allocator, max*sizeof(ParticleSort) );
			if (!particleSort)
			{
				return; // Skip this emitter if allocation failed
			}

			// Render particles for this emitter only
			const uint32_t particleCount = emitter.render(_mtxView, _eye, 0, max, particleSort, vertices);

			// Sort particles within this emitter for proper alpha blending
			qsort(particleSort
				, particleCount
				, sizeof(ParticleSort)
				, particleSortFn
				);

			// Generate indices for sorted particles
			uint16_t* indices = (uint16_t*)tib.data;
			for (uint32_t jj = 0; jj < particleCount; ++jj)
			{
				const ParticleSort& sort = particleSort[jj];
				uint16_t* index = &indices[jj*6];
				uint16_t vertexIdx = (uint16_t)sort.idx;
				index[0] = vertexIdx*4+0;
				index[1] = vertexIdx*4+1;
				index[2] = vertexIdx*4+2;
				index[3] = vertexIdx*4+2;
				index[4] = vertexIdx*4+3;
				index[5] = vertexIdx*4+0;
			}

			// Ensure we're freeing valid memory
			BX_ASSERT(particleSort != nullptr, "Attempting to free null particleSort pointer");
			bx::free(m_allocator, particleSort);
			particleSort = nullptr; // Prevent double-free

			// Render this emitter with its specific texture
			bgfx::setState(0
				| BGFX_STATE_WRITE_RGB
				| BGFX_STATE_WRITE_A
				| BGFX_STATE_DEPTH_TEST_LESS
				| BGFX_STATE_CULL_CW
				| BGFX_STATE_BLEND_NORMAL
				);
			bgfx::setVertexBuffer(0, &tvb);
			bgfx::setIndexBuffer(&tib);
			bgfx::setTexture(0, s_texColor, emitter.m_uniforms.m_texture);	
			bgfx::submit(_view, program);
		}

		EmitterHandle createEmitter(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles)
		{
			EmitterHandle handle = { m_emitterAlloc->alloc() };

			if (UINT16_MAX != handle.idx)
			{
				m_emitter[handle.idx].create(_shape, _direction, _maxParticles);
			}

			return handle;
		}

		void updateEmitter(EmitterHandle _handle, const EmitterUniforms* _uniforms)
		{
			BX_ASSERT(isValid(_handle)
				, "destroyEmitter handle %d is not valid."
				, _handle.idx
				);

			Emitter& emitter = m_emitter[_handle.idx];

			if (nullptr == _uniforms)
			{
				emitter.reset();
			}
			else
			{
				// Copy new uniforms
				EmitterUniforms newUniforms = *_uniforms;

	            float prevPosition[3];
				prevPosition[0] = emitter.m_uniforms.m_position[0];
				prevPosition[1] = emitter.m_uniforms.m_position[1];
				prevPosition[2] = emitter.m_uniforms.m_position[2];
				
				emitter.m_uniforms = newUniforms;

				emitter.m_uniforms.m_prevPosition[0] = prevPosition[0];
				emitter.m_uniforms.m_prevPosition[1] = prevPosition[1];
				emitter.m_uniforms.m_prevPosition[2] = prevPosition[2];

				if(emitter.m_firstUpdate)
				{
					emitter.m_uniforms.m_prevPosition[0] = emitter.m_uniforms.m_position[0];
					emitter.m_uniforms.m_prevPosition[1] = emitter.m_uniforms.m_position[1];
					emitter.m_uniforms.m_prevPosition[2] = emitter.m_uniforms.m_position[2];
				}

				
			}
		}

		void getAabb(EmitterHandle _handle, bx::Aabb& _outAabb)
		{
			BX_ASSERT(isValid(_handle)
				, "getAabb handle %d is not valid."
				, _handle.idx
				);
			_outAabb = m_emitter[_handle.idx].m_aabb;
		}
		uint32_t getNumParticles(EmitterHandle _handle)
		{
			BX_ASSERT(isValid(_handle)
				, "getNumParticles handle %d is not valid."
				, _handle.idx
				);
			return m_emitter[_handle.idx].m_num;
		}

		void destroyEmitter(EmitterHandle _handle)
		{
			BX_ASSERT(isValid(_handle)
				, "destroyEmitter handle %d is not valid."
				, _handle.idx
				);

			m_emitter[_handle.idx].destroy();
			m_emitterAlloc->free(_handle.idx);
		}

		bx::AllocatorI* m_allocator;

		bx::HandleAlloc* m_emitterAlloc;
		Emitter* m_emitter;

		bgfx::UniformHandle s_texColor;
		bgfx::ProgramHandle m_particleProgram;
	};

	static ParticleSystem s_ctx;

	void Emitter::create(EmitterShape::Enum _shape, EmitterDirection::Enum _direction, uint32_t _maxParticles)
	{
		reset();

		m_shape     = _shape;
		m_direction = _direction;
		m_max       = _maxParticles;
		m_particles = (Particle*)bx::alloc(s_ctx.m_allocator, m_max*sizeof(Particle) );
	}

	void Emitter::destroy()
	{
		bx::free(s_ctx.m_allocator, m_particles);
		m_particles = nullptr;
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

void psUpdateEmitter(EmitterHandle _handle, float _dt, const EmitterUniforms* _uniforms)
{
	if (_uniforms != nullptr)
	{
		s_ctx.updateEmitter(_handle, _uniforms);
	}
	s_ctx.updateEmitterById(_handle, _dt);
}

void psResetEmitter(EmitterHandle _handle)
{
	BX_ASSERT(isValid(_handle)
		, "psResetEmitter handle %d is not valid."
		, _handle.idx
		);

	s_ctx.m_emitter[_handle.idx].reset();
}

void psGetAabb(EmitterHandle _handle, bx::Aabb& _outAabb)
{
	s_ctx.getAabb(_handle, _outAabb);
}

uint32_t psGetNumParticles(EmitterHandle _handle)
{
	return s_ctx.getNumParticles(_handle);
}

void psDestroyEmitter(EmitterHandle _handle)
{
	s_ctx.destroyEmitter(_handle);
}

void psRenderEmitter(EmitterHandle _handle, uint8_t _view, bgfx::ProgramHandle _program, const float* _mtxView, const bx::Vec3& _eye)
{
	s_ctx.renderEmitterById(_handle, _view, _program, _mtxView, _eye);
}
