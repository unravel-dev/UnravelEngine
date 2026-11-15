# Async script code

How `await` works in scripts and what an engine change around it must keep.

| Purpose | Path |
|---------|------|
| Contexts and the pump (`ScriptSynchronizationContext`, `WorkerSynchronizationContext`, `AwaitableScheduler`) | `engine_data/data/scripts/system/ScriptSynchronizationContext.cs` |
| Waits (`Awaitable`) | `engine_data/data/scripts/system/Awaitable.cs` |
| Destroy token, `RunningScope` | `engine_data/data/scripts/scene/components/ScriptComponent.cs` |
| Pump calls from the frame hooks | `engine_data/data/scripts/system/System.cs` (`SystemManager`) |

## Model

- Each script has a synchronization context per play session, and the session has one for code no
  script runs. `RunningScope` installs the script's context around every call into a script, so an
  `await` resumes as that script.
- The pump runs after the coroutines (`AwaitableScheduler.Tick`); fixed-update waits resume after
  FixedUpdate (`TickFixedUpdate`), end-of-frame waits after LateUpdate (`TickEndOfFrame`). Nothing pumps
  while paused or outside play.
- `MainThreadAsync` after `BackgroundThreadAsync` resumes as the same script: the worker runs under a
  `WorkerSynchronizationContext` that carries the owner across worker awaits.

## Ending a script or a session

- One rule for both: when a script ends (destroy, before OnDestroy) or the session ends (play stop,
  `internal_n2m_end_play`, while the scene is still whole; OnDestroy does not run at stop), its token
  (`destroyCancellationToken`, or `Application.exitCancellationToken`) is cancelled and its pending
  waits end cancelled, on the script thread as that script. What the cancellation posts runs right after.
- Everything else of that script is dropped: work queued before, and tasks that finish later (a running
  `Task.Run` body, IO), `finally` blocks included. `task.WaitAsync(destroyCancellationToken)` ends such a
  wait at once.
- Script tokens hang on the session token, so play stop cancels them all.

## Rules for changes

- Pump lists only grow while they are pumped and shed ended waits after the outermost pump, so a
  continuation may destroy scripts, add waits or run a pump again. Ending a script touches only its own
  waits.
- Awaitables are not pooled: a kept one can be cancelled or awaited again, token callbacks arrive from
  other threads after the wait ended, and a second await is chained with an error instead of thrown.
  Pooling would resume the wrong code.
- Never block the script thread on async code (`.Result`, `.Wait()`, `GetAwaiter().GetResult()`): the
  continuation needs the thread that waits. On CoreCLR `ScriptSynchronizationContext.Wait` logs a wait
  longer than 3 s while async code is pending, with its stack.
- Exceptions from async code are logged with the script's type; cancellations by a token are not. An
  `async void` exception posted to a closed context is still logged.
- Code on a worker thread calls only pure C#: engine bindings registered with `script_internal_call`
  throw there.

## Verify

In an editor, with a script that awaits `WaitForSecondsAsync`, `BackgroundThreadAsync` /
`MainThreadAsync` and a `Task.Run`: the continuations run as the script, pausing holds them, destroying
the entity cancels them, and leaving play mid-wait logs no exception.
