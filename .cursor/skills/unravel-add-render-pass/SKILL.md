---
name: unravel-add-render-pass
description: >-
  Procedure for adding a render pass to the UnravelEngine deferred pipeline.
  Use when adding geometry, lighting, post-process, or compute passes to the
  rendering pipeline.
disable-model-invocation: true
---

# Add Render Pass Workflow

## Prerequisites

- Read `unravel-rendering` skill
- Understand where pass fits in `deferred::run_pipeline_impl`
- Identify required inputs (G-buffer, depth, shadow maps) and outputs

## Step 1: Design pass contract

Define:

- Input attachments (albedo, normal, depth, etc.)
- Output target (screen, intermediate FBO)
- When pass runs (new `pipeline_steps` flag or existing flag)
- Layer mask interaction

## Step 2: Add pipeline_steps flag (if new pass category)

**File:** `engine/engine/rendering/pipeline/deferred/pipeline.h`

```cpp
enum pipeline_steps : uint32_t
{
    // existing flags...
    my_pass = 1u << N,
};
```

## Step 3: Implement pass

**File:** `engine/engine/rendering/pipeline/deferred/pipeline.cpp`

- Add pass function (e.g. `run_my_pass(...)`)
- Call from `run_pipeline_impl` at correct stage
- Guard with `(stages & pipeline_steps::my_pass) != 0u`
- Set `run_params::pflags` at call sites if editor/game need to toggle

## Step 4: Shaders (if needed)

Follow `unravel-shader-change`:

- Add `.sc` files in `engine_data/data/shaders/`
- Register in CMake shader compile list if needed
- Bind uniforms/samplers matching engine conventions

## Step 5: Volume component (if artist-tweakable post-process)

If pass parameters should be per-scene:

1. New `*_component` in `rendering/ecs/components/`
2. Meta registration
3. Pipeline reads component from scene entities
4. Add to `all_components.h`

## Step 6: Debug visualization (optional)

- `set_debug_pass(int)` for pipeline debug modes
- Editor menu entry in scene panel visualization menu

## Step 7: Stats (optional)

Extend `pipeline_stats` if pass adds measurable work. Update `viewport_stats_overlay` if user-visible.

## Verification

- [ ] Pass runs when flag set, skipped when not
- [ ] Correct ordering relative to shadows/lighting/post
- [ ] Scene and game view output correct
- [ ] No FBO format mismatch asserts
- [ ] GPU stats reasonable (no accidental double cost)
- [ ] Play mode matches edit mode

## Common integration points

| Call site | File |
|-----------|------|
| Main render | `rendering_system.cpp` |
| Shadow toggle | scene panel visualization menu |
| Debug pass | `deferred::set_debug_pass` |
