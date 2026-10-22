#!/usr/bin/env bash
# Generate the API docs:
#   - Engine C++ API: Doxygen with Engine-Doxyfile -> docs/engine-api/html
#   - Scripting C# API: docfx with docfx.json -> docs/_site
#
# Prerequisites: doxygen (Engine docs); docfx (Scripting docs):
# dotnet tool install -g docfx
#
# Usage (from repo root):
#   bash docs/generate-api-docs.sh
#   bash docs/generate-api-docs.sh --script-only
#   bash docs/generate-api-docs.sh --engine-only

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCS="${ROOT}/docs"

RUN_SCRIPT=1
RUN_ENGINE=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --script-only) RUN_ENGINE=0 ;;
    --engine-only) RUN_SCRIPT=0 ;;
    -h|--help)
      sed -n '2,12p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
  shift
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Required command not found on PATH: $1" >&2
    exit 1
  }
}

# Neither Doxygen nor docfx deletes old files from its output, so each build
# starts from an empty folder.
if [[ "${RUN_ENGINE}" -eq 1 ]]; then
  need_cmd doxygen
  echo "=== doxygen Engine-Doxyfile ==="
  rm -rf "${DOCS}/engine-api"
  pushd "${DOCS}" >/dev/null
  doxygen Engine-Doxyfile
  popd >/dev/null
fi

if [[ "${RUN_SCRIPT}" -eq 1 ]]; then
  need_cmd docfx
  echo "=== docfx docfx.json ==="
  rm -rf "${DOCS}/api" "${DOCS}/_site"
  # docfx resolves the paths in docfx.json against the file's own folder.
  docfx "${DOCS}/docfx.json"
fi

echo "Done."
if [[ "${RUN_ENGINE}" -eq 1 ]]; then
  echo "  Engine C++ API:   docs/engine-api/html"
fi
if [[ "${RUN_SCRIPT}" -eq 1 ]]; then
  echo "  Scripting C# API: docs/_site"
fi
