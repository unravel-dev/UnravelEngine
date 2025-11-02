# Static Mesh Batching System Implementation Plan

## Overview
This document outlines the step-by-step implementation of a static mesh batching system for UnravelEngine. The system will use BGFX's instancing capabilities to batch multiple static meshes with the same material and geometry into single draw calls, significantly reducing CPU overhead and improving rendering performance.

## Design Goals
- **Opt-in System**: Keep existing individual rendering as default, add batching as optional feature
- **Performance Comparison**: Enable side-by-side performance analysis
- **Backward Compatibility**: Maintain all existing functionality
- **Incremental Implementation**: Each step can be tested independently

## Implementation Steps

### Phase 1: Core Infrastructure (Steps 1-3)

#### Step 1: Create Batch Key System
**File**: `engine/engine/rendering/batch_key.h` and `batch_key.cpp`

Create the batch key structure for grouping compatible draw calls:

```cpp
struct batch_key
{
    asset_handle<mesh> mesh_handle;
    asset_handle<material> material_handle;
    uint32_t lod_index;
    uint32_t submesh_index;
    
    // Hash and comparison for std::unordered_map
    auto operator<=>(const batch_key&) const = default;
    size_t hash() const noexcept;
};

// Hash specialization
template<>
struct std::hash<batch_key>
{
    size_t operator()(const batch_key& key) const noexcept;
};
```

**Tasks**:
- [ ] Create batch_key.h header file
- [ ] Implement hash function using mesh/material handles
- [ ] Add comparison operators
- [ ] Create unit tests for key generation and hashing

#### Step 2: Create Instance Data Structures
**File**: `engine/engine/rendering/batch_instance.h`

Define instance data layout for GPU:

```cpp
struct batch_instance
{
    math::mat4 world_transform;
    math::vec3 lod_params;          // LOD blending parameters
    float padding;                  // Align to 16 bytes
};

struct instance_vertex_data
{
    math::vec4 matrix_row0;         // i_data0
    math::vec4 matrix_row1;         // i_data1  
    math::vec4 matrix_row2;         // i_data2
    math::vec4 matrix_row3;         // i_data3 (w = lod_param)
};

// Conversion function
auto pack_instance_data(const batch_instance& instance) -> instance_vertex_data;
```

**Tasks**:
- [ ] Define instance data structures
- [ ] Implement packing/unpacking functions
- [ ] Ensure proper memory alignment
- [ ] Add validation for data integrity

#### Step 3: Create Batch Collector
**File**: `engine/engine/rendering/batch_collector.h` and `batch_collector.cpp`

Implement the core batching logic:

```cpp
class batch_collector
{
public:
    struct batch_group
    {
        batch_key key;
        std::vector<batch_instance> instances;
        size_t max_instances = 1024;
    };
    
    // Collection phase
    void collect_renderable(const batch_key& key, const batch_instance& instance);
    
    // Preparation phase  
    void prepare_batches();
    
    // Submission phase
    void submit_batches(const submit_context& context);
    
    // Cleanup
    void clear();
    
    // Statistics
    auto get_stats() const -> const batch_stats&;

private:
    std::unordered_map<batch_key, batch_group> batch_groups_;
    std::vector<batch_group*> sorted_batches_;
    batch_stats stats_;
};
```

**Tasks**:
- [ ] Implement batch collection logic
- [ ] Add batch sorting by state (material, mesh, LOD)
- [ ] Implement statistics tracking
- [ ] Add batch size limits and splitting
- [ ] Create comprehensive unit tests

### Phase 2: Shader Infrastructure (Steps 4-5)

#### Step 4: Create Instanced Geometry Shaders
**Files**: 
- `engine_data/data/shaders/vs_deferred_geom_instanced.sc`
- `engine_data/data/shaders/fs_deferred_geom_instanced.sc` (copy of existing)

Create instanced version of geometry shader:

```glsl
$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0

#include "common.sh"

void main()
{
    // Reconstruct world matrix from instance data
    mat4 worldMatrix = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    
    vec4 wpos = mul(worldMatrix, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, wpos);

    vec4 normal = a_normal * 2.0 - 1.0;
    vec4 tangent = a_tangent * 2.0 - 1.0;
    vec4 bitangent = a_bitangent * 2.0 - 1.0;

    mat3 modelIT = calculateInverseTranspose(worldMatrix);
    
    vec3 wnormal = normalize(mul(modelIT, normal.xyz));
    vec3 wtangent = normalize(mul(modelIT, tangent.xyz));
    vec3 wbitangent = normalize(mul(modelIT, bitangent.xyz));
    
    v_wpos = wpos.xyz;
    v_pos = gl_Position.xyz/gl_Position.w;
    v_wnormal = wnormal;
    v_wtangent = wtangent;
    v_wbitangent = wbitangent;
    v_texcoord0 = a_texcoord0;
}
```

**Tasks**:
- [ ] Create instanced vertex shader
- [ ] Test shader compilation for all platforms
- [ ] Verify matrix reconstruction works correctly
- [ ] Add LOD parameter support if needed

#### Step 5: Create Instanced GPU Program Wrapper
**File**: `engine/engine/rendering/geom_program_instanced.h` and `.cpp`

Extend existing GPU program for instancing:

```cpp
struct geom_program_instanced : geom_program
{
    // Instance vertex layout
    gfx::vertex_layout instance_layout;
    
    void cache_uniforms() override;
    void setup_instance_layout();
    
    // Same uniforms as base class, no additional ones needed
};
```

**Tasks**:
- [ ] Create instanced program wrapper
- [ ] Setup instance vertex layout
- [ ] Integrate with existing uniform caching
- [ ] Test program creation and validation

### Phase 3: Model Integration (Steps 6-7)

#### Step 6: Extend Model Class for Batching
**File**: `engine/engine/rendering/model.h` and `model.cpp`

Add batching support to model class:

```cpp
class model : public crtp_meta_type<model>
{
public:
    // Existing methods remain unchanged...
    
    // New batching methods
    void submit_for_batching(batch_collector& collector,
                           const math::mat4& world_transform,
                           const pose_mat4& submesh_transforms,
                           const pose_mat4& bone_transforms,
                           unsigned int lod) const;
    
    auto create_batch_keys(unsigned int lod) const -> std::vector<batch_key>;
    
    // Check if model is batchable (static, no special requirements)
    auto is_batchable() const -> bool;

private:
    // Helper methods
    auto create_batch_instance(const math::mat4& world_transform,
                              const pose_mat4& submesh_transforms,
                              unsigned int lod,
                              uint32_t submesh_index) const -> batch_instance;
};
```

**Tasks**:
- [ ] Implement batch key generation for submeshes
- [ ] Add batchability checks (static vs skinned, special materials)
- [ ] Implement instance data creation
- [ ] Maintain full backward compatibility
- [ ] Add comprehensive tests

#### Step 7: Create Batch Renderer
**File**: `engine/engine/rendering/batch_renderer.h` and `batch_renderer.cpp`

Implement the actual batch rendering logic:

```cpp
class batch_renderer
{
public:
    struct render_context
    {
        const camera* cam;
        gfx::render_view* rview;
        gfx::render_pass* pass;
        const geom_program_instanced* program;
    };
    
    void render_batch(const batch_collector::batch_group& batch,
                     const render_context& context);
    
    void render_all_batches(const batch_collector& collector,
                           const render_context& context);

private:
    // Instance data buffer management
    gfx::transient_vertex_buffer create_instance_buffer(
        const std::vector<batch_instance>& instances);
    
    void setup_batch_state(const batch_key& key, const render_context& context);
};
```

**Tasks**:
- [ ] Implement batch rendering with transient instance buffers
- [ ] Add proper state management (materials, textures)
- [ ] Implement error handling and validation
- [ ] Add performance profiling hooks
- [ ] Test with various batch sizes

### Phase 4: Pipeline Integration (Steps 8-9)

#### Step 8: Modify Deferred Pipeline
**File**: `engine/engine/rendering/pipeline/deferred/pipeline.h` and `pipeline.cpp`

Integrate batching into the deferred pipeline:

```cpp
class deferred : public pipeline
{
private:
    // Batching components
    batch_collector static_batch_collector_;
    batch_renderer batch_renderer_;
    geom_program_instanced geom_program_instanced_;
    
    // Configuration
    bool enable_batching_ = false;  // Opt-in feature
    
    // Modified g-buffer pass with batching support
    void run_g_buffer_pass_hybrid(const visibility_set_models_t& visibility_set,
                                  const camera& camera,
                                  gfx::render_view& rview,
                                  delta_t dt);
    
    // Separate batched and individual rendering
    void run_g_buffer_pass_batched(const std::vector<entt::entity>& batchable_entities,
                                   const camera& camera,
                                   gfx::render_view& rview,
                                   delta_t dt);
    
    void run_g_buffer_pass_individual(const std::vector<entt::entity>& individual_entities,
                                      const camera& camera,
                                      gfx::render_view& rview,
                                      delta_t dt);

public:
    // Configuration methods
    void set_batching_enabled(bool enabled) { enable_batching_ = enabled; }
    auto is_batching_enabled() const -> bool { return enable_batching_; }
    
    // Statistics
    auto get_batch_stats() const -> const batch_stats&;
};
```

**Tasks**:
- [ ] Implement hybrid rendering approach
- [ ] Add entity classification (batchable vs individual)
- [ ] Integrate batch collector into pipeline
- [ ] Maintain existing rendering path as fallback
- [ ] Add configuration options

#### Step 9: Add Performance Monitoring
**File**: `engine/engine/rendering/batch_stats.h` and related files

Implement comprehensive performance tracking:

```cpp
struct batch_stats
{
    // Batch statistics
    uint32_t total_batches = 0;
    uint32_t total_instances = 0;
    uint32_t draw_calls_saved = 0;
    
    // Performance metrics
    float batch_collection_time_ms = 0.0f;
    float batch_submission_time_ms = 0.0f;
    
    // Memory usage
    size_t instance_buffer_memory_used = 0;
    
    // Efficiency metrics
    float average_batch_size = 0.0f;
    float batching_efficiency = 0.0f;  // instances / draw_calls
    
    void reset();
    auto to_string() const -> std::string;
};
```

**Tasks**:
- [ ] Implement statistics collection
- [ ] Add timing measurements
- [ ] Create performance comparison tools
- [ ] Add debug visualization
- [ ] Integrate with existing profiling system

### Phase 5: Testing and Optimization (Steps 10-12)

#### Step 10: Create Test Scenes
**Files**: Test scenes and unit tests

Create comprehensive test scenarios:

```cpp
// Test scenarios to implement:
// 1. Many identical cubes (best case for batching)
// 2. Mixed static and skinned models
// 3. Different materials with same geometry
// 4. Various LOD levels
// 5. Edge cases (empty batches, single instances)
```

**Tasks**:
- [ ] Create test scenes with varying complexity
- [ ] Implement automated performance tests
- [ ] Add regression tests for rendering correctness
- [ ] Create stress tests for large batch counts
- [ ] Validate visual output matches individual rendering

#### Step 11: Performance Analysis Tools
**File**: `engine/engine/rendering/batch_profiler.h` and related

Create tools for performance analysis:

```cpp
class batch_profiler
{
public:
    struct comparison_result
    {
        float individual_render_time_ms;
        float batched_render_time_ms;
        float performance_improvement;
        uint32_t draw_calls_before;
        uint32_t draw_calls_after;
    };
    
    auto run_comparison_test(scene& test_scene) -> comparison_result;
    void generate_performance_report();
};
```

**Tasks**:
- [ ] Implement side-by-side performance comparison
- [ ] Create automated benchmarking tools
- [ ] Add memory usage analysis
- [ ] Generate performance reports
- [ ] Create visualization tools for batch efficiency

#### Step 12: Documentation and Integration
**Files**: Documentation and integration guides

Complete the implementation:

**Tasks**:
- [ ] Write comprehensive documentation
- [ ] Create integration examples
- [ ] Add configuration guidelines
- [ ] Document performance characteristics
- [ ] Create migration guide for existing projects

## Implementation Timeline

### Week 1: Core Infrastructure
- Steps 1-3: Batch key system, instance data, batch collector

### Week 2: Shader and GPU Integration  
- Steps 4-5: Instanced shaders and GPU programs

### Week 3: Model and Rendering Integration
- Steps 6-7: Model batching support and batch renderer

### Week 4: Pipeline Integration
- Steps 8-9: Deferred pipeline integration and performance monitoring

### Week 5: Testing and Optimization
- Steps 10-12: Testing, analysis tools, and documentation

## Success Criteria

### Performance Targets
- **50-80% reduction** in draw calls for scenes with many similar objects
- **30-50% reduction** in CPU rendering overhead
- **No visual differences** between batched and individual rendering
- **Configurable batch sizes** for different hardware capabilities

### Quality Targets
- **100% backward compatibility** with existing rendering
- **Comprehensive test coverage** (>90% code coverage)
- **Zero performance regression** when batching is disabled
- **Clear performance improvement** in target scenarios

## Risk Mitigation

### Technical Risks
- **Shader compatibility**: Test on all target platforms early
- **Memory overhead**: Monitor instance buffer usage carefully  
- **State management**: Ensure proper material/texture binding
- **LOD integration**: Verify LOD transitions work correctly

### Integration Risks
- **Backward compatibility**: Maintain existing API unchanged
- **Performance regression**: Keep individual rendering as default
- **Complexity**: Implement incrementally with thorough testing

## Next Steps

1. **Start with Step 1**: Create the batch key system
2. **Validate approach**: Test basic batching with simple geometry
3. **Iterate quickly**: Implement and test each step independently
4. **Measure early**: Add performance monitoring from the beginning
5. **Document progress**: Keep detailed notes on performance improvements

This implementation plan provides a clear roadmap for adding static mesh batching to UnravelEngine while maintaining stability and enabling performance comparisons.
