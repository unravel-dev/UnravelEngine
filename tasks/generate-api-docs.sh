#!/usr/bin/env bash
# Generate API docs: Doxygen (Script + Engine) then moxygen -> Markdown.
# Moxygen does not run Doxyfiles; this script chains both tools.
#
# Prerequisites: doxygen; Node.js 20+ with moxygen on PATH or npm (npx).
#
# Usage (from repo root):
#   bash tasks/generate-api-docs.sh
#   bash tasks/generate-api-docs.sh --script-only
#   bash tasks/generate-api-docs.sh --engine-only
#   bash tasks/generate-api-docs.sh --skip-doxygen
#   bash tasks/generate-api-docs.sh --skip-moxygen

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCS="${ROOT}/docs"

RUN_SCRIPT=1
RUN_ENGINE=1
SKIP_DOXYGEN=0
SKIP_MOXYGEN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --script-only) RUN_ENGINE=0 ;;
    --engine-only) RUN_SCRIPT=0 ;;
    --skip-doxygen) SKIP_DOXYGEN=1 ;;
    --skip-moxygen) SKIP_MOXYGEN=1 ;;
    -h|--help)
      sed -n '2,14p' "$0"
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

run_moxygen() {
  local xml_dir="$1"
  local output_pattern="$2"
  local language="$3"
  if [[ ! -f "${xml_dir}/index.xml" ]]; then
    echo "Doxygen XML missing (expected index.xml): ${xml_dir}" >&2
    exit 1
  fi
  mkdir -p "$(dirname "${output_pattern}")"
  local args=(--html-anchors --classes --language "${language}" --output "${output_pattern}" "${xml_dir}")
  if command -v moxygen >/dev/null 2>&1; then
    echo "moxygen ${args[*]}"
    moxygen "${args[@]}"
  else
    need_cmd npx
    echo "npx --yes moxygen ${args[*]}"
    npx --yes moxygen "${args[@]}"
  fi
}

if [[ "${SKIP_DOXYGEN}" -eq 0 ]]; then
  need_cmd doxygen
  pushd "${DOCS}" >/dev/null
  if [[ "${RUN_SCRIPT}" -eq 1 ]]; then
    echo "=== doxygen Script-Doxyfile ==="
    doxygen Script-Doxyfile
  fi
  if [[ "${RUN_ENGINE}" -eq 1 ]]; then
    echo "=== doxygen Engine-Doxyfile ==="
    doxygen Engine-Doxyfile
  fi
  popd >/dev/null
fi

if [[ "${SKIP_MOXYGEN}" -eq 0 ]]; then
  if [[ "${RUN_SCRIPT}" -eq 1 ]]; then
    echo "=== moxygen script-api ==="
    # C# sources; moxygen has no csharp templates (only cpp/java). java is closer.
    run_moxygen \
      "${DOCS}/script-api/xml" \
      "${DOCS}/markdown/script-api/%s.md" \
      java
  fi
  if [[ "${RUN_ENGINE}" -eq 1 ]]; then
    echo "=== moxygen engine-api ==="
    run_moxygen \
      "${DOCS}/engine-api/xml" \
      "${DOCS}/markdown/engine-api/%s.md" \
      cpp
  fi
fi

echo "Done."
echo "  HTML:     docs/script-api/html , docs/engine-api/html"
echo "  XML:      docs/script-api/xml  , docs/engine-api/xml"
echo "  Markdown: docs/markdown/script-api , docs/markdown/engine-api"
