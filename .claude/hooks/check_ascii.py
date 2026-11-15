"""PostToolUse hook: reports non-ASCII characters in text an agent wrote into the repository.

The repository is ASCII only (AGENTS.md). Only the new text of an Edit or Write is checked,
so existing text is never flagged. Files outside the project, under deps/3rdparty/ and in
build trees are skipped. A finding goes back to the agent as a blocking reason.
"""

import json
import os
import sys

EXCLUDED_PREFIXES = ("deps/3rdparty/", "build/", ".claude/worktrees/")
MAX_REPORTED_CHARACTERS = 8
MAX_SNIPPET_LENGTH = 60


def read_event() -> dict:
    """Reads the hook event JSON from stdin."""
    return json.loads(sys.stdin.buffer.read().decode("utf-8"))


def collect_new_texts(tool_input: dict) -> list:
    """Returns the texts the tool call adds: Write content, Edit new_string, MultiEdit edits."""
    texts = [tool_input.get("content", ""), tool_input.get("new_string", "")]
    texts.extend(edit.get("new_string", "") for edit in tool_input.get("edits") or [])
    return [text for text in texts if text]


def get_repository_path(file_path: str, project_dir: str):
    """Returns file_path relative to the project with forward slashes, or None when outside."""
    if not file_path or not project_dir:
        return None
    try:
        relative = os.path.relpath(os.path.abspath(file_path), os.path.abspath(project_dir))
    except ValueError:
        return None
    relative = relative.replace("\\", "/")
    if relative == ".." or relative.startswith("../"):
        return None
    return relative


def find_non_ascii(texts: list) -> list:
    """Returns (character, snippet) for every non-ASCII character, with its line."""
    findings = []
    for text in texts:
        for line in text.splitlines():
            snippet = line.strip()[:MAX_SNIPPET_LENGTH]
            findings.extend((c, snippet) for c in line if ord(c) > 127)
    return findings


def describe(findings: list) -> str:
    """Formats the distinct characters as U+XXXX with one example line."""
    distinct = []
    for character, _ in findings:
        if character not in distinct:
            distinct.append(character)
    codes = ", ".join("U+%04X" % ord(c) for c in distinct[:MAX_REPORTED_CHARACTERS])
    example = findings[0][1].encode("ascii", "backslashreplace").decode("ascii")
    return "%s; first line: %s" % (codes, example)


def main() -> int:
    try:
        event = read_event()
    except (ValueError, UnicodeDecodeError):
        return 0
    tool_input = event.get("tool_input") or {}
    project_dir = os.environ.get("CLAUDE_PROJECT_DIR") or event.get("cwd", "")
    path = get_repository_path(tool_input.get("file_path", ""), project_dir)
    if path is None or path.startswith(EXCLUDED_PREFIXES):
        return 0
    findings = find_non_ascii(collect_new_texts(tool_input))
    if not findings:
        return 0
    reason = ("%s: the new text has non-ASCII characters (%s). The repository is ASCII only "
              "(AGENTS.md): replace them, for example '-' for dashes, '->' for arrows and "
              "straight quotes." % (path, describe(findings)))
    print(json.dumps({"decision": "block", "reason": reason}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
