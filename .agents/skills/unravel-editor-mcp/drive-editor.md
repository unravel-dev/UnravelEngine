# Driving an editor of your own

Start an editor on your own port, call its tools, capture the editor window, stop it. The
script `scripts/editor_ctl.py` (Python 3, standard library) does start, wait, call and stop;
the raw commands below it are for what the script does not cover. `<repo>`, `<project>` and the
port 27391 are placeholders - pick a port no other session uses.

Contents: [Start](#start), [Call tools](#call-tools),
[Capture the editor window](#capture-the-editor-window), [Stop](#stop).

## Start

1. Pick the configured tree whose `bin` has `data/` and `clrpp/` (skill
   `unravel-build-verify`), usually `build/RelWithDebInfo/bin` for speed and `build/Debug/bin`
   for debugging. Check that no editor runs from that folder:

   ```powershell
   Get-CimInstance Win32_Process -Filter "Name='unravel-editor.exe'" | Select-Object ProcessId, ExecutablePath, CommandLine
   ```

2. Start it and wait until it answers:

   ```bash
   python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py start build/RelWithDebInfo/bin <project> --port 27391
   python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py wait --port 27391
   ```

   `start` refuses a port that answers and a port whose recorded editor still runs, keeps
   vsync on unless `--novsync` (GPU timing only), and passes `--renderer` and `--profile`
   (`--mcp-profile developer|user`). The pid file and the editor's console output are in
   `<temp>/unravel-editor-ctl/`. The project must exist: started with a path that does not,
   the editor never answers on its port.
3. Call `editor_get_status`: the project path, the scene, the MCP profile and the graphics
   device must be the ones you meant. A `modal` in it is a dialog the editor waits on
   (`editor_answer_modal`).

Without the script: `Start-Process -FilePath "$bin/unravel-editor.exe" -WorkingDirectory $bin
-ArgumentList '-p', '"<project>"', '--mcp-port', '27391' -PassThru`, keep the pid, and poll
`http://127.0.0.1:27391/health` until it answers `{"ok":true,"service":"unravel-editor-mcp"}`.

## Call tools

```bash
python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py call editor_get_status --port 27391
python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py call skills_get '{"names":["getting-started","verification"]}' --port 27391
python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py call viewport_capture_scene '{"scale":0.5}' --image scene.png --port 27391
```

`call` prints the result's text blocks, writes a capture's PNG to `--image`, and exits 1 when
the tool fails or is refused. `--timeout` (300 s) covers tools that wait (`editor_wait_for` up
to 120 s, imports 15 s by default).

The protocol underneath: one POST per call to `/mcp`, no session or `initialize` needed.

```bash
curl -s http://127.0.0.1:27391/mcp -H "Content-Type: application/json" -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"editor_get_status","arguments":{}}}'
```

`"method":"tools/list"` lists the tools of the running profile. The answer is
`{"result":{"content":[...],"isError":false}}`: the first block's `text` is the tool's result
(JSON for most tools), a capture adds a block `{"type":"image","data":<base64 PNG>}`. A refused
call (unknown tool, a developer tool on the user profile) answers `{"error":{...}}` instead.

## Capture the editor window

MCP captures show only the Scene and Game views. For panels, inspectors and popups:

1. Make the capturing thread per-monitor DPI aware first
   (`SetThreadDpiAwarenessContext` with -4): an unaware process works in scaled coordinates on
   a scaled monitor and crops the window.
2. Take the window from the pid you started (`(Get-Process -Id <pid>).MainWindowHandle`). With
   ImGui viewports a dialog can be a window of its own, so check the size of what you got.
3. A minimized window (`IsIconic`) is restored with `ShowWindow(hwnd, 4)` (SW_SHOWNOACTIVATE),
   which leaves the user's focus alone; capture a few frames later.
4. Capture with `PrintWindow(hwnd, hdc, 2)` (PW_RENDERFULLCONTENT) into a bitmap of the window
   rect (`GetWindowRect`), then crop to the client area at `ClientToScreen(0,0)` minus the window
   rect's corner: PrintWindow draws from the window's corner, title bar and borders included, so a
   client-sized bitmap is shifted and its points miss what `editor_ui_click` hits. A screen copy
   (`CopyFromScreen`) grabs whatever window is on top.
5. A popup opened near the window's edge becomes an OS window of its own that the main
   window's capture does not show: open it away from the edge.
6. Click and drag in the editor UI with `editor_ui_click`, which moves the editor's virtual
   pointer. Posted window messages do not reach the editor's ImGui, and moving the real
   cursor moves the user's mouse.

## Stop

```bash
python .agents/skills/unravel-editor-mcp/scripts/editor_ctl.py stop --port 27391
```

`stop` ends only the pid `start` recorded for that port, with what it started (shader
compiles, script builds): `taskkill /PID <pid> /T /F` on Windows. List the editor processes
again to confirm none of yours is left, then read the errors of the run:

```bash
grep -E "\[(error|critical)\]" <repo>/build/RelWithDebInfo/bin/Log.txt
```
