#!/usr/bin/env bash
# Runs a Python hook script of this folder with the first Python 3.8+ that works.
# Without one the hook does nothing, so a machine without Python is never blocked.
# Usage: bash .claude/hooks/run_python.sh <script.py>   (the hook event arrives on stdin)
script="$(dirname "${BASH_SOURCE[0]}")/$1"
for python in python3 python; do
    if "$python" -c "import sys; sys.exit(sys.version_info < (3, 8))" >/dev/null 2>&1; then
        exec "$python" -I "$script"
    fi
done
exit 0
