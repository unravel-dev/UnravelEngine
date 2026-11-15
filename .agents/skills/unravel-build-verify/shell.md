# Git Bash on this tree

The Bash tool runs Git Bash (MSYS) on Windows; PowerShell is available beside it.

## Line endings

- The sources mix CRLF files and LF files, and each keeps its own. `sed -i` rewrites a CRLF
  file as LF, which turns a one-line change into a whole-file diff; `grep -c $'\r'` miscounts.
- Edit with the Edit tool, or in Python on bytes: read with `open(path, 'rb')`, detect the
  newline, write back with it. `Path.read_text()` / `write_text()` on Windows writes CRLF into
  an LF file.
- Check with Python: `data.count(b'\r\n')` against `data.count(b'\n')`, before and after. A diff
  stat far larger than the edit means the endings changed.

## Backslashes

- Text in a Bash heredoc and in a non-raw Python string is unescaped before it reaches the
  file: a C++ `"\n"` becomes a line break, `C:\Users` fails as a `\U` escape. Write patch
  scripts with the Write tool and keep old and new text in raw strings (`r'''...'''`).
- After a scripted C++ patch, `grep -n 'printf("[^"]*$' <file>` finds a split string literal
  before the build does.

## Patch scripts and git

- A patch script checks every anchor before it writes anything. Undo a partial patch from a
  copy taken before it (`cp file file.bak`), never with `git checkout --` or `git restore`.
- No destructive git command in a command chain; chains use `&&`, so a failed step stops the
  rest.

## Background jobs and processes

- Long drivers (an editor start, MCP scripts, phone runs) start with the Bash tool's
  `run_in_background`, never a trailing `&`: an `&` job outlives the call, `ps` does not show
  it, and it collides with the next run (two editors on one MCP port).
- Stopping a background `bash driver.sh` task ends only the wrapper. Find the orphan with
  PowerShell, `Get-CimInstance Win32_Process | Where-Object CommandLine -like '*driver.sh*'`,
  and stop that process id.
- `taskkill /F /IM ...` is mangled by MSYS path conversion. Stop a process by id:
  `powershell -NoProfile -Command "Stop-Process -Id <pid> -Force"`, and only a process you
  started.
- Arguments that start with `/` are converted to Windows paths too (an `adb shell` path):
  prefix the command with `MSYS_NO_PATHCONV=1`.

## Scratch tooling

- Scratch scripts write into a new, uniquely named folder per run and never delete
  (`shutil.rmtree`, `os.remove`, `rm -f`), not even their own output: a denied deletion also
  blocks the related checks that follow it.
- Keep a destructive step out of a command that also runs a check.
