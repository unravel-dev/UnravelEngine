"""Starts, waits for, calls and stops an Unravel editor of your own, on a port of your own.

From the repository root:
    python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py start <bin_dir> <project_dir> --port N
           [--novsync] [--renderer NAME] [--profile developer|user] [--arg=--option ...]
    python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py wait --port N [--timeout SECONDS]
    python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py call <tool> [JSON arguments] --port N
           [--image PATH] [--timeout SECONDS]
    python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py stop --port N

`start` refuses a port that already answers and records the pid it started; `stop` ends only
that recorded pid and what it started, never another editor. The pid file and the editor's
console output live in <temp>/unravel-editor-ctl/. vsync stays on unless --novsync (GPU timing
only): an uncapped editor slows the builds and test runs beside it.
"""

import argparse
import base64
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

STATE_FOLDER = Path(tempfile.gettempdir()) / "unravel-editor-ctl"
EDITOR_EXECUTABLE = "unravel-editor.exe" if os.name == "nt" else "unravel-editor"
HEALTH_TIMEOUT_SECONDS = 2
HEALTH_POLL_SECONDS = 2
STOP_WAIT_SECONDS = 30


def get_pid_file(port: int) -> Path:
    return STATE_FOLDER / ("editor_%d.pid" % port)


def is_healthy(port: int) -> bool:
    """Whether an editor's MCP server answers on the port."""
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/health" % port, timeout=HEALTH_TIMEOUT_SECONDS) as response:
            return response.status == 200
    except (urllib.error.URLError, OSError):
        return False


def is_process_alive(pid: int) -> bool:
    """Whether a process with this pid runs."""
    if os.name == "nt":
        listing = subprocess.run(["tasklist", "/FI", "PID eq %d" % pid, "/NH"], capture_output=True, text=True)
        return str(pid) in listing.stdout
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def get_recorded_pid(port: int) -> int:
    """The pid this script started on the port and that still runs, or 0."""
    pid_file = get_pid_file(port)
    if not pid_file.exists():
        return 0
    pid = int(pid_file.read_text().strip())
    return pid if is_process_alive(pid) else 0


def start(arguments: argparse.Namespace) -> int:
    if is_healthy(arguments.port):
        print("an editor already answers on port %d; pick another port" % arguments.port)
        return 1
    recorded = get_recorded_pid(arguments.port)
    if recorded:
        print("editor pid %d started for port %d still runs; stop it first" % (recorded, arguments.port))
        return 1
    bin_dir = Path(arguments.bin_dir).resolve()
    command = [str(bin_dir / EDITOR_EXECUTABLE), "-p", str(Path(arguments.project_dir).resolve()),
               "--mcp-port", str(arguments.port)]
    if arguments.novsync:
        command.append("--novsync")
    if arguments.renderer:
        command += ["--renderer", arguments.renderer]
    if arguments.profile:
        command += ["--mcp-profile", arguments.profile]
    command += arguments.arg
    STATE_FOLDER.mkdir(parents=True, exist_ok=True)
    log = open(STATE_FOLDER / ("editor_%d.log" % arguments.port), "w")
    if os.name == "nt":
        # A hidden console rather than none: console tools the editor runs share it instead of
        # each opening a terminal window on the desktop.
        flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
        process = subprocess.Popen(command, cwd=bin_dir, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
    else:
        process = subprocess.Popen(command, cwd=bin_dir, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    get_pid_file(arguments.port).write_text(str(process.pid))
    print("started pid %d: %s" % (process.pid, " ".join(command)))
    return 0


def wait(arguments: argparse.Namespace) -> int:
    deadline = time.time() + arguments.timeout
    while time.time() < deadline:
        if is_healthy(arguments.port):
            print("healthy")
            return 0
        time.sleep(HEALTH_POLL_SECONDS)
    print("no answer on port %d after %d s" % (arguments.port, arguments.timeout))
    return 1


def write_images(content: list, image_path: str) -> None:
    """Writes the first image block of a tool result to image_path."""
    images = [block for block in content if block.get("type") == "image"]
    if images and image_path:
        Path(image_path).write_bytes(base64.b64decode(images[0]["data"]))
        print("image written to %s" % image_path)


def call(arguments: argparse.Namespace) -> int:
    tool_arguments = json.loads(arguments.arguments) if arguments.arguments else {}
    body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": arguments.tool, "arguments": tool_arguments}}
    request = urllib.request.Request("http://127.0.0.1:%d/mcp" % arguments.port, json.dumps(body).encode("utf-8"),
                                     {"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=arguments.timeout) as response:
            reply = json.load(response)
    except (urllib.error.URLError, OSError) as error:
        print("no answer on port %d: %s" % (arguments.port, error))
        return 1
    if "error" in reply:
        print("refused: %s" % reply["error"].get("message"))
        return 1
    result = reply["result"]
    write_images(result.get("content", []), arguments.image)
    texts = [block["text"] for block in result.get("content", []) if block.get("type") == "text"]
    print("\n".join(texts))
    return 1 if result.get("isError") else 0


def stop(arguments: argparse.Namespace) -> int:
    pid_file = get_pid_file(arguments.port)
    if not pid_file.exists():
        print("no editor recorded for port %d" % arguments.port)
        return 1
    pid = int(pid_file.read_text().strip())
    if not is_process_alive(pid):
        pid_file.unlink()
        print("editor pid %d had already ended" % pid)
        return 0
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], check=False)
    else:
        try:
            os.killpg(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.time() + STOP_WAIT_SECONDS
    while is_healthy(arguments.port) and time.time() < deadline:
        time.sleep(1)
    pid_file.unlink()
    print("stopped pid %d" % pid)
    return 0


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    start_parser = commands.add_parser("start", help="start an editor on your port")
    start_parser.add_argument("bin_dir")
    start_parser.add_argument("project_dir")
    start_parser.add_argument("--novsync", action="store_true", help="render uncapped (GPU timing only)")
    start_parser.add_argument("--renderer", default="")
    start_parser.add_argument("--profile", default="", choices=["", "developer", "user"])
    start_parser.add_argument("--arg", action="append", default=[],
                              help="one more editor launch argument, repeatable; write it as --arg=--option")
    wait_parser = commands.add_parser("wait", help="wait until the editor answers")
    wait_parser.add_argument("--timeout", type=int, default=300)
    call_parser = commands.add_parser("call", help="call one MCP tool")
    call_parser.add_argument("tool")
    call_parser.add_argument("arguments", nargs="?", default="", help="tool arguments as JSON")
    call_parser.add_argument("--image", default="", help="where to write a capture's PNG")
    call_parser.add_argument("--timeout", type=int, default=300)
    commands.add_parser("stop", help="stop the editor this script started on your port")
    for command in (start_parser, wait_parser, call_parser, commands.choices["stop"]):
        command.add_argument("--port", type=int, required=True, help="your own port, never another session's")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    handlers = {"start": start, "wait": wait, "call": call, "stop": stop}
    return handlers[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
