using System;
using System.Collections;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// A coroutine started with <see cref="ScriptComponent.StartCoroutine"/>. Yield it from another
    /// coroutine to wait until it finishes.
    /// </summary>
    /// <remarks>
    /// A coroutine is an iterator method that runs a step every frame until its next
    /// <c>yield return</c>. It can yield:
    /// <list type="bullet">
    /// <item><c>null</c> - resume next frame;</item>
    /// <item><see cref="WaitForSeconds"/>, <see cref="WaitForSecondsRealtime"/>, <see cref="WaitForFrames"/>,
    /// <see cref="WaitUntil"/>, <see cref="WaitWhile"/>;</item>
    /// <item>another <see cref="IEnumerator"/> - runs inline and returns here when it ends;</item>
    /// <item>a <see cref="Coroutine"/> - waits until that coroutine finishes.</item>
    /// </list>
    /// Coroutines run after every script's <c>OnUpdate</c>, follow the game clock (they pause with
    /// the game), and stop when their component is disabled or destroyed.
    /// </remarks>
    public sealed class Coroutine
    {
        internal enum WaitKind
        {
            NextFrame,
            Seconds,
            RealtimeSeconds,
            Frames,
            Until,
            While,
            Coroutine,
        }

        internal readonly ScriptComponent owner;
        internal readonly Stack<IEnumerator> routines = new Stack<IEnumerator>();
        internal WaitKind waitKind;
        internal double waitUntil;
        internal long waitFrame;
        internal Func<bool> waitCondition;
        internal Coroutine waitCoroutine;

        internal Coroutine(ScriptComponent owner, IEnumerator routine)
        {
            this.owner = owner;
            routines.Push(routine);
            isRunning = true;
        }

        /// <summary>
        /// Whether the coroutine has not finished or been stopped yet.
        /// </summary>
        public bool isRunning { get; private set; }

        /// <summary>
        /// Stops the coroutine. It does not resume again.
        /// </summary>
        public void Stop()
        {
            isRunning = false;
            routines.Clear();
            waitCondition = null;
            waitCoroutine = null;
        }
    }

    /// <summary>
    /// Base of the wait objects a coroutine can yield. They only describe the wait, so one
    /// instance can be cached and yielded many times.
    /// </summary>
    public abstract class YieldInstruction
    {
    }

    /// <summary>
    /// Resumes the coroutine after the given number of seconds of game time (scaled by
    /// <see cref="Time.timeScale"/>).
    /// </summary>
    public sealed class WaitForSeconds : YieldInstruction
    {
        /// <summary>
        /// Seconds of game time to wait.
        /// </summary>
        public readonly float seconds;

        /// <summary>
        /// Creates the wait.
        /// </summary>
        /// <param name="seconds">Seconds of game time to wait.</param>
        public WaitForSeconds(float seconds)
        {
            this.seconds = seconds;
        }
    }

    /// <summary>
    /// Resumes the coroutine after the given number of real seconds, whatever the time scale.
    /// </summary>
    public sealed class WaitForSecondsRealtime : YieldInstruction
    {
        /// <summary>
        /// Real seconds to wait.
        /// </summary>
        public readonly float seconds;

        /// <summary>
        /// Creates the wait.
        /// </summary>
        /// <param name="seconds">Real seconds to wait.</param>
        public WaitForSecondsRealtime(float seconds)
        {
            this.seconds = seconds;
        }
    }

    /// <summary>
    /// Resumes the coroutine after the given number of frames.
    /// </summary>
    public sealed class WaitForFrames : YieldInstruction
    {
        /// <summary>
        /// Frames to wait; 1 is the same as yielding <c>null</c>.
        /// </summary>
        public readonly int frames;

        /// <summary>
        /// Creates the wait.
        /// </summary>
        /// <param name="frames">Frames to wait; 1 is the same as yielding <c>null</c>.</param>
        public WaitForFrames(int frames)
        {
            this.frames = frames;
        }
    }

    /// <summary>
    /// Resumes the coroutine in the first frame the condition is true (checked once per frame).
    /// </summary>
    public sealed class WaitUntil : YieldInstruction
    {
        internal readonly Func<bool> condition;

        /// <summary>
        /// Creates the wait.
        /// </summary>
        /// <param name="condition">Checked once per frame.</param>
        public WaitUntil(Func<bool> condition)
        {
            this.condition = condition ?? throw new ArgumentNullException(nameof(condition));
        }
    }

    /// <summary>
    /// Resumes the coroutine in the first frame the condition is false (checked once per frame).
    /// </summary>
    public sealed class WaitWhile : YieldInstruction
    {
        internal readonly Func<bool> condition;

        /// <summary>
        /// Creates the wait.
        /// </summary>
        /// <param name="condition">Checked once per frame.</param>
        public WaitWhile(Func<bool> condition)
        {
            this.condition = condition ?? throw new ArgumentNullException(nameof(condition));
        }
    }

    /// <summary>
    /// Runs every coroutine one step per frame, after the scripts' updates.
    /// </summary>
    [AutoStaticsCleanup]
    internal static class CoroutineScheduler
    {
        private static List<Coroutine> running_ = new List<Coroutine>();

        private static void OnStaticsCleanup()
        {
            running_ = new List<Coroutine>();
        }

        /// <summary>
        /// Starts a coroutine: runs it up to its first yield now, then once per frame.
        /// </summary>
        internal static Coroutine Start(ScriptComponent owner, IEnumerator routine)
        {
            if (routine == null)
            {
                throw new ArgumentNullException(nameof(routine));
            }
            var coroutine = new Coroutine(owner, routine);
            if (Advance(coroutine))
            {
                running_.Add(coroutine);
            }
            else
            {
                coroutine.Stop();
            }
            return coroutine;
        }

        /// <summary>
        /// Stops every coroutine started by @p owner.
        /// </summary>
        internal static void StopAll(ScriptComponent owner)
        {
            foreach (Coroutine coroutine in running_)
            {
                if (ReferenceEquals(coroutine.owner, owner))
                {
                    coroutine.Stop();
                }
            }
        }

        /// <summary>
        /// Advances every running coroutine whose wait is over. Coroutines started during the tick
        /// wait for the next one: they already ran their first step.
        /// </summary>
        internal static void Tick()
        {
            int count = running_.Count;
            for (int i = 0; i < count; i++)
            {
                Coroutine coroutine = running_[i];
                if (!coroutine.isRunning)
                {
                    continue;
                }
                if (!coroutine.owner.owner.IsValid())
                {
                    coroutine.Stop();
                    continue;
                }
                if (IsWaiting(coroutine))
                {
                    continue;
                }
                if (!Advance(coroutine))
                {
                    coroutine.Stop();
                }
            }
            running_.RemoveAll(c => !c.isRunning);
        }

        private static bool IsWaiting(Coroutine coroutine)
        {
            switch (coroutine.waitKind)
            {
                case Coroutine.WaitKind.NextFrame:
                case Coroutine.WaitKind.Frames:
                    return Time.frameCount < coroutine.waitFrame;
                case Coroutine.WaitKind.Seconds:
                    return Time.time < coroutine.waitUntil;
                case Coroutine.WaitKind.RealtimeSeconds:
                    return Time.realtimeSinceStartup < coroutine.waitUntil;
                case Coroutine.WaitKind.Until:
                    return !Check(coroutine, coroutine.waitCondition, false);
                case Coroutine.WaitKind.While:
                    return Check(coroutine, coroutine.waitCondition, true);
                case Coroutine.WaitKind.Coroutine:
                    return coroutine.waitCoroutine.isRunning;
            }
            return false;
        }

        /// <summary>The condition's value; a throwing condition stops the coroutine and reads as @p onError.</summary>
        private static bool Check(Coroutine coroutine, Func<bool> condition, bool onError)
        {
            try
            {
                return condition();
            }
            catch (Exception ex)
            {
                Report(coroutine, ex);
                coroutine.Stop();
                return onError;
            }
        }

        /// <summary>
        /// Runs the coroutine up to its next wait. Nested iterators run inline: a finished one
        /// returns to its caller within the same step.
        /// </summary>
        /// <returns>False when the coroutine finished or failed.</returns>
        private static bool Advance(Coroutine coroutine)
        {
            while (coroutine.isRunning && coroutine.routines.Count > 0)
            {
                IEnumerator routine = coroutine.routines.Peek();
                bool moved;
                try
                {
                    using (ScriptComponent.Run(coroutine.owner))
                    {
                        moved = routine.MoveNext();
                    }
                }
                catch (Exception ex)
                {
                    Report(coroutine, ex);
                    return false;
                }
                if (!moved)
                {
                    coroutine.routines.Pop();
                    continue;
                }
                object yielded = routine.Current;
                if (yielded is IEnumerator nested)
                {
                    coroutine.routines.Push(nested);
                    continue;
                }
                SetWait(coroutine, yielded);
                return true;
            }
            return false;
        }

        private static void SetWait(Coroutine coroutine, object yielded)
        {
            coroutine.waitCondition = null;
            coroutine.waitCoroutine = null;
            switch (yielded)
            {
                case WaitForSeconds wait:
                    coroutine.waitKind = Coroutine.WaitKind.Seconds;
                    coroutine.waitUntil = Time.time + wait.seconds;
                    break;
                case WaitForSecondsRealtime wait:
                    coroutine.waitKind = Coroutine.WaitKind.RealtimeSeconds;
                    coroutine.waitUntil = Time.realtimeSinceStartup + wait.seconds;
                    break;
                case WaitForFrames wait:
                    coroutine.waitKind = Coroutine.WaitKind.Frames;
                    coroutine.waitFrame = Time.frameCount + Math.Max(wait.frames, 1);
                    break;
                case WaitUntil wait:
                    coroutine.waitKind = Coroutine.WaitKind.Until;
                    coroutine.waitCondition = wait.condition;
                    break;
                case WaitWhile wait:
                    coroutine.waitKind = Coroutine.WaitKind.While;
                    coroutine.waitCondition = wait.condition;
                    break;
                case Coroutine other:
                    coroutine.waitKind = Coroutine.WaitKind.Coroutine;
                    coroutine.waitCoroutine = other;
                    break;
                default:
                    // null, or anything else: the next frame.
                    coroutine.waitKind = Coroutine.WaitKind.NextFrame;
                    coroutine.waitFrame = Time.frameCount + 1;
                    break;
            }
        }

        private static void Report(Coroutine coroutine, Exception ex)
        {
            Log.Error($"Coroutine of {coroutine.owner.GetType().FullName}: {ex}");
        }
    }
}
