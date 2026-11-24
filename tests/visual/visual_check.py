"""Helpers for the visual tests: an editor of the test's own, MCP calls, PNG files, image comparison.

A visual test starts an editor on a port of its own with a fresh project, builds its scene over the
editor's MCP server, captures the Scene view and compares the captures. Python 3, standard library
only. The editor's MCP server and tools: skill unravel-editor-mcp.
"""

import base64
import json
import os
import struct
import subprocess
import time
import urllib.error
import urllib.request
import zlib
from pathlib import Path

EDITOR_EXECUTABLE = "unravel-editor.exe" if os.name == "nt" else "unravel-editor"


class VisualTestError(Exception):
    """A step of a visual test that could not run; the test stops."""


class Image:
    """An 8-bit RGB image, rows top to bottom."""

    def __init__(self, width: int, height: int, rgb: bytes):
        if len(rgb) != width * height * 3:
            raise VisualTestError("an RGB image of %d x %d needs %d bytes, not %d" % (width, height, width * height * 3, len(rgb)))
        self.width = width
        self.height = height
        self.rgb = rgb


class Comparison:
    """How two images differ inside a rectangle."""

    def __init__(self, pixels: int, differing: int, max_difference: int, mean_difference: float):
        self.pixels = pixels
        self.differing = differing
        self.max_difference = max_difference
        self.mean_difference = mean_difference

    @property
    def differing_fraction(self) -> float:
        return self.differing / self.pixels if self.pixels else 0.0


# PNG files

def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter_row(kind: int, row: bytearray, previous: bytearray, stride: int) -> None:
    count = len(row)
    if kind == 0:
        return
    if kind == 1:
        for i in range(stride, count):
            row[i] = (row[i] + row[i - stride]) & 0xFF
    elif kind == 2:
        for i in range(count):
            row[i] = (row[i] + previous[i]) & 0xFF
    elif kind == 3:
        for i in range(count):
            left = row[i - stride] if i >= stride else 0
            row[i] = (row[i] + ((left + previous[i]) >> 1)) & 0xFF
    elif kind == 4:
        for i in range(count):
            left = row[i - stride] if i >= stride else 0
            upper_left = previous[i - stride] if i >= stride else 0
            row[i] = (row[i] + _paeth(left, previous[i], upper_left)) & 0xFF
    else:
        raise VisualTestError("unknown PNG row filter %d" % kind)


def decode_png(data: bytes) -> Image:
    """Reads an 8-bit RGB or RGBA PNG without interlacing; alpha is dropped."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise VisualTestError("not a PNG file")
    position = 8
    width = height = 0
    color_type = 0
    compressed = bytearray()
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        chunk = data[position + 8:position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color_type not in (2, 6) or interlace != 0:
                raise VisualTestError("PNG with depth %d, color type %d, interlace %d is not read" % (depth, color_type, interlace))
        elif kind == b"IDAT":
            compressed += chunk
        elif kind == b"IEND":
            break
    channels = 3 if color_type == 2 else 4
    stride = width * channels
    raw = zlib.decompress(bytes(compressed))
    previous = bytearray(stride)
    rgb = bytearray(width * height * 3)
    for y in range(height):
        start = y * (stride + 1)
        row = bytearray(raw[start + 1:start + 1 + stride])
        _unfilter_row(raw[start], row, previous, channels)
        if channels == 3:
            rgb[y * width * 3:(y + 1) * width * 3] = row
        else:
            out = y * width * 3
            rgb[out:out + width * 3:3] = row[0::4]
            rgb[out + 1:out + width * 3:3] = row[1::4]
            rgb[out + 2:out + width * 3:3] = row[2::4]
        previous = row
    return Image(width, height, bytes(rgb))


def write_png(path: Path, image: Image) -> None:
    """Writes an RGB PNG."""
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    stride = image.width * 3
    raw = bytearray()
    for y in range(image.height):
        raw.append(0)
        raw += image.rgb[y * stride:(y + 1) * stride]
    header = struct.pack(">IIBBBBB", image.width, image.height, 8, 2, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


# Comparison

def compare(a: Image, b: Image, threshold: int, rect=None) -> Comparison:
    """Counts the pixels inside rect (x0, y0, x1, y1; the whole image when None) where a channel of a
    and b differs by more than threshold (0-255)."""
    if (a.width, a.height) != (b.width, b.height):
        raise VisualTestError("images of %d x %d and %d x %d cannot be compared" % (a.width, a.height, b.width, b.height))
    x0, y0, x1, y1 = rect if rect else (0, 0, a.width, a.height)
    pixels = differing = largest = total = 0
    for y in range(y0, y1):
        start = (y * a.width + x0) * 3
        end = (y * a.width + x1) * 3
        row_a = a.rgb[start:end]
        row_b = b.rgb[start:end]
        if row_a == row_b:
            pixels += x1 - x0
            continue
        for i in range(0, end - start, 3):
            d = max(abs(row_a[i] - row_b[i]), abs(row_a[i + 1] - row_b[i + 1]), abs(row_a[i + 2] - row_b[i + 2]))
            pixels += 1
            total += d
            if d > threshold:
                differing += 1
            if d > largest:
                largest = d
    return Comparison(pixels, differing, largest, total / pixels if pixels else 0.0)


def difference_image(a: Image, b: Image, gain: int) -> Image:
    """The per-channel difference of a and b times gain, clamped to 255."""
    table = bytes(min(255, d * gain) for d in range(256))
    out = bytearray(len(a.rgb))
    for i in range(len(a.rgb)):
        out[i] = table[abs(a.rgb[i] - b.rgb[i])]
    return Image(a.width, a.height, bytes(out))


def side_by_side(images: list, gap: int = 8) -> Image:
    """The images in one row, top-aligned, on black."""
    width = sum(image.width for image in images) + gap * (len(images) - 1)
    height = max(image.height for image in images)
    out = bytearray(width * height * 3)
    x = 0
    for image in images:
        for y in range(image.height):
            source = y * image.width * 3
            target = (y * width + x) * 3
            out[target:target + image.width * 3] = image.rgb[source:source + image.width * 3]
        x += image.width + gap
    return Image(width, height, bytes(out))


# The editor

def is_healthy(port: int) -> bool:
    """Whether an editor's MCP server answers on the port."""
    try:
        with urllib.request.urlopen("http://127.0.0.1:%d/health" % port, timeout=2) as response:
            return response.status == 200
    except (urllib.error.URLError, OSError):
        return False


class Editor:
    """An editor this test starts on its own port with its own project, and stops when done.

    Use as a context manager: the editor is stopped by the pid it was started with, also when the
    test fails, and never an editor someone else started."""

    def __init__(self, bin_dir: Path, project_dir: Path, port: int, log_path: Path, extra_arguments=()):
        self.bin_dir = Path(bin_dir).resolve()
        self.project_dir = Path(project_dir).resolve()
        self.port = port
        self.log_path = log_path
        self.extra_arguments = list(extra_arguments)
        self.process = None

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exception):
        self.stop()
        return False

    def start(self, timeout: float = 300.0) -> None:
        executable = self.bin_dir / EDITOR_EXECUTABLE
        if not executable.exists():
            raise VisualTestError("no editor at %s; build the editor target first" % executable)
        if is_healthy(self.port):
            raise VisualTestError("an editor already answers on port %d; pass another --port" % self.port)
        command = [str(executable), "-p", str(self.project_dir), "--mcp-port", str(self.port)] + self.extra_arguments
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        log = open(self.log_path, "w")
        if os.name == "nt":
            flags = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
            self.process = subprocess.Popen(command, cwd=self.bin_dir, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        else:
            self.process = subprocess.Popen(command, cwd=self.bin_dir, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.time() + timeout
        while not is_healthy(self.port):
            if self.process.poll() is not None:
                raise VisualTestError("the editor ended with code %d before it answered; its output: %s" % (self.process.returncode, self.log_path))
            if time.time() > deadline:
                raise VisualTestError("the editor did not answer on port %d within %d s" % (self.port, timeout))
            time.sleep(1)

    def stop(self) -> None:
        if self.process is None or self.process.poll() is not None:
            return
        if os.name == "nt":
            subprocess.run(["taskkill", "/PID", str(self.process.pid), "/T", "/F"], capture_output=True, check=False)
        else:
            self.process.terminate()
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()

    def call_raw(self, tool: str, arguments=None, timeout: float = 300.0) -> dict:
        """The tool's result: {"content": [...], "isError": ...}. Raises when the call is refused or fails."""
        body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": tool, "arguments": arguments or {}}}
        request = urllib.request.Request("http://127.0.0.1:%d/mcp" % self.port, json.dumps(body).encode("utf-8"),
                                         {"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                reply = json.load(response)
        except (urllib.error.URLError, OSError) as error:
            raise VisualTestError("%s: no answer from the editor: %s" % (tool, error))
        if "error" in reply:
            raise VisualTestError("%s was refused: %s" % (tool, reply["error"].get("message")))
        result = reply["result"]
        if result.get("isError"):
            raise VisualTestError("%s failed: %s" % (tool, _text_of(result)))
        return result

    def call(self, tool: str, arguments=None, timeout: float = 300.0):
        """The tool's text result, parsed as JSON when it is JSON."""
        text = _text_of(self.call_raw(tool, arguments, timeout))
        try:
            return json.loads(text)
        except ValueError:
            return text

    def capture(self, tool: str, arguments=None) -> Image:
        """The image a capture tool returns."""
        result = self.call_raw(tool, arguments)
        for block in result.get("content", []):
            if block.get("type") == "image":
                return decode_png(base64.b64decode(block["data"]))
        raise VisualTestError("%s returned no image" % tool)


def _text_of(result: dict) -> str:
    return "\n".join(block["text"] for block in result.get("content", []) if block.get("type") == "text")


def find_bin_dir(repository: Path, configuration: str) -> Path:
    """The runtime folder of a configured tree: <repository>/build/<configuration>/bin."""
    bin_dir = repository / "build" / configuration / "bin"
    if not (bin_dir / "data").is_dir():
        raise VisualTestError("%s has no data/ folder; build the editor and engine_data targets of %s first" % (bin_dir, configuration))
    return bin_dir
