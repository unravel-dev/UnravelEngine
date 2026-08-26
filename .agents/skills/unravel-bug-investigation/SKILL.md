---
name: unravel-bug-investigation
description: >-
  Structured bug investigation workflow for UnravelEngine: reproduce, isolate
  domain, find root cause, minimal fix, and verify. Use for bug reports, crashes,
  regressions, or unexpected behavior in engine or editor.
---

# Bug Investigation Workflow

## Phase 1: Triage

Run `unravel-triage` to classify domain. Gather:

- Repro steps (exact clicks/keys)
- Edit mode vs play mode
- Scene-specific or global
- Platform (Windows/Linux/macOS)
- Console/log output
- Recent related changes (`git log`, `git diff`)

## Phase 2: Reproduce

- Reproduce locally before fixing
- Reduce to minimal scene/steps
- Note whether fresh project reproduces

If not reproducible: ask for scene file, project settings, or video - but keep investigating logs/code paths meanwhile.

## Phase 3: Isolate

Narrow the failure surface:

| Symptom | First reads |
|---------|-------------|
| Render/visual | `unravel-rendering`, viewport, pipeline |
| UI/layout | `unravel-editor-panel`, ImGui ID conflicts |
| Crash on play | `on_play_begin` handlers, physics, scripts |
| Save/load | serialization, meta, prefab paths |
| Asset missing | `unravel-assets`, UID, `.meta` |
| Script error | `unravel-scripting`, managed / editor console |
| Performance | `unravel-profiler-debug` |

Use logging (`APPLOG_*`), breakpoints, or stats overlay as appropriate.

## Phase 4: Root cause

- Trace call chain from symptom to origin
- Prefer root cause over symptom patch
- Check if bug is edit/play boundary, serialization, or asset staleness

**Do not** apply speculative fixes without evidence.

## Phase 5: Minimal fix

- Smallest correct diff
- Match surrounding conventions
- No drive-by refactors
- No `deps/3rdparty` edits unless unavoidable

## Phase 6: Verify

- [ ] Original repro steps pass
- [ ] Relevant `unravel-tests` suite green (serialization / physics / ik / animation /
      gi - see `unravel-build-verify`); when practical, add a failing check to the
      suite FIRST, then fix
- [ ] Related areas smoke tested
- [ ] Play mode enter/exit if applicable
- [ ] Save/reload if data involved
- [ ] Build succeeds (`unravel-build-verify`)
- [ ] No new linter issues in touched files

## Phase 7: Capture lesson

If user corrected approach: run `unravel-lessons` to record pattern.

## Report format

When presenting fix to user:

```markdown
## Root cause
[One sentence]

## Fix
[What changed and why]

## Verification
[What was tested]
```

## Anti-patterns

- Patching symptoms (null check hiding bad state)
- Large refactor bundled with bug fix
- Fixing without reproducing
- Marking done without build/test
