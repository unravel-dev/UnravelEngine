# UnravelEngine Lessons

Patterns learned from corrections and recurring mistakes. Review relevant entries at session start.

---

## 2026-07-09 — Menu bar alignment needs frame padding

**Context:** Right-aligning Stats toggle in viewport menu bar
**Mistake:** Used `CalcTextSize` only; MenuItem is wider than label text
**Correct pattern:** `item_width = text_width + FramePadding.x * 2`; subtract `FramePadding.x` from avail width for `AlignedItem`
**Files:** `editor/editor/hub/panels/viewport_stats_overlay.cpp`
