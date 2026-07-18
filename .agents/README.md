# Agent configuration (tool-agnostic)

```text
AGENTS.md            # always-on instructions (single source of truth)
.agents/skills/      # portable SKILL.md packs
CLAUDE.md            # optional: @AGENTS.md for Claude Code
.cursor/skills  -> .agents/skills   (optional junction / symlink)
.claude/skills  -> .agents/skills   (optional junction / symlink)
```

There is no separate rules directory. All always-on rules live in `AGENTS.md`.

## Recreate skill links

```powershell
powershell -File tasks/link-agent-dirs.ps1
```

```bash
bash tasks/link-agent-dirs.sh
```

Edit skills only under `.agents/skills/`.
