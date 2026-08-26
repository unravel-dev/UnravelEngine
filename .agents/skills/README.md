# UnravelEngine Agent Skills

Portable, project-scoped skills for UnravelEngine. **Source of truth:** `.agents/skills/`.

Always-on conventions live only in root `AGENTS.md`. Optional junctions under
`.cursor/skills` / `.claude/skills` point here (see `.agents/README.md`).

## How to use

Skills load when the task matches their `description` in frontmatter, or when you name them
explicitly (e.g. "use the unravel-rendering skill"). All skills allow model
auto-invocation except `unravel-architect`, which is invoked deliberately for large
design work.

Start non-trivial work with **`unravel-triage`**.

## Skill catalog

### Meta / routing

| Skill | Use when |
|-------|----------|
| `unravel-triage` | Starting any non-trivial task; route to the right domain |
| `unravel-architect` | Large features, API design, cross-subsystem refactors |
| `unravel-lessons` | After user corrections; capture patterns in `tasks/lessons.md` |

### Domain (Tier 1)

| Skill | Use when |
|-------|----------|
| `unravel-ecs-component` | Components, systems, scenes, entity lifecycle |
| `unravel-rendering` | Shaders, render passes, pipeline, post-processing |
| `unravel-editor-panel` | ImGui panels, menus, gizmos, undo/redo UI |
| `unravel-assets` | Importers, `.meta`, compilation, async loading |
| `unravel-scripting` | C# / CoreCLR (dotnetpp), hot-reload, script glue |

### Domain (Tier 2)

| Skill | Use when |
|-------|----------|
| `unravel-physics` | Bullet, collisions, character controllers |
| `unravel-animation` | Skeletal animation, blend spaces |
| `unravel-ui-rmlui` | In-game RmlUi documents |
| `unravel-audio` | OpenAL sources, listeners, spatial audio |
| `unravel-prefabs` | Prefab assets, nested instances, statement lists / overrides |
| `unravel-profiler-debug` | GPU timeline, eviction stats, frame debugging |
| `unravel-materials` | PBR `.mat` / MCP materials |
| `unravel-projects` | Open project / scene presets / `.spfb` |
| `unravel-entities` | MCP entities, transforms, scripts, scene_save |
| `unravel-viewport` | Scene camera, focus, orbit, screenshots |

### Workflow procedures

| Skill | Use when |
|-------|----------|
| `unravel-add-component` | End-to-end new ECS component |
| `unravel-add-render-pass` | New pass in deferred pipeline |
| `unravel-add-inspector` | Custom inspector for a reflected type |
| `unravel-shader-change` | Edit bgfx `.sc` shaders |
| `unravel-play-mode-change` | Edit vs play mode separation |
| `unravel-build-verify` | CMake, CI, data targets, .NET SDK |
| `unravel-bug-investigation` | Structured bug fix workflow |

## Authoring guidelines

- Keep `SKILL.md` under 500 lines; put deep detail in `reference.md`
- Use third-person descriptions with trigger terms
- Encode Unravel-specific paths and checklists, not generic C++ advice
- Never modify `deps/3rdparty/` unless unavoidable
- Reference `AGENTS.md` for always-on rules; put domain workflows in skills
- Verification sections should name the concrete `unravel-tests` suites for the
  domain (see `unravel-build-verify`), not just "build and smoke test"
- When a lesson in `tasks/lessons.md` states a durable contract, promote it into the
  matching skill (see `unravel-lessons`) - skills go stale when knowledge stops at
  the lessons log
