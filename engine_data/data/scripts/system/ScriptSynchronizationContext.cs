using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Threading;

namespace Unravel.Core
{
    /// <summary>
    /// Brings the continuations of awaited script code back to the script thread, into the frame
    /// loop, so code after an <c>await</c> can call the engine. Each script has one, so what a
    /// destroyed script still has in flight is dropped; the play session has one more for code no
    /// script runs.
    /// </summary>
    internal sealed class ScriptSynchronizationContext : SynchronizationContext
    {
        private volatile bool isClosed;

        internal ScriptSynchronizationContext(ScriptComponent owner)
        {
            this.owner = owner;
        }

        /// <summary>The script continuations run as, or null for the session's own context.</summary>
        internal ScriptComponent owner { get; }

        /// <summary>Whether work posted here still runs: its script lives and the game plays.</summary>
        internal bool isOpen => !isClosed && AwaitableScheduler.isPlaying;

        /// <summary>Drops everything posted here from now on.</summary>
        internal void Close()
        {
            isClosed = true;
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

        private static ConcurrentQueue<PostedWork> posted = new ConcurrentQueue<PostedWork>();
        private static List<Awaitable> frameWaits = new List<Awaitable>();
        private static List<Awaitable> fixedUpdateWaits = new List<Awaitable>();
        private static List<Awaitable> endOfFrameWaits = new List<Awaitable>();
        private static CancellationTokenSource exitCancellation;
        private static volatile bool playing;
        private static int scriptThreadId = -1;

        /// <summary>The context of code no script runs during play, or null outside play.</summary>
        internal static ScriptSynchronizationContext sessionContext { get; private set; }

        internal static bool isPlaying => playing;

        internal static bool isScriptThread => Environment.CurrentManagedThreadId == scriptThreadId;

        /// <summary>Cancelled when play stops; already cancelled outside play.</summary>
        internal static CancellationToken exitToken =>
            exitCancellation != null && playing ? exitCancellation.Token : new CancellationToken(true);

        private static void OnStaticsCleanup()
        {
            playing = false;
            posted = new ConcurrentQueue<PostedWork>();
            frameWaits = new List<Awaitable>();
            fixedUpdateWaits = new List<Awaitable>();
            endOfFrameWaits = new List<Awaitable>();
            exitCancellation?.Dispose();
            exitCancellation = null;
            sessionContext = null;
            // A context left on the thread would keep this assembly's code alive past the unload.
            SynchronizationContext.SetSynchronizationContext(null);
        }

        /// <summary>The context code of @p script awaits on: its own, or the session's for no script.</summary>
        internal static SynchronizationContext ContextFor(ScriptComponent script)
        {
            return script != null ? script.synchronizationContext : sessionContext;
        }

        internal static void BeginPlay()
        {
            scriptThreadId = Environment.CurrentManagedThreadId;
            exitCancellation = new CancellationTokenSource();
            sessionContext = new ScriptSynchronizationContext(null);
            playing = true;
        }

        /// <summary>Drops every pending continuation and wait without running any of them.</summary>
        internal static void EndPlay()
        {
            playing = false;
            sessionContext?.Close();
            sessionContext = null;
            posted.Clear();
            frameWaits.Clear();
            fixedUpdateWaits.Clear();
            endOfFrameWaits.Clear();
            try
            {
                // Frees what scripts registered on it, Task.Delay timers among them.
                exitCancellation?.Cancel();
            }
            catch (Exception ex)
            {
                Log.Error($"A callback registered on Application.exitCancellationToken failed: {ex}");
            }
        }

        /// <summary>Queues work for the next pump, from any thread; dropped when its context is closed.</summary>
        internal static void Post(ScriptSynchronizationContext context, SendOrPostCallback callback, object state)
        {
            if (context.isOpen)
            {
                posted.Enqueue(new PostedWork { context = context, callback = callback, state = state });
            }
        }

        /// <summary>
        /// Ends the async work of a script being destroyed, before its OnDestroy and while its entity
        /// is still whole: its code learns it was cancelled, so its catch and finally blocks run once,
        /// and what it posts after that is dropped.
        /// </summary>
        internal static void OnScriptDestroyed(ScriptComponent script)
        {
            ScriptSynchronizationContext context = script.synchronizationContext;
            using (ScriptComponent.Run(script))
            {
                script.CancelDestroyToken();
                CancelOwnedBy(frameWaits, script);
                CancelOwnedBy(fixedUpdateWaits, script);
                CancelOwnedBy(endOfFrameWaits, script);
                RunPostedWorkOf(context);
            }
            context.Close();
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
            ResumeDue(frameWaits);
            RunPostedWork();
        }

        /// <summary>After the scripts' fixed updates.</summary>
        internal static void TickFixedUpdate()
        {
            ResumeDue(fixedUpdateWaits);
        }

        /// <summary>After the scripts' late updates.</summary>
        internal static void TickEndOfFrame()
        {
            ResumeDue(endOfFrameWaits);
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
            catch (OperationCanceledException ex) when (ex.CancellationToken.IsCancellationRequested)
            {
                // Cancelled on purpose: by a token, the script's destruction or the end of play.
            }
            catch (Exception ex)
            {
                string name = owner != null ? owner.GetType().FullName : "a script";
                Log.Error($"Async code of {name}: {ex}");
            }
        }

        // Waits added while these run (by the code they resume) wait for the next pump.
        private static void ResumeDue(List<Awaitable> waits)
        {
            int count = waits.Count;
            for (int i = 0; i < count; i++)
            {
                Awaitable awaitable = waits[i];
                if (!awaitable.isCompleted && awaitable.isDue)
                {
                    awaitable.Complete();
                }
            }
            waits.RemoveAll(awaitable => awaitable.isCompleted);
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
            }
        }

        // What awaited a cancelled task of the script was posted here, not run: it runs now. The rest
        // of the queue keeps its order.
        private static void RunPostedWorkOf(ScriptSynchronizationContext context)
        {
            int count = posted.Count;
            for (int i = 0; i < count && posted.TryDequeue(out PostedWork work); i++)
            {
                if (ReferenceEquals(work.context, context))
                {
                    Invoke(context.owner, work.callback, work.state);
                }
                else
                {
                    posted.Enqueue(work);
                }
            }
        }

        private static void CancelOwnedBy(List<Awaitable> waits, ScriptComponent script)
        {
            int count = waits.Count;
            for (int i = 0; i < count; i++)
            {
                Awaitable awaitable = waits[i];
                if (ReferenceEquals(awaitable.owner, script))
                {
                    awaitable.CancelNow();
                }
            }
            waits.RemoveAll(awaitable => awaitable.isCompleted);
        }
    }
}
