# API docs

This folder holds the sources and configs for the two API reference sites:

| Site             | Built by                    | Output                 | Published at                                                                     |
|------------------|-----------------------------|------------------------|----------------------------------------------------------------------------------|
| Engine C++ API   | Doxygen (`Engine-Doxyfile`) | `docs/engine-api/html` | [engine-api/html](https://unravel-dev.github.io/UnravelEngine/engine-api/html/) |
| Scripting C# API | docfx (`docfx.json`)        | `docs/_site`           | [script-api/html](https://unravel-dev.github.io/UnravelEngine/script-api/html/) |

`engine-mainpage.md` is the landing page of the C++ site. `index.md`, `toc.yml`
and `markdown/` are the landing page and guides of the Scripting site; docfx
builds only those files and the API pages it generates into `api/`.

## Generating

`generate-api-docs.ps1` (Windows) and `generate-api-docs.sh` (Linux, macOS, Git
Bash) build both sites, or one of them:

- Doxygen with `Engine-Doxyfile` builds the Engine site into `docs/engine-api`.
- docfx with `docfx.json` builds the Scripting site into `docs/_site`.

Each build first empties its output folder, because neither tool removes pages
that no longer exist. Run the scripts from the repository root. All outputs are
gitignored.

### Prerequisites

- [Doxygen](https://www.doxygen.nl/) on `PATH` for the Engine site.
- docfx on `PATH` for the Scripting site: `dotnet tool install -g docfx`.

### Options

| PowerShell    | bash            | Effect                                  |
|---------------|-----------------|-----------------------------------------|
| (none)        | (none)          | Build both sites.                       |
| `-EngineOnly` | `--engine-only` | Build only the Engine site (Doxygen).   |
| `-ScriptOnly` | `--script-only` | Build only the Scripting site (docfx).  |

### Examples

Both sites:

```bash
bash docs/generate-api-docs.sh
```

The same on Windows PowerShell. `-ExecutionPolicy Bypass` lets this one run
start the unsigned script without changing the machine's policy:

```powershell
powershell -ExecutionPolicy Bypass -File docs/generate-api-docs.ps1
```

Only the Engine C++ site:

```bash
bash docs/generate-api-docs.sh --engine-only
```

Only the Scripting C# site:

```bash
bash docs/generate-api-docs.sh --script-only
```

## Publishing

The sites are served from the
[unravel-dev.github.io](https://github.com/unravel-dev/unravel-dev.github.io)
repository, not from this one. Replace the folder contents there, then commit
and push that repository:

- `docs/engine-api/html/*` -> `UnravelEngine/engine-api/html/`
- `docs/_site/*` -> `UnravelEngine/script-api/html/`

Clear each target folder before copying so pages that no longer exist are
removed.
