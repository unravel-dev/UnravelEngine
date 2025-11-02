using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;

namespace Unravel.Core
{
    [StructLayout(LayoutKind.Sequential)]
    public struct UpdateInfo
    {
        public float time;
        public float deltaTime;
        public float timeScale;
        public long frameCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FixedUpdateInfo
    {
        public float deltaTime;
    }

    public static class Time
    {
        public static float time;
        public static float deltaTime;
        internal static float _timeScale = 1.0f;

        public static float fixedDeltaTime;

        public static long frameCount;

        /// <summary>
        /// Gets or sets the time scale for the application.
        /// </summary>
        public static float timeScale
        {
            get { return _timeScale; }
            set
            {
                _timeScale = value;
                internal_m2n_set_time_scale(value);
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_set_time_scale(float scale);
    }

    /// <summary>
    /// A manager for ScriptComponents with type-based priority.
    /// - Deferred additions if added during invocation (stored in pendingAdd).
    /// - Removals set comp=null if we are in the middle of invocation, 
    ///   or remove them directly if not in invocation.
    /// - After invocation, we remove any null comps and also insert any pendingAdd comps,
    ///   then we re-sort.
    /// </summary>
    public sealed class ScriptComponentManager
    {
        private bool isInvoking = false;

        // The main list.  (component, priority).
        private List<Entry> entries = new List<Entry>();

        // List of comps to add after the invocation finishes
        private List<PendingOp> pendingOps = new List<PendingOp>();

        // Type-based priorities
        private Dictionary<Type, int> typePriorities = new Dictionary<Type, int>();

        // Track components removed during invoke to avoid O(n²) LINQ queries
        private HashSet<ScriptComponent> removedDuringInvoke = new HashSet<ScriptComponent>();

        // Cached delegates to avoid lambda allocations every frame
        private static readonly Action<ScriptComponent> updateAction = c => c.OnUpdate();
        private static readonly Action<ScriptComponent> fixedUpdateAction = c => c.OnFixedUpdate();
        private static readonly Action<ScriptComponent> lateUpdateAction = c => c.OnLateUpdate();

        // Cached key selector for sorting to avoid lambda allocation
        private static readonly Func<Entry, int> prioritySelector = e => e.Priority;

        public ScriptComponentManager(Dictionary<Type, int> typePriorityMap = null)
        {
            if (typePriorityMap != null)
            {
                foreach (var kv in typePriorityMap)
                {
                    typePriorities[kv.Key] = kv.Value;
                }
            }
        }

        /// <summary>
        /// Add a ScriptComponent. 
        /// If we're currently invoking, we defer it into 'pendingOps'. 
        /// Otherwise, we insert it directly.
        /// </summary>
        public void Add(ScriptComponent comp)
        {
            if (ReferenceEquals(comp, null)) return;
            // if (comp == null) return;

            if (isInvoking)
            {
                // Collapse operations to avoid redundant add/remove chains
                CollapseOperations(comp, true);
            }
            else
            {
                // Insert immediately
                InsertComponent(comp);
                // Keep it sorted
                Resort();
            }
        }

        /// <summary>
        /// Remove a ScriptComponent. 
        /// If not currently invoking, remove them directly from 'entries'.
        /// If we are invoking, defer the operation and collapse with existing operations.
        /// </summary>
        public void Remove(ScriptComponent comp)
        {
            if (ReferenceEquals(comp, null)) return;
            // if (comp == null) return;

            if (isInvoking)
            {
                // Collapse operations to avoid redundant add/remove chains
                CollapseOperations(comp, false);
            }
            else
            {
                // Direct removal
                RemoveComponent(comp);
            }
        }

        /// <summary>
        /// Clears everything.
        /// </summary>
        public void Clear()
        {
            entries.Clear();
            pendingOps.Clear();
            removedDuringInvoke.Clear();
            isInvoking = false;
        }

        // If you want more update passes, you can replicate:
        public void InvokeUpdate() => InvokeInternal(updateAction);
        public void InvokeFixedUpdate() => InvokeInternal(fixedUpdateAction);
        public void InvokeLateUpdate() => InvokeInternal(lateUpdateAction);

        // The core logic for iteration in priority order, plus deferred add & remove cleanup
        private void InvokeInternal(Action<ScriptComponent> action)
        {
            isInvoking = true;
            removedDuringInvoke.Clear(); // Clear at start of invocation

            try
            {
                // Iterate in current sorted order
                foreach (var entry in entries)
                {
                    if (!ReferenceEquals(entry.Comp, null) && !removedDuringInvoke.Contains(entry.Comp))
                    // if (entry.Comp != null && !removedDuringInvoke.Contains(entry.Comp))
                    {
                        action(entry.Comp);
                    }
                }
            }
            finally
            {
                isInvoking = false;
            }

            // Insert any pending additions
            if (pendingOps.Count > 0)
            {
                foreach (var op in pendingOps)
                {
                    if (op.add)
                    {
                        InsertComponent(op.Comp);
                    }
                    else
                    {
                        RemoveComponent(op.Comp);
                    }
                }
                pendingOps.Clear();

                // Re-sort
                Resort();
            }
        }

        void Resort()
        {
            // Re-sort
            //entries.Sort((a, b) => a.Priority.CompareTo(b.Priority));

            // Use cached key selector and pooling to avoid allocations
            entries.StableSort(prioritySelector, null, usePooling: true);
        }

        /// <summary>
        /// Sets or updates the priority for a given type.
        /// If you do this after some have been inserted, 
        /// you may need to re-sort or re-insert them.
        /// </summary>
        public void SetTypePriority(Type t, int priority)
        {
            typePriorities[t] = priority;
            // optionally re-sort existing if you want them to reflect the new priority
            // but you'd have to recalc all. Typically you'd do that carefully.
        }

        // Actually inserts into 'entries' with the right priority
        private void InsertComponent(ScriptComponent comp)
        {
            int p = GetPriorityFor(comp);
            entries.Add(new Entry { Comp = comp, Priority = p });
        }
        private void RemoveComponent(ScriptComponent comp)
        {
            // Use manual loop instead of RemoveAll to avoid lambda allocation
            for (int i = entries.Count - 1; i >= 0; i--)
            {
                if (entries[i].Comp == comp)
                {
                    entries.RemoveAt(i);
                }
            }
        }

        // Retrieve or default a priority for this comp
        private int GetPriorityFor(ScriptComponent comp)
        {
            Type t = comp.GetType();
            if (typePriorities.TryGetValue(t, out int p))
                return p;
            return 100; // fallback
        }

        /// <summary>
        /// Collapses operations for a component to avoid redundant add/remove chains.
        /// </summary>
        /// <param name="comp">The component to process operations for.</param>
        /// <param name="isAdd">True if this is an add operation, false if remove.</param>
        private void CollapseOperations(ScriptComponent comp, bool isAdd)
        {
            // Find the last operation for this component
            int lastOpIndex = -1;
            for (int i = pendingOps.Count - 1; i >= 0; i--)
            {
                if (pendingOps[i].Comp == comp)
                {
                    lastOpIndex = i;
                    break;
                }
            }

            if (lastOpIndex == -1)
            {
                // No previous operations for this component, just add the new one
                pendingOps.Add(new PendingOp { add = isAdd, Comp = comp });

                // Track removal for fast lookup during invoke
                if (!isAdd)
                {
                    removedDuringInvoke.Add(comp);
                }
            }
            else
            {
                var lastOp = pendingOps[lastOpIndex];

                if (lastOp.add && !isAdd)
                {
                    // Last was add, this is remove -> cancel both operations
                    pendingOps.RemoveAt(lastOpIndex);
                    removedDuringInvoke.Add(comp);
                }
                else if (!lastOp.add && isAdd)
                {
                    // Last was remove, this is add -> cancel both operations
                    pendingOps.RemoveAt(lastOpIndex);
                    removedDuringInvoke.Remove(comp);
                }
                else if (lastOp.add && isAdd)
                {
                    // Last was add, this is add -> do nothing (already added)
                    // No need to add another add operation
                }
                else if (!lastOp.add && !isAdd)
                {
                    // Last was remove, this is remove -> do nothing (already removed)
                    // No need to add another remove operation
                }
            }
        }

        // Internal data
        private struct Entry
        {
            public ScriptComponent Comp;
            public int Priority;
        }

        private struct PendingOp
        {
            public bool add;
            public ScriptComponent Comp;
        }
    }


    public class GCMonitor
    {
        private int lastGen0Count;
        private int lastGen1Count;
        private int lastGen2Count;
        private long lastMemory;

        public GCMonitor()
        {
            Reset();
        }

        public void Reset()
        {
            lastGen0Count = GC.CollectionCount(0);
            lastGen1Count = GC.CollectionCount(1);
            lastGen2Count = GC.CollectionCount(2);
            lastMemory = GC.GetTotalMemory(false);
        }

        public void CheckAndLog(string context = "")
        {
            int gen0 = GC.CollectionCount(0);
            int gen1 = GC.CollectionCount(1);
            int gen2 = GC.CollectionCount(2);
            long memory = GC.GetTotalMemory(false);

            int gen0Delta = gen0 - lastGen0Count;
            int gen1Delta = gen1 - lastGen1Count;
            int gen2Delta = gen2 - lastGen2Count;
            long memDelta = memory - lastMemory;

            // Log every time (even if no change) for debugging
            if (gen0Delta > 0 || gen1Delta > 0 || gen2Delta > 0 || Math.Abs(memDelta) > 1024)
            {
                // Get Mono heap size (native memory used by Mono runtime)
                long monoHeap = internal_m2n_get_mono_heap_size();
                long monoUsed = internal_m2n_get_mono_used_size();

                Log.Info($"[GC] {context} - Collections: Gen0={gen0Delta}, Gen1={gen1Delta}, Gen2={gen2Delta} | Managed: {memDelta / 1024.0:F2} KB (Total: {memory / 1024.0:F2} KB) | Mono Heap: {monoHeap / (1024.0 * 1024.0):F2} MB (Used: {monoUsed / (1024.0 * 1024.0):F2} MB)");
            }

            lastGen0Count = gen0;
            lastGen1Count = gen1;
            lastGen2Count = gen2;
            lastMemory = memory;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long internal_m2n_get_mono_heap_size();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long internal_m2n_get_mono_used_size();
    }
    public static class SystemManager
    {
        public static ScriptComponentManager ScriptManager = new ScriptComponentManager();
        private static GCMonitor gcMonitor = new GCMonitor();
        public static void internal_n2m_update(UpdateInfo info)
        {
            Time.time = info.time;
            Time.deltaTime = info.deltaTime;
            Time._timeScale = info.timeScale;
            Time.frameCount = info.frameCount;

            // gcMonitor.Reset();
            ScriptManager.InvokeUpdate();
            // gcMonitor.CheckAndLog("Update");
        }


        public static void internal_n2m_fixed_update(FixedUpdateInfo info)
        {
            Time.fixedDeltaTime = info.deltaTime;

            ScriptManager.InvokeFixedUpdate();
        }

        public static void internal_n2m_late_update()
        {
            ScriptManager.InvokeLateUpdate();
        }
    }




    public static class ExceptionHelper
    {
        public static void ThrowException(string message)
        {
            throw new InvalidOperationException(message);
        }
    }

}


