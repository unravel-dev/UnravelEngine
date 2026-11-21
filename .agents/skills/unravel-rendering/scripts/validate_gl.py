"""Runs glslangValidator over the desktop GLSL of compiled shaders.

shaderc writes GLSL without validating it, so OpenGL drivers reject keywords used as names,
rsqrt, expressions in layout ids and similar only at run time, and the pass draws nothing.
This finds them offline, for every variant of every compiled .eshader and graph material.

A compiled `.gl` file is a binary document (engine/core/serialization/archives/tagged_binary.hpp)
holding a list of variants (engine/engine/meta/rendering/shader_variants.hpp), each with a name,
the shaderc bytes and its varyings; a graph material's file holds two such lists (read_variants).
The bytes are a bgfx shader binary, version 12 or newer: magic, hash in, hash out, raw SRV and UAV
masks, uint16 uniform count, the uniforms, uint32 code size, the code.

Usage (from the repository root):
    python .agents/skills/unravel-rendering/scripts/validate_gl.py [folder] [--filter TEXT] [--show N]
             [--validator PATH]
The folder defaults to engine_data/compiled/shaders; a runtime tree's
build/<Config>/bin/data/engine/compiled/shaders holds what an editor compiled since.
glslangValidator comes from --validator, $VULKAN_SDK/Bin or PATH (the Vulkan SDK ships it).
Exit code 1 when a variant fails.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[4]
DEFAULT_FOLDER = REPOSITORY / "engine_data" / "compiled" / "shaders"
STAGES = {b"CSH": "comp", b"FSH": "frag", b"VSH": "vert"}
MIN_SHADER_VERSION = 12
UNIFORM_NUM_WIDENED_VERSION = 14
# bgfx's desktop OpenGL backend prepends this line to code without one when it loads a shader
# (renderer_gl.cpp, BGFX_CONFIG_RENDERER_OPENGL is at least 43); shaderc declares the outputs.
DESKTOP_GLSL_VERSION = "#version 430"
# glslangValidator lets these through as identifiers; NVIDIA's driver does not.
DRIVER_REJECTED_NAMES = ("packed",)
LAYOUT_QUALIFIERS = re.compile(r"\blayout\s*\([^)]*\)")


class reader:
    """Little-endian cursor over a byte buffer."""

    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def read(self, layout: str):
        value = struct.unpack_from("<" + layout, self.data, self.offset)
        self.offset += struct.calcsize("<" + layout)
        return value[0]

    def read_bytes(self, size: int) -> bytes:
        value = self.data[self.offset:self.offset + size]
        if len(value) != size:
            raise ValueError("truncated at byte %d" % self.offset)
        self.offset += size
        return value

    def read_string(self) -> str:
        return self.read_bytes(self.read("Q")).decode("utf-8", "replace")


DOCUMENT_MAGIC = 0x31425455
DOCUMENT_PREAMBLE = 32
NUMBER_LAYOUTS = {3: "b", 4: "B", 5: "h", 6: "H", 7: "i", 8: "I", 9: "q", 10: "Q", 11: "f", 12: "d"}
OBJECT, ARRAY, PACKED, NUMBERS = 15, 16, 17, 18


def read_varint(cursor: reader) -> int:
    value = 0
    shift = 0
    while True:
        byte = cursor.read("B")
        value |= (byte & 0x7F) << shift
        if byte < 0x80:
            return value
        shift += 7


def read_value(cursor: reader, names: list):
    """One value of a binary document: an object as a dict, an array or a value of numbers as a list, a blob
    as bytes. A packed list holds rows only its C++ type reads, so it is returned as None."""
    kind = cursor.read("B")
    if kind <= 2:
        return (None, False, True)[kind]
    if kind in NUMBER_LAYOUTS:
        return cursor.read(NUMBER_LAYOUTS[kind])
    if kind in (13, 14):
        payload = cursor.read_bytes(read_varint(cursor))
        return payload.decode("utf-8", "replace") if kind == 13 else payload
    if kind == NUMBERS:
        layout = NUMBER_LAYOUTS[cursor.read("B")]
        return [cursor.read(layout) for _ in range(read_varint(cursor))]
    if kind not in (OBJECT, ARRAY, PACKED):
        raise ValueError("unknown value type %d at byte %d" % (kind, cursor.offset - 1))
    body = cursor.read("I")
    cursor.read("I")
    end = cursor.offset + body
    if kind == PACKED:
        cursor.offset = end
        return None
    members = {} if kind == OBJECT else []
    while cursor.offset < end:
        if kind == OBJECT:
            key = read_varint(cursor)
            members[names[key]] = read_value(cursor, names)
        else:
            members.append(read_value(cursor, names))
    if cursor.offset != end:
        raise ValueError("a node ends at byte %d, its values at %d" % (end, cursor.offset))
    return members


def read_document(data: bytes) -> dict:
    """The root object of a binary document: 32 zero bytes, then the header."""
    magic, _, dictionary, key_count = struct.unpack_from("<IIII", data, DOCUMENT_PREAMBLE)
    if any(data[:DOCUMENT_PREAMBLE]) or magic != DOCUMENT_MAGIC:
        raise ValueError("not a binary document")
    cursor = reader(data)
    cursor.offset = dictionary
    names = []
    for _ in range(key_count):
        cursor.read_bytes(8)
        names.append(cursor.read_bytes(read_varint(cursor)).decode("utf-8", "replace"))
        cursor.read_bytes(1)
    cursor = reader(data[:dictionary])
    cursor.offset = DOCUMENT_PREAMBLE + 16
    return read_value(cursor, names)


def read_variant_list(variants: list, prefix: str) -> list:
    """(name, bgfx shader bytes) of one saved list of compiled_shader_variant. A list of bytes is saved
    as one blob inside its array."""
    listed = []
    for variant in variants:
        code = variant["bytes"]
        listed.append((prefix + variant["name"], code[0] if code else b""))
    return listed


def read_variants(path: Path) -> list:
    """(name, bgfx shader bytes) for every variant of a compiled .eshader or graph material file.

    A graph material's file (<material>.mat.asset.gl, compiled_graph_material_shader in
    engine/engine/meta/rendering/graph_material.hpp) holds a code hash and compile parameters,
    then the fragment variants and the vertex variants.
    """
    document = read_document(path.read_bytes())
    if ".mat.asset." in path.name:
        shader = document["graph_material_shader"]
        return read_variant_list(shader["variants"], "") + read_variant_list(shader["vertex_variants"], "vertex ")
    return read_variant_list(document["variants"], "")


def read_glsl(shader: bytes) -> tuple:
    """(stage, GLSL text) of a bgfx shader binary."""
    stage = STAGES.get(shader[:3])
    if stage is None:
        raise ValueError("not a bgfx shader binary")
    version = shader[3]
    if version < MIN_SHADER_VERSION:
        raise ValueError("shader binary version %d, expected %d or newer" % (version, MIN_SHADER_VERSION))
    cursor = reader(shader)
    cursor.offset = 4 + 4 + 4 + 4 + 4
    for _ in range(cursor.read("H")):
        cursor.read_bytes(cursor.read("B"))
        cursor.read("B")
        cursor.read("H" if version >= UNIFORM_NUM_WIDENED_VERSION else "B")
        cursor.read_bytes(2 + 2 + 2 + 2)
    code = cursor.read_bytes(cursor.read("I"))
    return stage, code.decode("utf-8", "replace")


def prepare_for_validator(glsl: str) -> str:
    """Adds what bgfx's OpenGL backend adds at load time: the version line, when missing."""
    if glsl.startswith("#version"):
        return glsl
    return DESKTOP_GLSL_VERSION + "\n" + glsl


def find_names_drivers_reject(glsl: str) -> list:
    """Reserved words glslang accepts as names but NVIDIA's OpenGL driver rejects."""
    code = LAYOUT_QUALIFIERS.sub("", glsl)
    return ["'%s' used as a name (NVIDIA's driver rejects it)" % word
            for word in DRIVER_REJECTED_NAMES if re.search(r"\b%s\b" % word, code)]


def find_validator(explicit: str) -> str:
    """The glslangValidator to run, or an empty string."""
    candidates = [explicit] if explicit else []
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates += [os.path.join(sdk, "Bin", "glslangValidator.exe"), os.path.join(sdk, "bin", "glslangValidator")]
    candidates.append(shutil.which("glslangValidator") or "")
    return next((candidate for candidate in candidates if candidate and os.path.isfile(candidate)), "")


def validate(validator: str, stage: str, glsl: str, scratch: Path, label: str) -> list:
    """The validator's error lines for one shader; empty when it compiles."""
    source = scratch / ("%s.%s" % (label.replace("/", "_").replace(":", "_"), stage))
    source.write_text(prepare_for_validator(glsl), encoding="utf-8", newline="\n")
    result = subprocess.run([validator, "-S", stage, str(source)], capture_output=True, text=True)
    if result.returncode == 0:
        return []
    lines = (result.stdout + result.stderr).splitlines()
    return [line for line in lines if "ERROR" in line and "compilation errors" not in line] or lines[-1:]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", nargs="?", type=Path, default=DEFAULT_FOLDER)
    parser.add_argument("--filter", default="", help="only files whose path contains this text")
    parser.add_argument("--show", type=int, default=4, help="error lines shown per failing variant")
    parser.add_argument("--validator", default="", help="path of glslangValidator")
    arguments = parser.parse_args()
    validator = find_validator(arguments.validator)
    if not validator:
        print("glslangValidator not found: pass --validator, set VULKAN_SDK or add it to PATH")
        return 2
    files = [path for path in sorted(arguments.folder.rglob("*.gl")) if arguments.filter in path.as_posix()]
    failing, total = 0, 0
    with tempfile.TemporaryDirectory() as scratch:
        for path in files:
            relative = path.relative_to(arguments.folder).as_posix()
            try:
                variants = read_variants(path)
            except (ValueError, KeyError, TypeError, IndexError, struct.error) as error:
                print("UNREADABLE %s: %s" % (relative, error))
                failing += 1
                continue
            for name, shader in variants:
                if not shader:
                    continue
                label = "%s:%s" % (relative, name)
                try:
                    stage, glsl = read_glsl(shader)
                except (ValueError, struct.error) as error:
                    print("UNREADABLE %s: %s" % (label, error))
                    failing += 1
                    continue
                total += 1
                errors = find_names_drivers_reject(glsl) + validate(validator, stage, glsl, Path(scratch), label)
                if errors:
                    failing += 1
                    print("FAIL %s (%d errors)" % (label, len(errors)))
                    for line in errors[:arguments.show]:
                        print("    " + line[:170])
    print("%d failing of %d variants in %d files" % (failing, total, len(files)))
    return 1 if failing else 0


if __name__ == "__main__":
    sys.exit(main())
