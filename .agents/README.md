# Agent configuration

Where agent instructions live in this repository, who owns each kind, and how they stay
true. Skill authoring rules: [skills/README.md](skills/README.md).

## Files

```text
AGENTS.md                     always-on rules for the whole repository (every agent tool reads it)
CLAUDE.md                     @AGENTS.md - Claude Code reads AGENTS.md through this import
deps/AGENTS.md, editor/AGENTS.md
                              rules for one directory; load when an agent works there
deps/CLAUDE.md, editor/CLAUDE.md
                              @AGENTS.md beside each nested AGENTS.md
.agents/skills/               skills (SKILL.md + reference files) - the source of truth
.agents/link-agent-dirs.sh    creates .claude/skills and .cursor/skills as links to .agents/skills
.claude/settings.json         shared Claude Code settings: permissions, hooks, attribution
.claude/hooks/                hook scripts
editor_data/data/mcp/skills/  guides the editor's MCP server serves to any MCP client
```

`.claude/skills` and `.cursor/skills` are not in git. Claude Code creates them through the
SessionStart hook in `.claude/settings.json`; a session that starts before the link exists
picks the skills up after `/reload-skills` or on the next start. Other tools:

```bash
bash .agents/link-agent-dirs.sh
```

## Where knowledge goes

| Knowledge | Home |
|-----------|------|
| A rule that must hold every time | a hook or permission rule in `.claude/settings.json` |
| True for all code and not visible in the code | `AGENTS.md` |
| True for one directory | that directory's `AGENTS.md` |
| Contract, trap or procedure of one area | the skill that owns the area (table below) |
| Using the editor through its MCP tools | `editor_data/data/mcp/skills/` (served to every MCP client) |
| Personal preferences, facts about one machine | the agent's personal memory |
| Plans, analyses, measurements | `tasks/` (local, not in git) |

One fact has one home. Another file that needs it names the owner instead of repeating it.

## Area owners

Skills carry no `paths` frontmatter (it hides them until a matching file is read); this
table is where the ownership of directories is written down.

| Area | Skill | Owns |
|------|-------|------|
| Renderer, pipeline, passes, shaders, GI, shadows, post | `unravel-rendering` | `engine/engine/rendering/**`, `engine/core/graphics/**`, `engine_data/data/shaders/**`, `editor_data/data/shaders/**` |
| Material graphs, `.mat`, material instances | `unravel-materials` | `engine/engine/rendering/material_graph/**`, `editor/editor/hub/panels/material_graph_panel/**`, `editor_data/data/material_graphs/**`, `engine_data/data/shaders/material_graph/**` |
| ECS, components, systems, scenes, play mode, events | `unravel-ecs-component` | `engine/engine/ecs/**`, `engine/engine/meta/ecs/**`, `engine/engine/play_mode.*`, `engine/engine/events.h`, `engine/engine/engine.cpp` |
| Editor UI: panels, inspectors, gizmos, undo | `unravel-editor-panel` | `editor/editor/hub/**`, `editor/editor/imgui/**`, `editor/editor/editing/**` |
| Assets: import, compile, `.meta`, loading, content packages | `unravel-assets` | `engine/engine/assets/**`, `editor/core/asset_compiler/**`, `editor/editor/assets/**`, `editor/core/packages/**` |
| Prefabs | `unravel-prefabs` | `engine/engine/ecs/prefab.h`, `engine/engine/ecs/components/prefab_component.*`, `editor/editor/editing/prefab_*` |
| C# scripting, dotnetpp | `unravel-scripting` | `engine/engine/scripting/**`, `engine_data/data/scripts/**`, `deps/dotnetpp/**` |
| Physics | `unravel-physics` | `engine/engine/physics/**` |
| Animation | `unravel-animation` | `engine/engine/animation/**`, `editor/editor/hub/panels/animation_panel/**` |
| Audio, audiopp | `unravel-audio` | `engine/engine/audio/**`, `deps/audiopp/**` |
| In-game UI (RmlUi) | `unravel-ui-rmlui` | `engine/engine/ui/**`, `engine_data/data/ui/**` |
| Build, tests, CI | `unravel-build-verify` | `CMakeLists.txt`, `CMakePresets.json`, `cmake/**`, `.github/**`, `tests/**` |
| Bugs, crashes, profiling, measuring | `unravel-debugging` | `engine/engine/profiler/**`, `editor/editor/hub/panels/profiler_panel/**` |
| Driving the editor through MCP; MCP server code | `unravel-editor-mcp` | `editor/editor/system/mcp/**`, `editor/editor/system/mcp_manager.*`, `editor_data/data/mcp/**` |
| Designs that cross several areas | `unravel-architect` | - |

No skill yet (AGENTS.md and the code are the reference): deploy (`editor/core/deploy/`,
`editor/editor/deploy/`), the web player (`engine/engine/web/`), update check,
`engine/core/` utilities, input, settings, threading, `deps/ospp`, `deps/threadpp`, `deps/hpp`.

## When the user corrects an agent

1. Fix the work.
2. Write the rule where it belongs (table above): one or two checkable lines - usually the
   owning skill's Gotchas, AGENTS.md when it holds for all code, a hook when it must never
   be missed.
3. Say in the reply which file got the line, so the user sees it in the diff.

The old lessons log in `tasks/` is an archive; its durable rules moved into the skills and
AGENTS.md. New corrections do not go there.
