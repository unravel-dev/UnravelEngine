"""Lists the OpenGL binding traps that only show at run time, read from the shader sources.

- An image named like a sampler uniform: bgfx's OpenGL backend looks every active uniform of a
  program up by name and maps images to the sampler type, so an image named like a sampler that
  any program registers is rebound to that sampler's last stage and its writes land in another
  texture, on OpenGL only. Images take i_ names.
- An image bound at stage 8 or above: OpenGL has image units 0-7 and such a binding fails only
  at run time.

Usage (from the repository root):
    python .agents/skills/unravel-rendering/scripts/scan_gl_image_names.py [shader folder ...]
Without folders it scans engine_data/data/shaders and editor_data/data/shaders.
Exit code 1 when anything is found.
"""

import argparse
import re
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[4]
DEFAULT_FOLDERS = [REPOSITORY / "engine_data" / "data" / "shaders", REPOSITORY / "editor_data" / "data" / "shaders"]
SHADER_EXTENSIONS = (".eshader", ".sh", ".sc")
# bgfx's own headers define the macros; their parameter names are not uniforms.
SKIPPED_FILES = {"bgfx_shader.sh", "bgfx_compute.sh"}
MAX_IMAGE_UNIT = 7
IMAGE = re.compile(r"\b(?:U?IMAGE(?:2D|3D|2D_ARRAY))_(?:RO|WO|RW)\s*\(\s*(\w+)\s*,\s*\w+\s*,\s*(\d+)")
SAMPLER = re.compile(r"\b[IU]?SAMPLER\w*\s*\(\s*(\w+)")


def strip_comments(text: str) -> str:
    """Removes block and line comments so commented-out bindings are not counted."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def find_shader_files(folders: list) -> list:
    """Every shader source and header under the folders."""
    files = []
    for folder in folders:
        for path in sorted(folder.rglob("*")):
            if path.suffix in SHADER_EXTENSIONS and path.name not in SKIPPED_FILES and path.is_file():
                files.append(path)
    return files


def collect_bindings(files: list) -> tuple:
    """Maps image and sampler names to the files declaring them; lists images above the last unit."""
    images, samplers, high_units = {}, {}, []
    for path in files:
        relative = path.relative_to(REPOSITORY).as_posix() if REPOSITORY in path.parents else str(path)
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for match in IMAGE.finditer(text):
            images.setdefault(match.group(1), set()).add(relative)
            if int(match.group(2)) > MAX_IMAGE_UNIT:
                high_units.append("%s: %s at stage %s" % (relative, match.group(1), match.group(2)))
        for match in SAMPLER.finditer(text):
            samplers.setdefault(match.group(1), set()).add(relative)
    return images, samplers, high_units


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folders", nargs="*", type=Path, help="shader folders to scan")
    arguments = parser.parse_args()
    folders = [folder.resolve() for folder in arguments.folders] or DEFAULT_FOLDERS
    images, samplers, high_units = collect_bindings(find_shader_files(folders))
    collisions = sorted(set(images) & set(samplers))
    for name in collisions:
        print("image named like a sampler: %s" % name)
        print("    image in  : %s" % ", ".join(sorted(images[name])))
        print("    sampler in: %s" % ", ".join(sorted(samplers[name])))
    for line in high_units:
        print("image above unit %d: %s" % (MAX_IMAGE_UNIT, line))
    print("%d collisions, %d images above unit %d (%d image names, %d sampler names)"
          % (len(collisions), len(high_units), MAX_IMAGE_UNIT, len(images), len(samplers)))
    return 1 if collisions or high_units else 0


if __name__ == "__main__":
    sys.exit(main())
