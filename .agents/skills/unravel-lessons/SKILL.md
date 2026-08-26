---
name: unravel-lessons
description: >-
  Captures user corrections and recurring mistakes into tasks/lessons.md and
  proposes rule or skill updates for UnravelEngine. Use after any user correction,
  rejected approach, or repeated mistake pattern.
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
## YYYY-MM-DD - Short title

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

### 3. Promote to skill (do not let lessons rot)

A lesson that states a durable engine contract (an API ordering rule, a platform
gotcha, a lifecycle invariant) belongs in the matching domain skill, not only in the
log - lessons.md is written once and rarely re-read; skills are read at task start.
When the lesson is domain-shaped:

1. Fold a 1-5 line rule into the matching `.agents/skills/unravel-*/SKILL.md`
   (usually its "Common mistakes" or a contract section) - do not rewrite the skill
2. Mark the lesson entry `(promoted to <skill>)` so later passes skip it

Ask user before modifying rules/skills unless they explicitly requested autonomous
lesson integration - but always ask rather than silently leaving the lesson
unpromoted; that is how skills went stale before (fixed 2026-08-26).

## Entry template

```markdown
## 2026-07-09 - Menu bar alignment needs frame padding

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
