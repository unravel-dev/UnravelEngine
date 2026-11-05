using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.ExceptionServices;
using System.Threading;

namespace Unravel.Core
{
    /// <summary>
    /// Brings the continuations of awaited script code back to the script thread, into the frame
    /// loop, so code after an <c>await</c> can call the engine. Each script has one for each play
    /// session, and the session has one more for code no script runs.
    /// </summary>
    /// <remarks>
    /// A script's context ends when the script is destroyed, and every context of a session ends when
    /// play stops. Ending cancels the context's token (the script's destroy token, or for the session
    /// the exit token) and its pending waits, as its script on the script thread, so the code awaiting
    /// them resumes cancelled at once and its catch and finally blocks run. What that code posts here
    /// while the context ends runs right after; everything else posted here, before or later, is dropped.
    /// </remarks>
    internal sealed class ScriptSynchronizationContext : SynchronizationContext
    {
        /// <summary>The life of a context, which only moves forward.</summary>
        internal enum State
        {
            /// <summary>Work posted here runs in the next pump.</summary>
            Open,
            /// <summary>Its cancellation runs: what that posts on the script thread runs when it closes.</summary>
            Ending,
            /// <summary>Nothing posted here runs.</summary>
            Closed,
        }

        /// <summary>How long a wait on the script thread may last before it is checked for pending async code.</summary>
        private const int BlockedWaitReportMilliseconds = 3000;

        /// <summary>How often a longer wait looks again for pending async code until it finds some.</summary>
        private const int BlockedWaitCheckMilliseconds = 500;

        private readonly object tokenLock = new object();
        private volatile State ownState;
        private CancellationTokenSource cancellation;
        private CancellationTokenRegistration sessionRegistration;
        private bool isTokenCancelled;
        private List<Awaitable> waits;

        /// <summary>
        /// Creates the context of @p owner in @p session, or with no owner the session's own context. A
        /// script's context made outside play starts closed.
        /// </summary>
        internal ScriptSynchronizationContext(ScriptComponent owner, ScriptSynchronizationContext session)
        {
            this.owner = owner;
            this.session = session;
            ownState = owner != null && session == null ? State.Closed : State.Open;
            // Blocking waits on the script thread come to Wait, which reports one that cannot end.
            SetWaitNotificationRequired();
        }

        /// <summary>The script continuations run as, or null for the session's own context.</summary>
        internal ScriptComponent owner { get; }

        /// <summary>The session context a script's context belongs to; null for the session's own context.</summary>
        internal ScriptSynchronizationContext session { get; }

        /// <summary>How far the context is in its life: its own state, or its session's when that is further.</summary>
        internal State state
        {
            get
            {
                State own = ownState;
                State ofSession = session != null ? session.ownState : own;
                return ofSession > own ? ofSession : own;
            }
        }

        /// <summary>Whether work posted here still runs: its script lives and its play session goes on.</summary>
        internal bool isOpen => state == State.Open;

        /// <summary>
        /// Cancelled when the context ends: a script's destroy token, or the session's exit token. Already
        /// cancelled once the context stops running work. From any thread.
        /// </summary>
        internal CancellationToken token
        {
            get
            {
                lock (tokenLock)
                {
                    if (cancellation == null)
                    {
                        if (isTokenCancelled || state != State.Open)
                        {
                            return new CancellationToken(true);
                        }
                        cancellation = new CancellationTokenSource();
                        if (session != null)
                        {
                            // Play stop cancels the session's token, and through it this one, as this script.
                            sessionRegistration = session.token.UnsafeRegister(
                                static context => ((ScriptSynchronizationContext)context).CancelToken(), this);
                        }
                    }
                    return cancellation.Token;
                }
            }
        }

        /// <summary>Queues the work for the next frame; from any thread, without waiting.</summary>
        public override void Post(SendOrPostCallback callback, object state)
        {
            AwaitableScheduler.Post(this, callback, state);
        }

        /// <summary>Runs the work now on the script thread. Other threads cannot wait for it.</summary>
        public override void Send(SendOrPostCallback callback, object state)
        {
            if (!AwaitableScheduler.isScriptThread)
            {
                throw new InvalidOperationException(
                    "A worker thread cannot wait for the script thread. Post the work instead, or await Awaitable.MainThreadAsync().");
            }
            using (ScriptComponent.Run(owner))
            {
                callback(state);
            }
        }

        public override SynchronizationContext CreateCopy()
        {
            return this;
        }

        /// <summary>
        /// A blocking wait on the script thread: .Result, .Wait(), GetAwaiter().GetResult() or a lock.
        /// Async script code resumes only on the script thread, so a wait there for such code never ends.
        /// A wait that lasts while async script code is pending is reported once. CoreCLR reports blocking
        /// waits to the context; Mono does not.
        /// </summary>
        public override int Wait(IntPtr[] waitHandles, bool waitAll, int millisecondsTimeout)
        {
            bool isShort = millisecondsTimeout != Timeout.Infinite && millisecondsTimeout <= BlockedWaitReportMilliseconds;
            if (isShort || !AwaitableScheduler.isScriptThread)
            {
                return WaitHelper(waitHandles, waitAll, millisecondsTimeout);
            }
            long started = Environment.TickCount64;
            int slice = BlockedWaitReportMilliseconds;
            while (true)
            {
                int result = WaitHelper(waitHandles, waitAll, slice);
                long waited = Environment.TickCount64 - started;
                bool isOver = millisecondsTimeout != Timeout.Infinite && waited >= millisecondsTimeout;
                if (result != WaitHandle.WaitTimeout || isOver)
                {
                    return result;
                }
                if (AwaitableScheduler.hasPendingWork)
                {
                    ReportBlockedWait(waited);
                    int rest = millisecondsTimeout == Timeout.Infinite ? Timeout.Infinite : (int)(millisecondsTimeout - waited);
                    return WaitHelper(waitHandles, waitAll, rest);
                }
                slice = millisecondsTimeout == Timeout.Infinite
                            ? BlockedWaitCheckMilliseconds
                            : (int)Math.Min(BlockedWaitCheckMilliseconds, millisecondsTimeout - waited);
            }
        }

        /// <summary>Starts ending the context, on the script thread; false when it was not open.</summary>
        internal bool BeginEnd()
        {
            if (ownState != State.Open)
            {
                return false;
            }
            ownState = State.Ending;
            return true;
        }

        /// <summary>Closes the context: nothing posted here runs after this.</summary>
        internal void Close()
        {
            ownState = State.Closed;
        }

        /// <summary>
        /// Cancels the token once. On the script thread it runs as the owner, so the code awaiting the
        /// token resumes as the owner.
        /// </summary>
        internal void CancelToken()
        {
            CancellationTokenSource source;
            CancellationTokenRegistration fromSession;
            lock (tokenLock)
            {
                if (isTokenCancelled)
                {
                    return;
                }
                isTokenCancelled = true;
                source = cancellation;
                fromSession = sessionRegistration;
            }
            fromSession.Unregister();
            if (source == null)
            {
                return;
            }
            try
            {
                if (AwaitableScheduler.isScriptThread)
                {
                    using (ScriptComponent.Run(owner))
                    {
                        source.Cancel();
                    }
                }
                else
                {
                    // Only a token made on a worker while play stopped gets here, and nothing awaits it yet.
                    source.Cancel();
                }
            }
            catch (Exception ex)
            {
                string token = owner != null ? $"the destroy token of {owner.GetType().FullName}" : "Application.exitCancellationToken";
                Log.Error($"A callback registered on {token} failed: {ex}");
            }
        }

        /// <summary>Records a pending wait made by this context's code, so its end cancels it. Script thread.</summary>
        internal void AddWait(Awaitable awaitable)
        {
            (waits ??= new List<Awaitable>()).Add(awaitable);
        }

        /// <summary>Forgets a wait that ended. Script thread.</summary>
        internal void RemoveWait(Awaitable awaitable)
        {
            waits?.Remove(awaitable);
        }

        /// <summary>Takes the pending waits, to end them, or null when there are none. Script thread.</summary>
        internal List<Awaitable> TakeWaits()
        {
            List<Awaitable> taken = waits;
            waits = null;
            return taken;
        }

        private static void ReportBlockedWait(long waitedMilliseconds)
        {
            Log.Error($"The script thread has been blocked for {waitedMilliseconds / 1000} seconds in a wait (.Result, .Wait(), " +
                      "GetAwaiter().GetResult() or a lock) while async script code waits to resume on it. If the wait is " +
                      "for that code, it never ends: await it instead, or use ConfigureAwait(false) inside it.\n" +
                      new StackTrace(1, true));
        }
    }

    /// <summary>
    /// The context of script code that went to a worker with <see cref="Awaitable.BackgroundThreadAsync"/>:
    /// it keeps the script the code belongs to across awaits on workers, so
    /// <see cref="Awaitable.MainThreadAsync"/> comes back as that script. Work posted here runs on the
    /// thread pool. Workers run no script, so <see cref="ScriptComponent.running"/> stays null on them.
    /// </summary>
    internal sealed class WorkerSynchronizationContext : SynchronizationContext
    {
        internal WorkerSynchronizationContext(ScriptComponent owner)
        {
            this.owner = owner;
        }

        /// <summary>The script whose code runs here, or null for code no script runs.</summary>
        internal ScriptComponent owner { get; }

        /// <summary>Queues the work to the thread pool, where it runs with this context.</summary>
        public override void Post(SendOrPostCallback callback, object state)
        {
            ThreadPool.UnsafeQueueUserWorkItem(static work => work.context.Run(work.callback, work.state),
                                               (context: this, callback, state),
                                               preferLocal: false);
        }

        /// <summary>Runs the work at once, on this thread.</summary>
        public override void Send(SendOrPostCallback callback, object state)
        {
            callback(state);
        }

        public override SynchronizationContext CreateCopy()
        {
            return this;
        }

        /// <summary>Runs the work with this context, logging what it throws as the script thread does.</summary>
        internal void Run(SendOrPostCallback callback, object state)
        {
            SynchronizationContext previous = Current;
            SetSynchronizationContext(this);
            try
            {
                callback(state);
            }
            catch (Exception ex)
            {
                AwaitableScheduler.ReportException(owner, ex);
            }
            finally
            {
                SetSynchronizationContext(previous);
            }
        }
    }

    /// <summary>
    /// Runs awaited script code on the script thread once per frame, after the coroutines: the
    /// continuations posted to the scripts' contexts, and the <see cref="Awaitable"/> waits that are
    /// due. Nothing runs while the game is paused, and nothing after play stops.
    /// </summary>
    [AutoStaticsCleanup]
    internal static class AwaitableScheduler
    {
        private struct PostedWork
        {
            internal ScriptSynchronizationContext context;
            internal SendOrPostCallback callback;
            internal object state;
        }

        /// <summary>
        /// The waits one pump resumes, in the order they were made. The code a pump resumes can add waits
        /// (they wait for the next pump), end waits, destroy scripts and run a pump again: the list only
        /// grows while it is pumped, and the waits that ended leave it when the outermost pump is done.
        /// </summary>
        private sealed class WaitList
        {
            private readonly List<Awaitable> waits = new List<Awaitable>();
            private int pumpDepth;

            /// <summary>Whether a wait in the list has not ended yet.</summary>
            internal bool hasPending => waits.Exists(static awaitable => !awaitable.isCompleted);

            internal void Add(Awaitable awaitable)
            {
                waits.Add(awaitable);
            }

            /// <summary>Resumes the waits that are due.</summary>
            internal void ResumeDue()
            {
                int count = waits.Count;
                pumpDepth++;
                try
                {
                    for (int i = 0; i < count; i++)
                    {
                        Awaitable awaitable = waits[i];
                        if (!awaitable.isCompleted && awaitable.isDue)
                        {
                            awaitable.Complete();
                        }
                    }
                }
                finally
                {
                    pumpDepth--;
                    RemoveEnded();
                }
            }

            /// <summary>Ends every pending wait cancelled, as its context ended.</summary>
            internal void CancelAll()
            {
                int count = waits.Count;
                for (int i = 0; i < count; i++)
                {
                    waits[i].CancelWithContext();
                }
                RemoveEnded();
            }

            private void RemoveEnded()
            {
                if (pumpDepth == 0)
                {
                    waits.RemoveAll(static awaitable => awaitable.isCompleted);
                }
            }
        }

        private static ConcurrentQueue<PostedWork> posted = new ConcurrentQueue<PostedWork>();
        private static List<PostedWork> endingWork = new List<PostedWork>();
        private static WaitList frameWaits = new WaitList();
        private static WaitList fixedUpdateWaits = new WaitList();
        private static WaitList endOfFrameWaits = new WaitList();
        private static volatile ScriptSynchronizationContext session;
        private static int scriptThreadId = -1;

        /// <summary>The context of code no script runs during play, or null outside play.</summary>
        internal static ScriptSynchronizationContext sessionContext => session;

        internal static bool isScriptThread => Environment.CurrentManagedThreadId == scriptThreadId;

        /// <summary>Cancelled when play stops; already cancelled outside play.</summary>
        internal static CancellationToken exitToken
        {
            get
            {
                ScriptSynchronizationContext current = session;
                return current != null ? current.token : new CancellationToken(true);
            }
        }

        /// <summary>
        /// The script whose code runs on this thread: the running script on the script thread, and on a
        /// worker the script whose code went there with <see cref="Awaitable.BackgroundThreadAsync"/>.
        /// </summary>
        internal static ScriptComponent currentOwner =>
            isScriptThread ? ScriptComponent.running : (SynchronizationContext.Current as WorkerSynchronizationContext)?.owner;

        /// <summary>Whether async script code waits to resume: posted work, or a wait not ended yet. Script thread.</summary>
        internal static bool hasPendingWork =>
            !posted.IsEmpty || frameWaits.hasPending || fixedUpdateWaits.hasPending || endOfFrameWaits.hasPending;

        private static void OnStaticsCleanup()
        {
            // Contexts this assembly made may still be referenced from timers: a closed session drops what they get.
            session?.Close();
            session = null;
            posted = new ConcurrentQueue<PostedWork>();
            endingWork = new List<PostedWork>();
            frameWaits = new WaitList();
            fixedUpdateWaits = new WaitList();
            endOfFrameWaits = new WaitList();
            // A context left on the thread would keep this assembly's code alive past the unload.
            SynchronizationContext.SetSynchronizationContext(null);
        }

        /// <summary>The context code of @p script awaits on: its own, or the session's for no script.</summary>
        internal static ScriptSynchronizationContext ContextFor(ScriptComponent script)
        {
            return script != null ? script.synchronizationContext : session;
        }

        internal static void BeginPlay()
        {
            EndPlay();
            scriptThreadId = Environment.CurrentManagedThreadId;
            session = new ScriptSynchronizationContext(null, null);
        }

        /// <summary>
        /// Ends the play session, while its scene is still whole. Every script's destroy token and the
        /// exit token are cancelled, and every pending wait ends cancelled, each as its script, so the
        /// code awaiting them runs its catch and finally blocks at once. Nothing else of the session runs
        /// after this: work still queued, and work that completes later, is dropped.
        /// </summary>
        internal static void EndPlay()
        {
            ScriptSynchronizationContext ending = session;
            if (ending == null)
            {
                return;
            }
            if (ending.BeginEnd())
            {
                // Cancels every script's token too, each as its script.
                ending.CancelToken();
                frameWaits.CancelAll();
                fixedUpdateWaits.CancelAll();
                endOfFrameWaits.CancelAll();
                ending.Close();
                RunEndingWork();
            }
            session = null;
            DropPostedWork();
        }

        /// <summary>
        /// Queues work for the next pump, from any thread. Work an ending context's cancellation posts on
        /// the script thread runs when the context closes; any other work for a context that is not open
        /// is dropped.
        /// </summary>
        internal static void Post(ScriptSynchronizationContext context, SendOrPostCallback callback, object state)
        {
            var work = new PostedWork { context = context, callback = callback, state = state };
            ScriptSynchronizationContext.State contextState = context.state;
            if (contextState == ScriptSynchronizationContext.State.Open)
            {
                posted.Enqueue(work);
            }
            else if (contextState == ScriptSynchronizationContext.State.Ending && isScriptThread)
            {
                endingWork.Add(work);
            }
            else
            {
                Drop(work);
            }
        }

        /// <summary>
        /// Ends the async work of a script being destroyed, before its OnDestroy: its destroy token is
        /// cancelled and its waits end cancelled, as the script, so the code awaiting them runs its catch
        /// and finally blocks once. Its other work, queued or still to come, is dropped.
        /// </summary>
        internal static void OnScriptDestroyed(ScriptComponent script)
        {
            ScriptSynchronizationContext context = script.synchronizationContext;
            if (!context.BeginEnd())
            {
                return;
            }
            context.CancelToken();
            List<Awaitable> waits = context.TakeWaits();
            if (waits != null)
            {
                foreach (Awaitable awaitable in waits)
                {
                    awaitable.CancelWithContext();
                }
            }
            context.Close();
            RunEndingWork();
        }

        internal static void AddFrameWait(Awaitable awaitable)
        {
            frameWaits.Add(awaitable);
        }

        internal static void AddFixedUpdateWait(Awaitable awaitable)
        {
            fixedUpdateWaits.Add(awaitable);
        }

        internal static void AddEndOfFrameWait(Awaitable awaitable)
        {
            endOfFrameWaits.Add(awaitable);
        }

        /// <summary>The frame pump, after the coroutines: due waits first, then the posted work.</summary>
        internal static void Tick()
        {
            frameWaits.ResumeDue();
            RunPostedWork();
        }

        /// <summary>After the scripts' fixed updates.</summary>
        internal static void TickFixedUpdate()
        {
            fixedUpdateWaits.ResumeDue();
        }

        /// <summary>After the scripts' late updates.</summary>
        internal static void TickEndOfFrame()
        {
            endOfFrameWaits.ResumeDue();
        }

        /// <summary>Runs @p callback as @p owner, logging what it throws, as updates and coroutines do.</summary>
        internal static void Invoke(ScriptComponent owner, SendOrPostCallback callback, object state)
        {
            try
            {
                using (ScriptComponent.Run(owner))
                {
                    callback(state);
                }
            }
            catch (Exception ex)
            {
                ReportException(owner, ex);
            }
        }

        /// <summary>Runs @p resume on a worker as code of @p owner, so it can come back as that script.</summary>
        internal static void RunOnWorker(ScriptComponent owner, Action resume)
        {
            new WorkerSynchronizationContext(owner).Post(static action => ((Action)action)(), resume);
        }

        /// <summary>Logs what async code of @p owner threw; a cancellation by a token is not an error.</summary>
        internal static void ReportException(ScriptComponent owner, Exception exception)
        {
            if (exception is OperationCanceledException canceled && canceled.CancellationToken.IsCancellationRequested)
            {
                // Cancelled on purpose: by a token, the script's destruction or the end of play.
                return;
            }
            string name = owner != null ? owner.GetType().FullName : "a script";
            Log.Error($"Async code of {name}: {exception}");
        }

        // Only what was queued before the pump started: a continuation that posts again runs next frame.
        private static void RunPostedWork()
        {
            int count = posted.Count;
            for (int i = 0; i < count && posted.TryDequeue(out PostedWork work); i++)
            {
                if (work.context.isOpen)
                {
                    Invoke(work.context.owner, work.callback, work.state);
                }
                else
                {
                    Drop(work);
                }
            }
        }

        // Runs once what the code a context's end resumed posted to an ending context. Work for a context
        // that is still ending (an outer end, when a cancelled continuation destroyed a script) waits for
        // that context's own end; a closed context drops what this work posts in turn.
        private static void RunEndingWork()
        {
            if (endingWork.Count == 0)
            {
                return;
            }
            List<PostedWork> batch = endingWork;
            endingWork = new List<PostedWork>();
            foreach (PostedWork work in batch)
            {
                Invoke(work.context.owner, work.callback, work.state);
            }
        }

        private static void DropPostedWork()
        {
            while (posted.TryDequeue(out PostedWork work))
            {
                Drop(work);
            }
        }

        // Work that will not run. An exception async void code posted to be thrown here is still logged.
        private static void Drop(in PostedWork work)
        {
            if (work.state is ExceptionDispatchInfo thrown)
            {
                ReportException(work.context.owner, thrown.SourceException);
            }
        }
    }
}
