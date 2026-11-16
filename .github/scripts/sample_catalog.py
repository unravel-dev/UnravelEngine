#!/usr/bin/env python3
"""Writes samples.json: the sample projects the editor's start page offers.

Every public repository of the account whose name starts with UnravelEngine-Sample- and that has a sample.json
at its root is a sample. Its sample.json describes its card:

    {
      "schema": 1,
      "title": "2048",
      "topic": "Casual game",
      "description": "The sliding tile puzzle on a glowing 3D board: ...",
      "tags": ["Puzzle", "Mobile", "C# Scripts"],
      "icon": "numeric-2-box",
      "order": 20,
      "added": "2026-11-03",
      "thumbnail": "thumbnail.jpg",
      "min_engine": "1.2.0-450",
      "version": 2
    }

tags (at most 3), icon (a Material Design icon name), order (lower first, 1000 when not given), added (the day it
was published; the start page marks it NEW for 45 days), thumbnail (a JPEG or PNG of 16:9, 800 x 450, at most
1 MiB; thumbnail.jpg when not given), min_engine (the oldest editor it opens in) and version are optional. The
picture is also the project's own thumbnail in the editor once the sample is cloned.

version is a whole number, 1 when not given. Raise it when a new version of the sample is published: every copy
keeps the sample.json it was made with, so the editor marks a copy of a lower version "Update available".

samples.json lists each sample with its branch, the project_guid of its project.cfg and the last commit that
changed its picture, which is where the editor fetches the picture from. The editor checks every field as this
script does, so a sample this script refuses fails the run instead of vanishing from the start page.

    python3 sample_catalog.py --output samples.json
        Reads the samples from GitHub. GH_TOKEN or GITHUB_TOKEN, when set, raises the API's request limit.
    python3 sample_catalog.py --local-root ../ --output editor_data/samples/samples.json --thumbnails-dir editor_data/samples
        Reads the samples from clones in a folder; their sample.json and picture must be committed and pushed to
        the branch origin names as its default (as of each clone's last fetch), which the ZIP buttons take. With
        --thumbnails-dir, the pictures are copied there as <repository>.<extension>, the copies the editor ships.
"""

import argparse
import datetime
import email.utils
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

OWNER = "unravel-dev"
REPOSITORY_PREFIX = "UnravelEngine-Sample-"
API_URL = "https://api.github.com"
RAW_URL = "https://raw.githubusercontent.com"
USER_AGENT = "UnravelEngine-sample-catalog"
HTTP_ATTEMPTS = 3
HTTP_RETRY_SECONDS = 5
SCHEMA = 1
DEFAULT_THUMBNAIL = "thumbnail.jpg"
DEFAULT_ORDER = 1000
MAX_ORDER = 100000
FIRST_VERSION = 1
MAX_VERSION = 1000000
MAX_SAMPLES = 64
MAX_TAGS = 3
MAX_TITLE_SIZE = 64
MAX_TOPIC_SIZE = 48
MAX_DESCRIPTION_SIZE = 600
MAX_TAG_SIZE = 24
MAX_REPOSITORY_NAME_SIZE = 100
MAX_BRANCH_NAME_SIZE = 100
MAX_THUMBNAIL_PATH_SIZE = 128
MAX_ICON_NAME_SIZE = 48
MAX_THUMBNAIL_SIZE = 1024 * 1024
THUMBNAIL_EXTENSIONS = (".jpg", ".jpeg", ".png")
JPEG_SIGNATURE = b"\xff\xd8\xff"
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

NAME_PART = re.compile(r"^[A-Za-z0-9._-]+$")
REPOSITORY_NAME = re.compile(r"^" + re.escape(REPOSITORY_PREFIX) + r"[A-Za-z0-9._-]+$")
COMMIT_ID = re.compile(r"^[0-9a-f]{40}$")
PROJECT_GUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
ICON_NAME = re.compile(r"^[a-z0-9-]+$")
# A version as the editor writes it back: no v, no commit, no leading zeros, and no count of 0.
MIN_ENGINE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[1-9][0-9]*)?$")


class catalog_error(Exception):
    """A sample that cannot be listed; the run fails with every such error printed."""


# ---------------------------------------------------------------------------------------------------------------
# Checks, the same as the editor's (editor/core/sample_catalog/sample_manifest.cpp).
# ---------------------------------------------------------------------------------------------------------------

def is_relative_name_path(text):
    return all(part not in ("", ".", "..") and NAME_PART.match(part) for part in text.split("/"))


def is_branch_name(text):
    return 0 < len(text) <= MAX_BRANCH_NAME_SIZE and not text.startswith("-") and is_relative_name_path(text)


def is_thumbnail_path(text):
    return (0 < len(text) <= MAX_THUMBNAIL_PATH_SIZE and text.endswith(THUMBNAIL_EXTENSIONS)
            and is_relative_name_path(text))


def is_display_text(text, max_size):
    if not isinstance(text, str) or not text or text != text.strip(" "):
        return False
    if len(text.encode("utf-8")) > max_size:
        return False
    return not any(ord(c) < 0x20 or ord(c) == 0x7F for c in text)


def is_date(text):
    if not isinstance(text, str) or not re.match(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$", text):
        return False
    try:
        datetime.date.fromisoformat(text)
    except ValueError:
        return False
    return True


def is_picture_data(data):
    return data.startswith(JPEG_SIGNATURE) or data.startswith(PNG_SIGNATURE)


def read_card(repository, text):
    """The card fields of a sample.json, checked; raises catalog_error for anything the editor would refuse."""
    try:
        document = json.loads(text)
    except ValueError as e:
        raise catalog_error(f"{repository}: sample.json is not JSON: {e}.")
    if not isinstance(document, dict):
        raise catalog_error(f"{repository}: sample.json is not an object.")
    if document.get("schema") != SCHEMA:
        raise catalog_error(f"{repository}: sample.json has no \"schema\": {SCHEMA}.")
    problems = []
    card = {}
    for name, max_size in (("title", MAX_TITLE_SIZE), ("topic", MAX_TOPIC_SIZE), ("description", MAX_DESCRIPTION_SIZE)):
        value = document.get(name)
        if not is_display_text(value, max_size):
            problems.append(f"\"{name}\" must be text of 1 to {max_size} bytes, without control characters or "
                            "spaces at either end")
        card[name] = value
    tags = document.get("tags", [])
    if not isinstance(tags, list) or len(tags) > MAX_TAGS or not all(is_display_text(t, MAX_TAG_SIZE) for t in tags):
        problems.append(f"\"tags\" must be a list of at most {MAX_TAGS} texts of 1 to {MAX_TAG_SIZE} bytes")
    card["tags"] = tags
    icon = document.get("icon")
    if icon is not None:
        if not isinstance(icon, str) or not ICON_NAME.match(icon) or len(icon) > MAX_ICON_NAME_SIZE:
            problems.append("\"icon\" must be a Material Design icon name: lower-case letters, digits and dashes")
        card["icon"] = icon
    added = document.get("added")
    if added is not None:
        if not is_date(added):
            problems.append("\"added\" must be a date, YYYY-MM-DD")
        card["added"] = added
    min_engine = document.get("min_engine")
    if min_engine is not None:
        if not isinstance(min_engine, str) or not MIN_ENGINE.match(min_engine):
            problems.append("\"min_engine\" must be a version as 1.2.0 or 1.2.0-450")
        card["min_engine"] = min_engine
    version = document.get("version", FIRST_VERSION)
    if not isinstance(version, int) or isinstance(version, bool) or not FIRST_VERSION <= version <= MAX_VERSION:
        problems.append(f"\"version\" must be a whole number from {FIRST_VERSION} to {MAX_VERSION}")
    card["version"] = version
    order = document.get("order", DEFAULT_ORDER)
    if not isinstance(order, int) or isinstance(order, bool) or not 0 <= order <= MAX_ORDER:
        problems.append(f"\"order\" must be a whole number from 0 to {MAX_ORDER}")
    thumbnail = document.get("thumbnail", DEFAULT_THUMBNAIL)
    if not isinstance(thumbnail, str) or not is_thumbnail_path(thumbnail):
        problems.append("\"thumbnail\" must be the relative path of a .jpg, .jpeg or .png file, of letters, digits, "
                        "dots, dashes, underscores and slashes")
    if problems:
        raise catalog_error(f"{repository}: sample.json is not valid.\n" + "\n".join(f"  {p}." for p in problems))
    return card, order, thumbnail


def read_project_guid(text):
    """The project_guid of a project.cfg, or None."""
    try:
        guid = json.loads(text)["project"]["project_guid"]
    except (ValueError, KeyError, TypeError):
        return None
    return guid if isinstance(guid, str) and PROJECT_GUID.match(guid) else None


def make_entry(repository, branch, card, thumbnail_path, thumbnail_commit, project_guid):
    if not REPOSITORY_NAME.match(repository) or len(repository) > MAX_REPOSITORY_NAME_SIZE:
        raise catalog_error(f"{repository}: the repository name is not one the editor accepts.")
    if not is_branch_name(branch):
        raise catalog_error(f"{repository}: the branch name {branch} is not one the editor accepts.")
    if not COMMIT_ID.match(thumbnail_commit):
        raise catalog_error(f"{repository}: {thumbnail_commit} is no commit id.")
    entry = {"repository": repository, "branch": branch}
    entry.update(card)
    if project_guid:
        entry["project_guid"] = project_guid
    entry["thumbnail"] = {"path": thumbnail_path, "commit": thumbnail_commit}
    return entry


def check_picture(repository, path, data):
    if len(data) > MAX_THUMBNAIL_SIZE:
        raise catalog_error(f"{repository}: {path} is larger than {MAX_THUMBNAIL_SIZE} bytes.")
    if not is_picture_data(data):
        raise catalog_error(f"{repository}: {path} is no JPEG or PNG file.")


# ---------------------------------------------------------------------------------------------------------------
# Samples on GitHub.
# ---------------------------------------------------------------------------------------------------------------

def http_request(url, is_api, method="GET"):
    """The body and headers of a request, or (None, None) for a 404.

    GitHub answers 5xx or 429 at times, and a connection can drop: such a request is tried again, HTTP_ATTEMPTS
    times in all, before the run fails with a catalog_error.
    """
    headers = {"User-Agent": USER_AGENT}
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if is_api:
        headers["Accept"] = "application/vnd.github+json"
        if token:
            headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers, method=method)
    failure = ""
    for attempt in range(HTTP_ATTEMPTS):
        if attempt > 0:
            time.sleep(HTTP_RETRY_SECONDS * attempt)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read(), response.headers
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None, None
            failure = f"{url} answered {e.code}: {e.read()[:300].decode('utf-8', 'replace')}"
            if e.code < 500 and e.code != 429:
                break
        except (urllib.error.URLError, OSError) as e:
            failure = f"{url} could not be reached: {e}"
    raise catalog_error(failure)


def http_get(url, is_api):
    """The body and headers of a GET, or (None, None) for a 404."""
    return http_request(url, is_api)


def get_github_time():
    """GitHub's clock, from the Date of an answer of github.com: samples.json is dated by it, not by the clock of
    the computer the script runs on, as editors list the file written later."""
    _, headers = http_request("https://github.com", False, method="HEAD")
    if headers is None or not headers.get("Date"):
        raise catalog_error("github.com gave no Date to date samples.json by.")
    return email.utils.parsedate_to_datetime(headers["Date"]).astimezone(datetime.timezone.utc)


def get_api_json(path):
    body, _ = http_get(f"{API_URL}/{path}", True)
    if body is None:
        raise catalog_error(f"{API_URL}/{path} does not exist.")
    return json.loads(body)


def list_github_repositories():
    """The public sample repositories of the account, with their default branches."""
    repositories = []
    url = f"{API_URL}/users/{OWNER}/repos?per_page=100&type=owner"
    while url:
        body, headers = http_get(url, True)
        if body is None:
            raise catalog_error(f"{url} does not exist.")
        for repository in json.loads(body):
            name = repository["name"]
            if (name.startswith(REPOSITORY_PREFIX) and not repository.get("private") and not repository.get("fork")
                    and not repository.get("archived")):
                repositories.append((name, repository["default_branch"]))
        match = re.search(r'<([^>]+)>;\s*rel="next"', headers.get("Link", "") or "")
        url = match.group(1) if match else None
    return sorted(repositories)


def read_github_sample(repository, branch, skipped):
    head = get_api_json(f"repos/{OWNER}/{repository}/commits/{branch}")["sha"]
    sample_text, _ = http_get(f"{RAW_URL}/{OWNER}/{repository}/{head}/sample.json", False)
    if sample_text is None:
        skipped.append(f"{repository} has no sample.json, so it is not listed.")
        return None
    card, order, thumbnail_path = read_card(repository, sample_text)
    commits = get_api_json(f"repos/{OWNER}/{repository}/commits?sha={head}&path={thumbnail_path}&per_page=1")
    if not commits:
        raise catalog_error(f"{repository}: {thumbnail_path} is not in the repository.")
    thumbnail_commit = commits[0]["sha"]
    picture, _ = http_get(f"{RAW_URL}/{OWNER}/{repository}/{thumbnail_commit}/{thumbnail_path}", False)
    if picture is None:
        raise catalog_error(f"{repository}: {thumbnail_path} could not be read at {thumbnail_commit}.")
    check_picture(repository, thumbnail_path, picture)
    project_text, _ = http_get(f"{RAW_URL}/{OWNER}/{repository}/{head}/project.cfg", False)
    project_guid = read_project_guid(project_text) if project_text else None
    entry = make_entry(repository, branch, card, thumbnail_path, thumbnail_commit, project_guid)
    return order, entry, picture


# ---------------------------------------------------------------------------------------------------------------
# Samples in local clones.
# ---------------------------------------------------------------------------------------------------------------

def run_git(clone, *arguments):
    result = subprocess.run(["git", "-C", str(clone), *arguments], capture_output=True, text=True)
    if result.returncode != 0:
        raise catalog_error(f"{clone.name}: git {' '.join(arguments)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def get_default_branch(clone):
    """The branch origin serves by default: as the clone recorded it (refs/remotes/origin/HEAD), else as origin
    answers, for a clone that never recorded it."""
    recorded = subprocess.run(["git", "-C", str(clone), "symbolic-ref", "--short", "refs/remotes/origin/HEAD"],
                              capture_output=True, text=True)
    if recorded.returncode == 0 and "/" in recorded.stdout:
        return recorded.stdout.strip().split("/", 1)[1]
    answer = run_git(clone, "ls-remote", "--symref", "origin", "HEAD")
    match = re.search(r"^ref: refs/heads/(\S+)\s+HEAD$", answer, re.MULTILINE)
    if not match:
        raise catalog_error(f"{clone.name}: origin names no default branch.")
    return match.group(1)


def check_pushed(clone, branch, paths):
    """Fails unless `paths` are at HEAD as they are on origin's `branch` (as of the clone's last fetch): the editor
    fetches the picture from GitHub at the commit the file names."""
    upstream = f"origin/{branch}"
    if run_git(clone, "log", "--format=%h", f"{upstream}..HEAD", "--", *paths):
        raise catalog_error(f"{clone.name}: push the commits that change {' and '.join(paths)} to {upstream} first.")
    if run_git(clone, "log", "--format=%h", f"HEAD..{upstream}", "--", *paths):
        raise catalog_error(f"{clone.name}: {upstream} has newer {' and '.join(paths)}. Pull them first.")


def read_local_sample(clone, skipped):
    repository = clone.name
    sample_path = clone / "sample.json"
    if not sample_path.is_file():
        skipped.append(f"{repository} has no sample.json, so it is not listed.")
        return None
    card, order, thumbnail_path = read_card(repository, sample_path.read_bytes())
    changed = run_git(clone, "status", "--porcelain", "--", "sample.json", thumbnail_path)
    if changed:
        raise catalog_error(f"{repository}: commit sample.json and {thumbnail_path} first. The editor fetches the "
                            "picture at its commit.")
    thumbnail_commit = run_git(clone, "log", "-1", "--format=%H", "--", thumbnail_path)
    if not thumbnail_commit:
        raise catalog_error(f"{repository}: {thumbnail_path} is not committed.")
    branch = get_default_branch(clone)
    check_pushed(clone, branch, ["sample.json", thumbnail_path])
    picture = (clone / thumbnail_path).read_bytes()
    check_picture(repository, thumbnail_path, picture)
    project_path = clone / "project.cfg"
    project_guid = read_project_guid(project_path.read_bytes()) if project_path.is_file() else None
    entry = make_entry(repository, branch, card, thumbnail_path, thumbnail_commit, project_guid)
    return order, entry, picture


# ---------------------------------------------------------------------------------------------------------------
# Output.
# ---------------------------------------------------------------------------------------------------------------

def write_shipped_pictures(directory, samples):
    """Copies each picture to <repository>.<extension> and removes the copies of samples no longer listed."""
    directory.mkdir(parents=True, exist_ok=True)
    names = set()
    for entry, picture in samples:
        name = entry["repository"] + Path(entry["thumbnail"]["path"]).suffix
        (directory / name).write_bytes(picture)
        names.add(name)
    for path in directory.iterdir():
        if (path.is_file() and path.name.startswith(REPOSITORY_PREFIX) and path.suffix in THUMBNAIL_EXTENSIONS
                and path.name not in names):
            path.unlink()


def write_summary(lines):
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", required=True, type=Path, help="Where to write samples.json.")
    parser.add_argument("--local-root", type=Path, help="Read the samples from clones in this folder.")
    parser.add_argument("--thumbnails-dir", type=Path, help="Copy the pictures here as <repository>.<extension>.")
    options = parser.parse_args()

    skipped = []
    errors = []
    found = []
    if options.local_root:
        sources = [(clone.name, lambda clone=clone: read_local_sample(clone, skipped))
                   for clone in sorted(options.local_root.iterdir())
                   if clone.is_dir() and REPOSITORY_NAME.match(clone.name) and (clone / ".git").exists()]
    else:
        sources = [(name, lambda name=name, branch=branch: read_github_sample(name, branch, skipped))
                   for name, branch in list_github_repositories()]
    for name, read in sources:
        try:
            sample = read()
        except catalog_error as e:
            errors.append(str(e))
            continue
        if sample:
            found.append(sample)
    for line in skipped:
        print(line)
    if errors:
        for line in errors:
            print(f"error: {line}", file=sys.stderr)
        write_summary(["Sample catalog: not written."] + [f"- {line}" for line in errors])
        return 1
    if not found:
        print("error: no sample was found.", file=sys.stderr)
        return 1
    if len(found) > MAX_SAMPLES:
        print(f"error: {len(found)} samples; the editor reads at most {MAX_SAMPLES}.", file=sys.stderr)
        return 1
    found.sort(key=lambda sample: (sample[0], sample[1]["title"]))
    generated = get_github_time().strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest = {"schema": SCHEMA, "generated": generated, "samples": [entry for _, entry, _ in found]}
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_bytes((json.dumps(manifest, indent=2, ensure_ascii=True) + "\n").encode("ascii"))
    if options.thumbnails_dir:
        write_shipped_pictures(options.thumbnails_dir, [(entry, picture) for _, entry, picture in found])
    lines = [f"samples.json of {generated}: {len(found)} samples."]
    lines += [f"- {entry['repository']}: {entry['title']}, picture {entry['thumbnail']['path']} at "
              f"{entry['thumbnail']['commit'][:12]}" for _, entry, _ in found]
    lines += [f"- {line}" for line in skipped]
    print("\n".join(lines))
    write_summary(lines)
    return 0


if __name__ == "__main__":
    sys.exit(main())
