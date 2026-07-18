#!/usr/bin/env bash
# Recreate Cursor / Claude skill symlinks to .agents/skills (Unix).
# Run from repo root:  bash tasks/link-agent-dirs.sh
set -euo pipefail
cd "$(dirname "$0")/.."

mkdir -p .cursor .claude
ln -sfn ../.agents/skills .cursor/skills
ln -sfn ../.agents/skills .claude/skills
rm -f .cursor/rules
echo "OK .cursor/skills -> .agents/skills"
echo "OK .claude/skills -> .agents/skills"
