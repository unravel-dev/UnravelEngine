# Content packages (.unravelpkg)

Content shared between projects. An editor feature only: the game never sees a package.

| Purpose | Path |
|---------|------|
| Manifest, versions, archive, export, import plan, install, registry (library `packages`) | `editor/core/packages/` |
| Editor glue: project view, trash hooks, `.packages` folder | `editor/editor/assets/package_actions.h` |
| Packages window: browse, import review, export | `editor/editor/hub/panels/packages_panel/` |
| Suite | `tests/tests/suites/content_packages.cpp` (`--suite "content packages"`) |

## Contracts

- A package is an engine pack (`fs::write_pack`, read with `fs::pack_reader`,
  `engine/core/filesystem/pack.h`) holding `package.json`, `content/<path>` and `content/<path>.meta`.
  Files keep their data-folder paths and their uids; compiled output never ships.
- Export walks references with `deployment::find_content_references` (the deploy's walk), adds the
  scripts that declare the script types scenes and prefabs use, builds the package's scripts alone
  (`asset_compiler::check_script_files`) and adds the project scripts that declare the types the build
  misses (`add_scripts_declaring`). `add_scripts_declaring` sorts `plan.entries`: never take the entries
  past the old size as the ones it added; use the keys it returns.
- A folder's `package.json` keeps its name and version for the next export.
- Import is `plan_import` (pure: reads the project, writes nothing) then `install_package` (staged,
  all-or-nothing; replaced files go to the editor trash and come back when the install fails). The
  install plans again first, and writes nothing when the project changed since the review
  (`is_same_plan`).
- A uid another project file already holds gets a copy uid, `make_copy_uid(project_guid, name, uid)`
  (same inputs, same id, so an update finds the copy), and the package's text files are rewritten to it.
- The install record is `<project>/packages.cfg`, keyed by project uid, not the `.meta` files (they keep
  the package's bytes). It tells unchanged, edited and deleted files apart for updates and removal.
  When it does not read, imports and removals refuse (`project_view::registry_error`) instead of saving
  over it.
- Script types two files would define are checked from the actions picked in the review
  (`update_script_clashes` after an action changes); each script file in a manifest lists its types.
- Install and removal write under `fs::watcher::scoped_pause` and touch the written files inside it, as
  `editor_actions::import_files` does, so each file compiles once.
- The script build after an install waits until the asset manager holds every `.cs` the asset database
  lists (`script_system::is_waiting_for_sources`): each script reaches the manager after its own compile
  job, and a build in between fails on types the rest declares.
- Exports go to `<project>/.packages/`, which the project gitignore template ignores like `.deploy/`;
  the Packages window lists that folder. A `.unravelpkg` dropped on the Content Browser opens the import
  review; folders have Export as Package.
- Planning and installing run in the background; a review closes when its project closes, after a
  running install finishes (the install resolves `app:/` while it writes).
