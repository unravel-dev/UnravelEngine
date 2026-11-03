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
    /// Every <c>await</c> in a script's code comes back to that script: after an awaited
    /// <see cref="System.Threading.Tasks.Task"/> too, in the frame after the task finished. Nothing
    /// resumes while the game is paused or after play stops, and the waits of a destroyed script end
    /// cancelled, so its <c>finally</c> blocks run once. Pass <see cref="ScriptComponent.destroyCancellationToken"/>
    /// to other awaited calls so they stop with the script.
    /// An Awaitable can be awaited once.
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

        private Awaitable(Kind kind, long frame, double until)
        {
            this.kind = kind;
            this.frame = frame;
            this.until = until;
            owner = ScriptComponent.running;
            context = AwaitableScheduler.ContextFor(owner) as ScriptSynchronizationContext;
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
        /// stays on it. <see cref="MainThreadAsync"/> comes back.
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
        /// Continues on the script thread in the next frame, after a hop to a worker thread. Awaited on
        /// the script thread, it continues at once. Use the script's destroy token to stop work a
        /// destroyed script started.
        /// </summary>
        public static Awaitable MainThreadAsync()
        {
            var awaitable = new Awaitable(Kind.MainThread, 0, 0.0);
            if (AwaitableScheduler.isScriptThread)
            {
                awaitable.state = State.Completed;
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
            CancelNow(default);
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

        internal void Complete()
        {
            if (state != State.Pending)
            {
                return;
            }
            state = State.Completed;
            registration.Unregister();
            RunContinuation();
        }

        /// <summary>Ends the wait cancelled now, on the script thread, resuming the code awaiting it.</summary>
        internal void CancelNow(CancellationToken token)
        {
            if (state != State.Pending)
            {
                return;
            }
            state = State.Canceled;
            cancelledBy = token;
            registration.Unregister();
            RunContinuation();
        }

        /// <summary>Ends the wait of a destroyed script, as cancelled by its destroy token.</summary>
        internal void CancelNow()
        {
            CancelNow(owner != null ? owner.destroyCancellationToken : default);
        }

        // Waits in the frame loop are made on the script thread. One that can never resume (no play, a
        // destroyed script, a cancelled token) starts cancelled and is not queued.
        private static Awaitable StartWait(Awaitable awaitable,
                                           CancellationToken cancellationToken,
                                           Action<Awaitable> queue)
        {
            if (!AwaitableScheduler.isPlaying)
            {
                awaitable.state = State.Canceled;
                awaitable.cancelledBy = AwaitableScheduler.exitToken;
                return awaitable;
            }
            if (!AwaitableScheduler.isScriptThread)
            {
                throw new InvalidOperationException(
                    "Frame and time waits are made on the script thread. Await Awaitable.MainThreadAsync() first.");
            }
            if (cancellationToken.IsCancellationRequested)
            {
                awaitable.state = State.Canceled;
                awaitable.cancelledBy = cancellationToken;
                return awaitable;
            }
            if (awaitable.owner != null && awaitable.owner.isDestroyed)
            {
                awaitable.state = State.Canceled;
                awaitable.cancelledBy = awaitable.owner.destroyCancellationToken;
                return awaitable;
            }
            if (cancellationToken.CanBeCanceled)
            {
                awaitable.registration = cancellationToken.UnsafeRegister(
                    static (state, token) => ((Awaitable)state).OnTokenCancelled(token), awaitable);
            }
            queue(awaitable);
            return awaitable;
        }

        // Cancellation can come from any thread; the wait ends on the script thread.
        private void OnTokenCancelled(CancellationToken token)
        {
            if (AwaitableScheduler.isScriptThread)
            {
                CancelNow(token);
                return;
            }
            var target = context ?? AwaitableScheduler.sessionContext;
            if (target != null)
            {
                AwaitableScheduler.Post(target, _ => CancelNow(token), null);
            }
        }

        private void RunContinuation()
        {
            Action resume = continuation;
            continuation = null;
            if (resume != null)
            {
                AwaitableScheduler.Invoke(owner, static state => ((Action)state)(), resume);
            }
        }

        private void SetContinuation(Action resume)
        {
            if (continuation != null)
            {
                throw new InvalidOperationException("An Awaitable can be awaited once.");
            }
            switch (kind)
            {
                case Kind.BackgroundThread:
                    state = State.Completed;
                    ThreadPool.UnsafeQueueUserWorkItem(static action => action(), resume, preferLocal: false);
                    return;
                case Kind.MainThread:
                    PostToScriptThread(resume);
                    return;
            }
            if (state != State.Pending)
            {
                AwaitableScheduler.Invoke(owner, static state => ((Action)state)(), resume);
                return;
            }
            continuation = resume;
        }

        // From a worker: resumes in the next pump as the session's code; dropped when play has stopped.
        private void PostToScriptThread(Action resume)
        {
            var target = AwaitableScheduler.sessionContext;
            if (target == null)
            {
                return;
            }
            continuation = resume;
            AwaitableScheduler.Post(target, static state => ((Awaitable)state).Complete(), this);
        }

        private void GetResult()
        {
            if (state == State.Canceled)
            {
                throw new OperationCanceledException(cancelledBy);
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
            public void OnCompleted(Action continuation)
            {
                awaitable.SetContinuation(continuation);
            }

            /// <summary>Registers the code to resume when the wait is over.</summary>
            public void UnsafeOnCompleted(Action continuation)
            {
                awaitable.SetContinuation(continuation);
            }

            /// <summary>Ends the <c>await</c>: throws <see cref="OperationCanceledException"/> when the wait was cancelled.</summary>
            public void GetResult()
            {
                awaitable.GetResult();
            }
        }
    }
}
