---
name: unravel-lessons
description: >-
  Captures user corrections and recurring mistakes into tasks/lessons.md and
  proposes rule or skill updates for UnravelEngine. Use after any user correction,
  rejected approach, or repeated mistake pattern.
disable-model-invocation: true
---

# Unravel Lessons

Turn corrections into durable project memory.

## When to run

- User says the approach was wrong
- User rejects a diff or reverts a pattern
- Same mistake appears twice in a session
- A non-obvious engine convention is clarified

## Workflow

### 1. Append to lessons file

Add one entry to `tasks/lessons.md` (create file if missing):

```markdown
## YYYY-MM-DD — Short title

**Context:** What task was being done
**Mistake:** What went wrong
**Correct pattern:** What to do instead
**Files:** Key paths involved
```

Keep entries concise. One pattern per entry.

### 2. Evaluate escalation

| Symptom | Action |
|---------|--------|
| Style/naming violation | Already covered by `AGENTS.md` - cite the section in the lesson only |
| Repeated domain mistake | Propose addition to relevant `.agents/skills/unravel-*/SKILL.md` |
| Universal workflow issue | Propose addition to `AGENTS.md` |
| One-off edge case | Lesson entry only |

### 3. Propose skill/rule update

When escalating, draft a minimal addition (1-5 lines) — do not rewrite entire skills.

Ask user before modifying rules/skills unless they explicitly requested autonomous lesson integration.

## Entry template

```markdown
## 2026-07-09 — Menu bar alignment needs frame padding

**Context:** Right-aligning Stats toggle in viewport menu bar
**Mistake:** Used `CalcTextSize` only; MenuItem wider than label text
**Correct pattern:** `item_width = text_width + FramePadding.x * 2`; subtract `FramePadding.x` from avail width for `AlignedItem`
**Files:** `editor/editor/hub/panels/viewport_stats_overlay.cpp`
```

## Hard rules

- Never delete existing lessons
- Do not duplicate lessons already captured
- Lessons are factual patterns, not opinions
- Review `tasks/lessons.md` at session start for tasks in the same domain
