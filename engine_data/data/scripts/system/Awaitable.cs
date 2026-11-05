using System;
using System.Runtime.CompilerServices;
using System.Threading;

namespace Unravel.Core
{
    /// <summary>
    /// A wait that script code can <c>await</c>: the next frame, seconds of game or real time, the
    /// next fixed update, the end of the frame, or a hop to a worker thread and back. Code after the
    /// <c>await</c> runs on the script thread in the frame loop, so it can call the engine.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Every <c>await</c> in a script's code comes back to that script: after an awaited
    /// <see cref="System.Threading.Tasks.Task"/> too, in the frame after the task finished. Nothing
    /// resumes while the game is paused.
    /// </para>
    /// <para>
    /// When the script is destroyed, or play stops, its async work ends at once, as that script on the
    /// script thread: its <see cref="ScriptComponent.destroyCancellationToken"/> is cancelled and its
    /// waits end cancelled, so the code awaiting them gets an <see cref="OperationCanceledException"/>
    /// and its catch and finally blocks run once. Nothing else of it resumes: a task that finishes later
    /// (a running <c>Task.Run</c> body, a file or web read) is dropped with the code awaiting it, finally
    /// blocks included. To end such a wait at once with the script, await
    /// <c>task.WaitAsync(destroyCancellationToken)</c>; the work itself stops only if it checks the token.
    /// </para>
    /// <para>
    /// Never block the script thread on async code: <c>.Result</c>, <c>.Wait()</c> or
    /// <c>GetAwaiter().GetResult()</c> on a task whose code awaits without <c>ConfigureAwait(false)</c>,
    /// library code included, waits for the script thread to resume that code, and the script thread
    /// is the one waiting: it never returns, and the game freezes. Await the task instead. On CoreCLR,
    /// such a wait is logged as an error after three seconds, with its stack.
    /// </para>
    /// <para>
    /// Await an Awaitable once. Awaiting a pending one a second time logs an error; both awaits resume.
    /// Awaitables are not pooled, so one can be kept to <see cref="Cancel"/> it or to read
    /// <see cref="isCompleted"/>.
    /// </para>
    /// </remarks>
    /// <example><code>
    /// async void OnStart()
    /// {
    ///     await Awaitable.WaitForSecondsAsync(2.0f, destroyCancellationToken);
    ///     var path = await Task.Run(() => BuildPath(), destroyCancellationToken);
    ///     owner.transform.position = path[0]; // back on the script thread
    /// }
    /// </code></example>
    public sealed class Awaitable
    {
        private enum Kind
        {
            NextFrame,
            Seconds,
            RealtimeSeconds,
            FixedUpdate,
            EndOfFrame,
            BackgroundThread,
            MainThread,
        }

        private enum State
        {
            Pending,
            Completed,
            Canceled,
        }

        private readonly Kind kind;
        private readonly long frame;
        private readonly double until;
        private readonly ScriptSynchronizationContext context;
        private State state;
        private Action continuation;
        private CancellationToken cancelledBy;
        private CancellationTokenRegistration registration;
        private bool isListed;

        private Awaitable(Kind kind, long frame, double until)
        {
            this.kind = kind;
            this.frame = frame;
            this.until = until;
            owner = AwaitableScheduler.currentOwner;
            context = AwaitableScheduler.ContextFor(owner);
        }

        /// <summary>
        /// Resumes in the next frame, after the coroutines (like <c>yield return null</c>).
        /// </summary>
        /// <param name="cancellationToken">Ends the wait cancelled.</param>
        public static Awaitable NextFrameAsync(CancellationToken cancellationToken = default)
        {
            return StartWait(new Awaitable(Kind.NextFrame, Time.frameCount + 1, 0.0),
                             cancellationToken,
                             AwaitableScheduler.AddFrameWait);
        }

        /// <summary>
        /// Resumes once the given seconds of game time have passed (scaled by <see cref="Time.timeScale"/>).
        /// </summary>
        /// <param name="seconds">Seconds of game time to wait.</param>
        /// <param name="cancellationToken">Ends the wait cancelled.</param>
        public static Awaitable WaitForSecondsAsync(float seconds, CancellationToken cancellationToken = default)
        {
            return StartWait(new Awaitable(Kind.Seconds, 0, Time.time + seconds),
                             cancellationToken,
                             AwaitableScheduler.AddFrameWait);
        }

        /// <summary>
        /// Resumes once the given real seconds have passed, whatever the time scale.
        /// </summary>
        /// <param name="seconds">Real seconds to wait.</param>
        /// <param name="cancellationToken">Ends the wait cancelled.</param>
        public static Awaitable WaitForSecondsRealtimeAsync(float seconds, CancellationToken cancellationToken = default)
        {
            return StartWait(new Awaitable(Kind.RealtimeSeconds, 0, Time.realtimeSinceStartup + seconds),
                             cancellationToken,
                             AwaitableScheduler.AddFrameWait);
        }

        /// <summary>
        /// Resumes after the scripts' next fixed update. At time scale 0 no fixed update runs, so it waits.
        /// </summary>
        /// <param name="cancellationToken">Ends the wait cancelled.</param>
        public static Awaitable FixedUpdateAsync(CancellationToken cancellationToken = default)
        {
            return StartWait(new Awaitable(Kind.FixedUpdate, 0, 0.0),
                             cancellationToken,
                             AwaitableScheduler.AddFixedUpdateWait);
        }

        /// <summary>
        /// Resumes after the scripts' late updates of this frame, or of the next one when they already ran.
        /// </summary>
        /// <param name="cancellationToken">Ends the wait cancelled.</param>
        public static Awaitable EndOfFrameAsync(CancellationToken cancellationToken = default)
        {
            return StartWait(new Awaitable(Kind.EndOfFrame, 0, 0.0),
                             cancellationToken,
                             AwaitableScheduler.AddEndOfFrameWait);
        }

        /// <summary>
        /// Continues on a worker thread, for work that calls no engine API. Code awaited from a worker
        /// stays on workers, and still belongs to the script that went there: <see cref="MainThreadAsync"/>
        /// comes back as that script (unless an await on the worker used <c>ConfigureAwait(false)</c>).
        /// </summary>
        public static Awaitable BackgroundThreadAsync()
        {
            var awaitable = new Awaitable(Kind.BackgroundThread, 0, 0.0);
            if (!AwaitableScheduler.isScriptThread)
            {
                awaitable.state = State.Completed;
            }
            return awaitable;
        }

        /// <summary>
        /// Continues on the script thread in the next frame, as the script whose code went to the worker
        /// with <see cref="BackgroundThreadAsync"/>. Awaited on the script thread, it continues at once.
        /// When that script is destroyed or play stops first, it ends cancelled: on the script thread when
        /// that happens while it waits, and on the worker when it happened before.
        /// </summary>
        public static Awaitable MainThreadAsync()
        {
            var awaitable = new Awaitable(Kind.MainThread, 0, 0.0);
            if (AwaitableScheduler.isScriptThread)
            {
                awaitable.state = State.Completed;
            }
            else if (!awaitable.canResume)
            {
                awaitable.StartCancelled(awaitable.endToken);
            }
            return awaitable;
        }

        /// <summary>
        /// Whether the wait is over, finished or cancelled.
        /// </summary>
        public bool isCompleted => state != State.Pending;

        /// <summary>
        /// Ends the wait cancelled: the code awaiting it gets an <see cref="OperationCanceledException"/>.
        /// Call it on the script thread.
        /// </summary>
        public void Cancel()
        {
            CancelNow(new CancellationToken(true));
        }

        /// <summary>
        /// Returns the awaiter the <c>await</c> operator uses.
        /// </summary>
        public Awaiter GetAwaiter()
        {
            return new Awaiter(this);
        }

        /// <summary>The script whose code created the wait, or null.</summary>
        internal ScriptComponent owner { get; }

        internal bool isDue
        {
            get
            {
                switch (kind)
                {
                    case Kind.NextFrame:
                        return Time.frameCount >= frame;
                    case Kind.Seconds:
                        return Time.time >= until;
                    case Kind.RealtimeSeconds:
                        return Time.realtimeSinceStartup >= until;
                    default:
                        return true;
                }
            }
        }

        // Whether code awaiting it can still resume: its script lives and play goes on.
        private bool canResume => context != null && context.isOpen;

        // The token of its script's or session's end, which is the reason it ends cancelled with them.
        private CancellationToken endToken => context != null ? context.token : new CancellationToken(true);

        internal void Complete()
        {
            if (state != State.Pending)
            {
                return;
            }
            state = State.Completed;
            Finish();
        }

        /// <summary>Ends the wait cancelled at once, resuming the code awaiting it.</summary>
        internal void CancelNow(CancellationToken token)
        {
            if (state != State.Pending)
            {
                return;
            }
            state = State.Canceled;
            cancelledBy = token;
            Finish();
        }

        /// <summary>Ends the wait cancelled because its script was destroyed or play stopped.</summary>
        internal void CancelWithContext()
        {
            CancelNow(endToken);
        }

        // Waits in the frame loop are made on the script thread. One that can never resume (no play, a
        // destroyed script, a cancelled token) starts cancelled and is not queued.
        private static Awaitable StartWait(Awaitable awaitable,
                                           CancellationToken cancellationToken,
                                           Action<Awaitable> queue)
        {
            if (!awaitable.canResume)
            {
                awaitable.StartCancelled(awaitable.endToken);
                return awaitable;
            }
            if (!AwaitableScheduler.isScriptThread)
            {
                throw new InvalidOperationException(
                    "Frame and time waits are made on the script thread. Await Awaitable.MainThreadAsync() first.");
            }
            if (cancellationToken.IsCancellationRequested)
            {
                awaitable.StartCancelled(cancellationToken);
                return awaitable;
            }
            if (cancellationToken.CanBeCanceled)
            {
                awaitable.registration = cancellationToken.UnsafeRegister(
                    static (state, token) => ((Awaitable)state).OnTokenCancelled(token), awaitable);
                if (awaitable.isCompleted)
                {
                    return awaitable;
                }
            }
            awaitable.isListed = true;
            awaitable.context.AddWait(awaitable);
            queue(awaitable);
            return awaitable;
        }

        private void StartCancelled(CancellationToken token)
        {
            state = State.Canceled;
            cancelledBy = token;
        }

        // Cancellation can come from any thread; the wait ends on the script thread. A hop back to the
        // script thread is cancelled only by its context's end: on the script thread, or on the worker
        // that registers after that end, where nothing else would run it.
        private void OnTokenCancelled(CancellationToken token)
        {
            if (AwaitableScheduler.isScriptThread || kind == Kind.MainThread)
            {
                CancelNow(token);
                return;
            }
            AwaitableScheduler.Post(context, _ => CancelNow(token), null);
        }

        private void Finish()
        {
            registration.Unregister();
            if (isListed)
            {
                isListed = false;
                context.RemoveWait(this);
            }
            RunContinuation();
        }

        private void RunContinuation()
        {
            Action resume = continuation;
            continuation = null;
            if (resume == null)
            {
                return;
            }
            if (AwaitableScheduler.isScriptThread)
            {
                AwaitableScheduler.Invoke(owner, static action => ((Action)action)(), resume);
            }
            else
            {
                AwaitableScheduler.RunOnWorker(owner, resume);
            }
        }

        // Never throws: the async machinery would rethrow it on the thread pool, which ends the process.
        private void SetContinuation(Action resume)
        {
            if (continuation != null)
            {
                Log.Error($"An Awaitable of {owner?.GetType().FullName ?? "no script"} was awaited a second time " +
                          "while it was pending. Await each Awaitable once; both awaits resume when it ends.");
                continuation += resume;
                return;
            }
            continuation = resume;
            if (state != State.Pending)
            {
                RunContinuation();
                return;
            }
            switch (kind)
            {
                case Kind.BackgroundThread:
                    state = State.Completed;
                    continuation = null;
                    AwaitableScheduler.RunOnWorker(owner, resume);
                    return;
                case Kind.MainThread:
                    PostToScriptThread();
                    return;
            }
        }

        // From a worker: the end of its script or of play cancels it, else it completes in the next pump.
        private void PostToScriptThread()
        {
            registration = context.token.UnsafeRegister(
                static (state, token) => ((Awaitable)state).OnTokenCancelled(token), this);
            if (!isCompleted)
            {
                AwaitableScheduler.Post(context, static state => ((Awaitable)state).Complete(), this);
            }
        }

        private void GetResult()
        {
            switch (state)
            {
                case State.Canceled:
                    throw new OperationCanceledException(cancelledBy);
                case State.Pending:
                    throw new InvalidOperationException("The Awaitable has not ended: await it instead of reading its result.");
            }
        }

        /// <summary>
        /// The awaiter of an <see cref="Awaitable"/>, used by the <c>await</c> operator.
        /// </summary>
        public readonly struct Awaiter : ICriticalNotifyCompletion
        {
            private readonly Awaitable awaitable;

            internal Awaiter(Awaitable awaitable)
            {
                this.awaitable = awaitable;
            }

            /// <summary>Whether the wait is already over.</summary>
            public bool IsCompleted => awaitable.isCompleted;

            /// <summary>Registers the code to resume when the wait is over.</summary>
            /// <param name="continuation">The code to resume.</param>
            public void OnCompleted(Action continuation)
            {
                awaitable.SetContinuation(continuation);
            }

            /// <summary>Registers the code to resume when the wait is over.</summary>
            /// <param name="continuation">The code to resume.</param>
            public void UnsafeOnCompleted(Action continuation)
            {
                awaitable.SetContinuation(continuation);
            }

            /// <summary>
            /// Ends the <c>await</c>: throws <see cref="OperationCanceledException"/> when the wait was
            /// cancelled, and <see cref="InvalidOperationException"/> when it has not ended yet.
            /// </summary>
            public void GetResult()
            {
                awaitable.GetResult();
            }
        }
    }
}
