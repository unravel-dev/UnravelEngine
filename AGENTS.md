# UnravelEngine - Agent Instructions

Cross-tool project guide for any coding agent. Domain workflows live in **on-demand
skills** under `.agents/skills/` - do not load them all at once.

## Layout

| Path | Role |
|------|------|
| `AGENTS.md` | Always-on project instructions (this file) |
| `.agents/skills/` | Portable skills (`*/SKILL.md`) - source of truth |
| `CLAUDE.md` | Optional one-line import of this file for Claude Code |
| `.cursor/skills`, `.claude/skills` | Optional junctions to `.agents/skills` (see `.agents/README.md`) |

## Precedence

1. Explicit user instructions in the current chat  
2. This file (`AGENTS.md`)  
3. On-demand skills under `.agents/skills/`  

Skills add procedures and domain checklists. They do not override hard rules here
unless the user says so.

## Repository map

| Path | Role |
|------|------|
| `engine/` | Runtime library: ECS, rendering, assets, scripting, physics, audio |
| `editor/` | Editor executable and ImGui hub (must not be required at game runtime) |
| `game/` | Game / player runner |
| `engine_data/`, `editor_data/` | Shipped data (shaders, scripts, UI); rebuild `engine_data` / `editor_data` when changed |
| `deps/` | Third-party; use, almost never modify (`deps/3rdparty/` especially) |
| `cmake/`, `CMakeLists.txt`, `CMakePresets.json` | Build system |
| `.agents/skills/` | Domain and workflow skills |
| `tasks/` | Optional agent notes (`todo.md`, `lessons.md`) |

Bootstrap order and system list: see skill `unravel-triage`.

## Agent behavior

- Act as a principal C++ / engine engineer with many years of experience.
- Keep going until the user's query is completely resolved. Only stop when the
  problem is solved.
- If unsure about file content or codebase structure, use tools to read and gather
  information. Do not guess or invent answers.
- Plan extensively before each tool call, and reflect on outcomes before the next.
  Do not solve the problem with tool calls alone - think between steps.
- Prefer minimal diffs. Touch only what the task requires.
- Never modify `deps/3rdparty/` unless there is no alternative.
- After a user correction, capture the pattern in `tasks/lessons.md` (see skill
  `unravel-lessons`).

## Start here on non-trivial work

Skip triage for trivial one-file edits with an obvious touch point.

1. Read skill **`unravel-triage`** (`.agents/skills/unravel-triage/SKILL.md`).
2. Classify domain, list files to read, then open the matching domain / workflow skill.
3. For large or cross-cutting design, use **`unravel-architect`**.
4. Before calling done, use **`unravel-build-verify`** when code or data targets changed.

Skill catalog: `.agents/skills/README.md`.

## Hard project rules

- Match existing naming: `snake_case` for types, files, and functions in this codebase.
- Engine vs editor: code under `editor/` must not be required at game runtime.
- Play mode: respect `play_mode` phases; do not run edit-only mutations while playing.
- Serialization / meta / prefabs / C# parity: check the triage skill's cross-cutting
  list before adding components or reflected fields.
- Prefer English in code and comments; ASCII only in source.
- Do not create git commits, amend, push, or open PRs unless the user explicitly asks.
- Never update git config; never force-push `main`/`master`; avoid destructive git
  commands unless the user explicitly requests them.

## Code quality

- Verify information before presenting it. Do not assume or speculate without evidence.
- Make changes file by file when that helps review.
- Never use apologies.
- Do not add "understanding" feedback in comments or documentation.
- Do not suggest whitespace-only changes.
- Do not invent changes beyond what was requested.
- Do not ask for confirmation of information already in context.
- Preserve unrelated code and existing structure.
- Prefer a single coherent edit per file over multi-step rewrite instructions.
- Do not ask the user to verify implementations that are already visible in context.
- Do not suggest updates when no modification is needed.
- Link to real project files, not placeholder docs.
- Do not show or discuss the current implementation unless asked.
- Prefer root-cause fixes over temporary hacks.
- Keep changes simple and local; avoid drive-by refactors.

## Clean code

- Named constants over magic numbers; keep constants near the top of the file or
  in a dedicated constants location.
- Names reveal purpose; avoid unclear abbreviations.
- Comments explain *why*, not *what*; document APIs, complex algorithms, and
  non-obvious side effects.
- Single responsibility; small focused functions.
- DRY: extract repeated logic; keep a single source of truth.
- Keep related code together; consistent file and folder naming.
- Hide implementation details; expose clear interfaces.
- Refactor continuously; leave touched code cleaner than you found it.
- Write tests before fixing bugs when practical; cover edge cases.
- Clear commit messages; small focused commits; meaningful branch names.

## Workflow orchestration

### Plan first

- Enter a planning mindset for any non-trivial task (3+ steps or architectural decisions).
- If something goes sideways, stop and re-plan - do not keep pushing a failing approach.
- Use planning for verification steps, not only for building.
- Write clear specs upfront to reduce ambiguity.

### Subagents

Use only when the tool supports them and context isolation helps.

**Do use for:** broad or parallel codebase research, noisy shell/build logs,
independent verification. One clear goal per subagent. The prompt must be
self-contained (paths, constraints, definition of done) - subagents do not see
the parent chat history.

**Do not use for:** single-file edits, work that needs the full conversation,
or short procedures a skill already covers end-to-end. Prefer skills over
subagents for repeatable checklists.

**Model class (not product names):**

| Role | Model class |
|------|-------------|
| Explore / search | Fast / cheap |
| Shell / log-heavy | Fast / cheap |
| Implement / multi-file refactor | Inherit parent (or stronger if parent is light) |
| Review / verify | Inherit or strong; separate context from the implementer |

Omit an explicit model unless the user requests one. Prefer inherit / tool
defaults so user Settings are not overridden. Do not hardcode vendor model IDs
in prompts or in this file - they change often.

### Self-improvement

- After any correction from the user: update `tasks/lessons.md` with the pattern.
- Write rules that prevent the same mistake.
- Review lessons at session start when relevant.

### Verification before done

- Never mark a task complete without proving it works.
- Diff behavior against the baseline when relevant.
- Ask: would a staff engineer approve this?
- For UnravelEngine, prefer concrete checks:
  - Compile the touched target (`engine`, `editor`, and/or `game`).
  - Manual or MCP smoke when editor/scene behavior changed.
- Details: skill `unravel-build-verify`.

### Elegance (balanced)

- For non-trivial changes, pause and ask if there is a more elegant way.
- If a fix feels hacky, prefer the elegant solution once you understand the root cause.
- Skip this for simple, obvious fixes - do not over-engineer.

### Autonomous bug fixing

- When given a bug report: fix it. Point at logs, errors, or failing tests, then resolve them.
- Fix failing CI when it is in scope, without waiting for step-by-step instructions.

### Task tracking

1. Plan first: write a plan to `tasks/todo.md` with checkable items when helpful.
2. Verify the plan before large implementation when stakes are high.
3. Track progress; mark items complete as you go.
4. Explain changes at a high level as you proceed.
5. Document results in `tasks/todo.md` when useful.
6. Capture lessons in `tasks/lessons.md` after corrections.

### Core principles

- Simplicity first: smallest change that solves the problem.
- No laziness: find root causes; senior-developer standards.
- Minimal impact: touch only what is necessary.

## Build

- Prefer CMake commands.
- First check whether an existing configured build directory is available.
- Reuse the existing generator and configuration where possible.
- Common output: `build/RelWithDebInfo/`, `build/Debug/` (or the active preset under `build/`).
- Details and checklists: skill `unravel-build-verify`.

## C++ guidelines

Apply when editing C/C++ or CMake.

### Basic principles

- Use English for all code and documentation.
- Always declare the type of each variable and function (parameters and return value).
- Create necessary types and classes.
- Use Doxygen-style comments for public classes and methods.
- Do not leave blank lines within a function.
- Follow the one-definition rule (ODR).
- Use only ASCII in code and comments.

### Nomenclature

- Use the case already existent in the file.
- Use `snake_case` for classes, structures, variables, functions, methods, and file names.
- Use `ALL_CAPS` for constants and macros.
- Use `UPPERCASE` for environment variables.
- Avoid magic numbers; define named constants.
- Start each function with a verb.
- Use verb-style booleans: `is_loading`, `has_error`, `can_delete`, etc.
- Prefer complete words over abbreviations, except standard ones (API, URL, etc.) and
  well-known locals (`i`, `j`, `k`, `err`, `ctx`, `req`, `res`).

### Functions

- Short, single-purpose functions (aim under ~20 instructions).
- Name with a verb plus object.
- Booleans: `is_x` / `has_x` / `can_x`. Side-effecting void: `execute_x` / `save_x`, etc.
- Avoid nesting via early returns and extraction to helpers.
- Prefer standard algorithms (`std::for_each`, `std::transform`, `std::find`, ...) over
  hand-rolled nesting.
- Lambdas for simple ops; named functions for non-trivial ops.
- Prefer default arguments over null checks where appropriate.
- Reduce parameter lists with structs/classes for inputs and outputs.
- Keep a single level of abstraction per function.

### Data

- Prefer composite types over abusing primitives.
- Prefer validation inside types over scattered checks.
- Prefer immutability; use `const` and `constexpr` appropriately.
- Use `std::optional` (or project `hpp::optional`) for possibly absent values.

### Classes

- SOLID; prefer composition over inheritance.
- Interfaces as abstract classes or concepts.
- Small classes: aim under ~200 instructions, ~10 public methods, ~10 properties.
- Rule of Five or Rule of Zero for resource management.
- Private members; const-correct methods.

### Exceptions and errors

- Exceptions for unexpected failures.
- When catching: fix, add context, or rethrow to a global handler.
- Prefer `hpp::optional` or error codes for expected failures.

### Memory

- Prefer smart pointers over raw pointers.
- RAII; prefer `std::vector` and standard containers over C arrays.

### Testing

- Arrange-Act-Assert; clear names (`input_x`, `mock_x`, `actual_x`, `expected_x`).
- Unit tests for public functions; integration tests per module.
- Test doubles for expensive dependencies; Given-When-Then where useful.

### Project structure

- Modular architecture; separate `.h` / `.cpp`.
- Organize with namespaces; foundational pieces in core-style namespaces as existing code does.
- `deps/` holds third-party libraries - use them, almost never modify them.

### Standard library and concurrency

- Prefer the C++ standard library (`std::string`, containers, `std::filesystem` as `fs`,
  `std::chrono`, etc.).
- Use project helpers where established (`hpp::optional`, `hpp::variant`, `hpp::string_view`).
- Prefer task-based parallelism; synchronize with mutexes/atomics; avoid data races.

## Skills discovery

After clone, if your tool expects skills under `.cursor/skills` or `.claude/skills`, run:

```powershell
powershell -File tasks/link-agent-dirs.ps1
```

```bash
bash tasks/link-agent-dirs.sh
```

Edit skills only under `.agents/skills/`.
