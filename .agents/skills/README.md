# Writing UnravelEngine skills

Source of truth: `.agents/skills/<name>/`. Area owners and where other knowledge goes:
[../README.md](../README.md).

## What a skill holds

What an agent cannot get quickly by reading the code: contracts, traps, the order of steps,
and how to verify a change. Leave out what the model already does well (generic C++, generic
debugging, clean code) and what one search finds (long file lists, code it can read).

## Layout

```text
<name>/SKILL.md      the skill: under 300 lines (hard limit 500)
<name>/<topic>.md    procedures and deep reference, linked from SKILL.md, one level deep;
                     a table of contents at the top when longer than 100 lines
<name>/scripts/      helper scripts the skill runs (checkers, editor control)
```

A helper that goes with a skill lives in its `scripts/` folder, not in `tasks/`:

- Python 3 with the standard library only, run from the repository root
  (`python .agents/skills/<name>/scripts/<script>.py`); repository paths come from the
  script's own location, machine paths from arguments or environment variables.
- A module docstring with the usage, what it checks and its exit code (1 on a finding); the
  skill names the script, when to run it and what a finding means.
- A checker is proven able to fail (a control with a known bad input) before it is relied on.
- Scratch output goes to the temp folder, never next to the script.

## Frontmatter

```yaml
---
name: unravel-<area>
description: >-
  <What it covers, in the words a task uses>. Use when <tasks, symptoms, file types>.
---
```

- `name` matches the folder.
- `description` is a trigger, not a summary: third person, the words a request would use
  first, what and when, under 500 characters. It is the only thing that makes an agent
  pick the skill, so it names the area's files and symptoms.
- No `paths` key: Claude Code hides a skill with `paths` from its skill list and from the
  `/` menu until a matching file has been read, so a task that starts from a symptom never
  sees it. The area globs live in the owner table of `../README.md`.
- `disable-model-invocation: true` only for a skill with side effects that must run on the
  user's command alone (none today).

## Body order

1. One short paragraph: what the skill covers and the directories it owns.
2. `## Gotchas` - the non-obvious contracts and traps, one rule per bullet, each one
   checkable, at most one clause of why. This is the most valuable section and goes first:
   after a context compaction only the first ~5k tokens of a skill come back.
3. `## How it works` - only what reading the code does not quickly show: ordering,
   lifetimes, threading, serialization, ownership between systems.
4. `## Procedures` - short steps, or markdown links to the reference files of the folder.
5. `## Verify` - the `unravel-tests` suites, targets to build, and the smoke check.
6. `## Entry points` - the files to open first, at most about ten rows.

## Writing rules

- ASCII only.
- Current design only: no dates, no "legacy", "previously", "no longer", "was fixed", no
  incident stories. A gotcha states the rule.
- Checkable statements. No emphasis words (IMPORTANT, CRITICAL, capitalized NEVER) and no
  bold used as an alarm; a rule that must never be missed becomes a hook instead.
- Every repository path cited exists; forward slashes. An example path that does not exist
  names its file `my_*` (`my_component.h`). No links to `tasks/` (local, not in git) or to
  personal memory.
- Nothing AGENTS.md already says, and no generic advice.
- One owner per fact: when another skill owns it, name that skill instead of repeating it.
- Reference files are named for what they do (`add-render-pass.md`, `shader-change.md`).

## Keeping skills true

- When code moves or a contract changes, update the owning skill in the same change.
- When the user corrects an agent, the rule goes into the owning skill's Gotchas
  (`../README.md`, "When the user corrects an agent").
- `unravel-tests --suite "agent docs"` checks that every path cited in `AGENTS.md`, the
  nested `AGENTS.md` files and the skills exists, and that each skill's frontmatter names
  its folder and has no `paths` key.

## Catalog

| Skill | Use when |
|-------|----------|
| `unravel-rendering` | renderer, passes, shaders, GI, shadows, post, GPU features |
| `unravel-materials` | material graphs, `.mat`, material instances |
| `unravel-ecs-component` | components, systems, scenes, play mode, adding a component |
| `unravel-editor-panel` | editor panels, inspectors, gizmos, undo |
| `unravel-assets` | importers, `.meta`, asset compiler, loading |
| `unravel-prefabs` | prefab assets, instances, overrides |
| `unravel-scripting` | C# API, script glue, dotnetpp |
| `unravel-physics` | bodies, shapes, joints, character controllers |
| `unravel-animation` | skeletal animation, clips, blend spaces, IK |
| `unravel-audio` | audio sources, listeners, audiopp |
| `unravel-ui-rmlui` | in-game RmlUi documents |
| `unravel-build-verify` | building, tests, CI, data targets |
| `unravel-debugging` | bugs, crashes, profiling, A/B measurements |
| `unravel-editor-mcp` | starting and driving an editor over MCP, MCP server code |
| `unravel-architect` | designs that cross several areas |
