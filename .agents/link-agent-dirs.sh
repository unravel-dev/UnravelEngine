#!/usr/bin/env bash
# Links .claude/skills and .cursor/skills to .agents/skills, the skills' source of truth.
# Usage: bash .agents/link-agent-dirs.sh [--if-missing]
#   --if-missing  keep links that already work and print nothing then (the SessionStart hook
#                 of .claude/settings.json runs it this way)
# On Windows (Git Bash) it runs link-agent-dirs.ps1, which makes directory junctions.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        switch=""
        if [ "${1:-}" = "--if-missing" ]; then switch="-IfMissing"; fi
        exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$here/link-agent-dirs.ps1")" $switch
        ;;
esac
cd "$root"
for tool in .claude .cursor; do
    link="$tool/skills"
    if [ "${1:-}" = "--if-missing" ] && [ -f "$link/README.md" ]; then
        continue
    fi
    if [ -e "$link" ] && [ ! -L "$link" ]; then
        echo "Skipped $link: a real folder, not a link"
        continue
    fi
    mkdir -p "$tool"
    ln -sfn ../.agents/skills "$link"
    echo "Linked $link -> .agents/skills; skills load after /reload-skills or in the next session"
done
