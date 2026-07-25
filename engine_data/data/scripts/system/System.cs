using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;

namespace Unravel.Core
{
    /// <summary>
    /// Comparer that uses reference equality instead of overridden Equals/GetHashCode.
    /// This prevents issues when NativeObject's IsValid() state changes after adding to collections.
    /// </summary>
    internal sealed class ReferenceEqualityComparer : IEqualityComparer<ScriptComponent>
    {
        public static readonly ReferenceEqualityComparer Instance = new ReferenceEqualityComparer();

        private ReferenceEqualityComparer() { }

        public bool Equals(ScriptComponent x, ScriptComponent y)
        {
            return ReferenceEquals(x, y);
        }

        public int GetHashCode(ScriptComponent obj)
        {
            return RuntimeHelpers.GetHashCode(obj);
        }
    }

    /// <summary>
    /// Per-frame timing values pushed from native into managed code.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct UpdateInfo
    {
        /// <summary>Elapsed time in seconds since play began.</summary>
        public float time;
        /// <summary>Seconds since the previous update frame.</summary>
        public float deltaTime;
        /// <summary>Current time scale multiplier.</summary>
        public float timeScale;
        /// <summary>Number of update frames since play began.</summary>
        public long frameCount;
    }

    /// <summary>
    /// Fixed-step timing values pushed from native into managed code.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct FixedUpdateInfo
    {
        /// <summary>Seconds for the current fixed update step.</summary>
        public float deltaTime;
    }

    /// <summary>
    /// Provides global timing information for gameplay scripts.
    /// </summary>
    public static class Time
    {
        /// <summary>
        /// Elapsed time in seconds since play began.
        /// </summary>
        public static float time;

        /// <summary>
        /// Seconds since the previous update frame (scaled by <see cref="timeScale"/>).
        /// </summary>
        public static float deltaTime;

        internal static float _timeScale = 1.0f;

        /// <summary>
        /// Seconds for the current fixed update step.
        /// </summary>
        public static float fixedDeltaTime;

        /// <summary>
        /// Number of update frames since play began.
        /// </summary>
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
    /// Optimized manager for ScriptComponents with type-based priority.
    /// - Per-type buckets: Components of same type never sorted against each other
    /// - Slot reuse: Removed components leave gaps that are reused (no shifting)
    /// - O(1) removal: Dictionary lookup instead of linear search
    /// - Minimal allocations: Reuses collections across frames
    /// - Deferred operations during invocation for thread safety
    /// - Method override detection: Only adds components that actually override Update/FixedUpdate/LateUpdate
    /// </summary>
    public sealed class ScriptComponentManager
    {
        private bool isInvoking = false;

        // Per-type buckets, each with its own priority
        private readonly Dictionary<Type, TypeBucket> bucketsByType = new Dictionary<Type, TypeBucket>();
        private readonly List<TypeBucket> sortedBuckets = new List<TypeBucket>(); // Sorted by type priority for iteration
        
        // Fast component->bucket+index lookup for O(1) removal
        // Use ReferenceEqualityComparer to avoid issues with overridden Equals/GetHashCode
        private readonly Dictionary<ScriptComponent, ComponentLocation> locationByComponent = new Dictionary<ScriptComponent, ComponentLocation>(ReferenceEqualityComparer.Instance);
        
        // Pending operations during invocation
        private readonly List<PendingOp> pendingOps = new List<PendingOp>();
        
        // Fast lookup for pending operations by component (avoids linear search in CollapseOperations)
        private readonly Dictionary<ScriptComponent, int> pendingOpIndexByComponent = new Dictionary<ScriptComponent, int>(ReferenceEqualityComparer.Instance);
        
        // Track removals during invoke (reused, not recreated each frame)
        // Use ReferenceEqualityComparer to avoid issues with overridden Equals/GetHashCode
        private readonly HashSet<ScriptComponent> removedDuringInvoke = new HashSet<ScriptComponent>(ReferenceEqualityComparer.Instance);
        
        // Type priorities
        private readonly Dictionary<Type, int> typePriorities = new Dictionary<Type, int>();
        
        // Cache which methods each type overrides (checked once per type)
        private readonly Dictionary<Type, MethodOverrides> methodOverrideCache = new Dictionary<Type, MethodOverrides>();
        
        // Cached delegates to avoid lambda allocations every frame
        private static readonly Action<ScriptComponent> updateAction = c => c.OnUpdate();
        private static readonly Action<ScriptComponent> fixedUpdateAction = c => c.OnFixedUpdate();
        private static readonly Action<ScriptComponent> lateUpdateAction = c => c.OnLateUpdate();
        
        // Base ScriptComponent type for override detection
        private static readonly Type scriptComponentBaseType = typeof(ScriptComponent);

        /// <summary>
        /// Creates a manager, optionally seeding per-type update priorities.
        /// </summary>
        /// <param name="typePriorityMap">Optional map of script type to update priority.</param>
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
        /// Otherwise, we insert it directly into the appropriate type bucket.
        /// </summary>
        /// <param name="comp">Script component instance to register for updates.</param>
        public void Add(ScriptComponent comp)
        {
            if (ReferenceEquals(comp, null)) return;

            if (isInvoking)
            {
                // Collapse operations to avoid redundant add/remove chains
                CollapseOperations(comp, true);
            }
            else
            {
                // Insert immediately - no global sort needed
                InsertComponent(comp);
            }
        }

        /// <summary>
        /// Remove a ScriptComponent.
        /// Uses O(1) dictionary lookup instead of linear search.
        /// If we are invoking, defer the operation and collapse with existing operations.
        /// </summary>
        /// <param name="comp">Script component instance to unregister.</param>
        public void Remove(ScriptComponent comp)
        {
            if (ReferenceEquals(comp, null)) return;

            if (isInvoking)
            {
                // Collapse operations to avoid redundant add/remove chains
                CollapseOperations(comp, false);
            }
            else
            {
                // Direct removal - O(1) via dictionary lookup
                RemoveComponent(comp);
            }
        }

        /// <summary>
        /// Clears everything.
        /// </summary>
        public void Clear()
        {
            bucketsByType.Clear();
            sortedBuckets.Clear();
            locationByComponent.Clear();
            pendingOps.Clear();
            pendingOpIndexByComponent.Clear();
            removedDuringInvoke.Clear();
            isInvoking = false;
        }

        /// <summary>
        /// Invokes <see cref="ScriptComponent.OnUpdate"/> for registered components in priority order.
        /// </summary>
        public void InvokeUpdate() => InvokeInternal(updateAction);

        /// <summary>
        /// Invokes <see cref="ScriptComponent.OnFixedUpdate"/> for registered components in priority order.
        /// </summary>
        public void InvokeFixedUpdate() => InvokeInternal(fixedUpdateAction);

        /// <summary>
        /// Invokes <see cref="ScriptComponent.OnLateUpdate"/> for registered components in priority order.
        /// </summary>
        public void InvokeLateUpdate() => InvokeInternal(lateUpdateAction);

        // The core logic for iteration in priority order, plus deferred add & remove cleanup
        private void InvokeInternal(Action<ScriptComponent> action)
        {
            isInvoking = true;
            // Don't clear removedDuringInvoke - it's already empty or will be cleared at end

            // Determine which method we're calling
            bool isUpdate = ReferenceEquals(action, updateAction);
            bool isFixedUpdate = ReferenceEquals(action, fixedUpdateAction);
            bool isLateUpdate = ReferenceEquals(action, lateUpdateAction);

            try
            {
                // Iterate through type buckets in priority order
                for (int i = 0; i < sortedBuckets.Count; i++)
                {
                    var bucket = sortedBuckets[i];

                    // Skip bucket if it doesn't override the method we're calling
                    if (isUpdate && !bucket.overrides.hasUpdate) continue;
                    if (isFixedUpdate && !bucket.overrides.hasFixedUpdate) continue;
                    if (isLateUpdate && !bucket.overrides.hasLateUpdate) continue;

                    var slots = bucket.slots;

                    // Iterate through slots (including nulls/gaps from removals)
                    for (int j = 0; j < slots.Count; j++)
                    {
                        var comp = slots[j];
                        // Fast path: skip HashSet lookup if no components were removed during invoke
                        if (!ReferenceEquals(comp, null) && (removedDuringInvoke.Count == 0 || !removedDuringInvoke.Contains(comp)))
                        {
                            action(comp);
                        }
                    }
                }
            }
            finally
            {
                isInvoking = false;
            }

            // Process pending operations
            if (pendingOps.Count > 0)
            {
                for (int i = 0; i < pendingOps.Count; i++)
                {
                    var op = pendingOps[i];
                    // Skip cancelled operations (Comp is null when operations were collapsed)
                    if (ReferenceEquals(op.Comp, null)) continue;
                    
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
                pendingOpIndexByComponent.Clear();
            }

            // Clear removed tracking AFTER processing pending operations (reuse the HashSet)
            if (removedDuringInvoke.Count > 0)
            {
                removedDuringInvoke.Clear();
            }
        }

        /// <summary>
        /// Sets or updates the priority for a given type.
        /// If the type already has components, the bucket priority is updated and buckets are re-sorted.
        /// </summary>
        public void SetTypePriority(Type t, int priority)
        {
            bool needsResort = typePriorities.TryGetValue(t, out int oldPriority) && oldPriority != priority;
            typePriorities[t] = priority;

            if (needsResort && bucketsByType.TryGetValue(t, out var bucket))
            {
                bucket.priority = priority;
                ResortBuckets();
            }
        }

        // Inserts component into appropriate type bucket, only if it overrides at least one update method
        private void InsertComponent(ScriptComponent comp)
        {
            Type t = comp.GetType();
            
            // Check which methods this type overrides (cached per type)
            MethodOverrides overrides = GetMethodOverrides(t);
            
            // Skip components that don't override any update methods
            if (!overrides.hasUpdate && !overrides.hasFixedUpdate && !overrides.hasLateUpdate)
            {
                return;
            }

            int priority = GetPriorityFor(t); // Pass Type directly to avoid redundant GetType() call

            // Get or create bucket for this type
            if (!bucketsByType.TryGetValue(t, out var bucket))
            {
                bucket = new TypeBucket { type = t, priority = priority, overrides = overrides };
                bucketsByType[t] = bucket;
                
                // Insert bucket in sorted position (binary search)
                InsertBucketSorted(bucket);
            }

            // Try to reuse a free slot first (from previous removals)
            int slotIndex;
            if (bucket.freeSlots.Count > 0)
            {
                // Reuse last free slot (O(1) removal from end of list)
                slotIndex = bucket.freeSlots[bucket.freeSlots.Count - 1];
                bucket.freeSlots.RemoveAt(bucket.freeSlots.Count - 1);
                bucket.slots[slotIndex] = comp;
            }
            else
            {
                // No free slots, append new slot
                slotIndex = bucket.slots.Count;
                bucket.slots.Add(comp);
            }

            // Cache location for O(1) removal
            locationByComponent[comp] = new ComponentLocation { bucket = bucket, slotIndex = slotIndex };
        }

        // Gets or caches which methods a type overrides
        private MethodOverrides GetMethodOverrides(Type t)
        {
            if (methodOverrideCache.TryGetValue(t, out var cached))
            {
                return cached;
            }

            // Check if type overrides each method (check once per type, then cache)
            var overrides = new MethodOverrides
            {
                hasUpdate = IsMethodOverridden(t, "OnUpdate"),
                hasFixedUpdate = IsMethodOverridden(t, "OnFixedUpdate"),
                hasLateUpdate = IsMethodOverridden(t, "OnLateUpdate")
            };

            methodOverrideCache[t] = overrides;
            return overrides;
        }

        // Checks if a type overrides a specific method from ScriptComponent
        private static bool IsMethodOverridden(Type derivedType, string methodName)
        {
            var method = derivedType.GetMethod(methodName, 
                System.Reflection.BindingFlags.Public | 
                System.Reflection.BindingFlags.Instance | 
                System.Reflection.BindingFlags.DeclaredOnly);
            
            // If method is declared in derived type, it's overridden
            if (method != null && method.DeclaringType == derivedType)
            {
                return true;
            }

            // Check base types up to (but not including) ScriptComponent
            Type currentType = derivedType.BaseType;
            while (currentType != null && currentType != scriptComponentBaseType)
            {
                method = currentType.GetMethod(methodName,
                    System.Reflection.BindingFlags.Public |
                    System.Reflection.BindingFlags.Instance |
                    System.Reflection.BindingFlags.DeclaredOnly);

                if (method != null && method.DeclaringType == currentType)
                {
                    return true;
                }

                currentType = currentType.BaseType;
            }

            return false;
        }

        // O(1) removal using cached location lookup
        private void RemoveComponent(ScriptComponent comp)
        {
            // O(1) lookup instead of linear search
            if (!locationByComponent.TryGetValue(comp, out var location))
            {
                // Component not found - this can happen if:
                // 1. Component was never added (didn't override any methods)
                // 2. Component was already removed
                // 3. Component was added during invocation and immediately removed (operations collapsed)
                // In all cases, we should ensure it's not in removedDuringInvoke for next frame
                removedDuringInvoke.Remove(comp);
                return;
            }

            // Mark slot as free (null) instead of removing - no element shifting needed
            location.bucket.slots[location.slotIndex] = null;
            location.bucket.freeSlots.Add(location.slotIndex);
            
            locationByComponent.Remove(comp);

            // Compact bucket if it has too many gaps (tunable threshold)
            // This prevents unbounded memory growth while keeping removals fast
            if (location.bucket.freeSlots.Count > 16 && 
                location.bucket.freeSlots.Count > location.bucket.slots.Count / 2)
            {
                CompactBucket(location.bucket);
            }
        }

        // Compacts a bucket by removing null slots and rebuilding the free list
        private void CompactBucket(TypeBucket bucket)
        {
            int writeIndex = 0;
            for (int readIndex = 0; readIndex < bucket.slots.Count; readIndex++)
            {
                var comp = bucket.slots[readIndex];
                if (!ReferenceEquals(comp, null))
                {
                    if (writeIndex != readIndex)
                    {
                        bucket.slots[writeIndex] = comp;
                        // Update cached location - must write back to dictionary since ComponentLocation is a struct
                        locationByComponent[comp] = new ComponentLocation { bucket = bucket, slotIndex = writeIndex };
                    }
                    writeIndex++;
                }
            }

            // Trim excess slots
            if (writeIndex < bucket.slots.Count)
            {
                bucket.slots.RemoveRange(writeIndex, bucket.slots.Count - writeIndex);
            }

            bucket.freeSlots.Clear();
        }

        // Binary search insertion to keep buckets sorted by priority
        private void InsertBucketSorted(TypeBucket bucket)
        {
            int left = 0;
            int right = sortedBuckets.Count;

            while (left < right)
            {
                int mid = (left + right) / 2;
                if (sortedBuckets[mid].priority <= bucket.priority)
                {
                    left = mid + 1;
                }
                else
                {
                    right = mid;
                }
            }

            sortedBuckets.Insert(left, bucket);
        }

        // Re-sorts all buckets by priority (rare operation, only when SetTypePriority changes existing type)
        private void ResortBuckets()
        {
            sortedBuckets.Sort((a, b) => a.priority.CompareTo(b.priority));
        }

        // Retrieve or default a priority for a given type
        private int GetPriorityFor(Type t)
        {
            if (typePriorities.TryGetValue(t, out int p))
                return p;
            return 100; // fallback
        }

        /// <summary>
        /// Collapses operations for a component to avoid redundant add/remove chains.
        /// Uses O(1) dictionary lookup instead of O(n) linear search.
        /// </summary>
        /// <param name="comp">The component to process operations for.</param>
        /// <param name="isAdd">True if this is an add operation, false if remove.</param>
        private void CollapseOperations(ScriptComponent comp, bool isAdd)
        {
            // O(1) lookup for last operation index
            if (!pendingOpIndexByComponent.TryGetValue(comp, out int lastOpIndex))
            {
                // No previous operations for this component, just add the new one
                int newIndex = pendingOps.Count;
                pendingOps.Add(new PendingOp { add = isAdd, Comp = comp });
                pendingOpIndexByComponent[comp] = newIndex;

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
                    // Mark the operation as cancelled (set Comp to null) instead of removing from list
                    // to avoid invalidating indices in pendingOpIndexByComponent
                    pendingOps[lastOpIndex] = new PendingOp { add = false, Comp = null };
                    pendingOpIndexByComponent.Remove(comp);
                    removedDuringInvoke.Add(comp);
                }
                else if (!lastOp.add && isAdd)
                {
                    // Last was remove, this is add -> cancel both operations
                    pendingOps[lastOpIndex] = new PendingOp { add = false, Comp = null };
                    pendingOpIndexByComponent.Remove(comp);
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

        // Internal data structures
        
        /// <summary>
        /// Bucket containing all components of a single type.
        /// Maintains slots with gaps (nulls) for efficient removal and reuse.
        /// </summary>
        private class TypeBucket
        {
            public Type type;
            public int priority;
            public MethodOverrides overrides; // Which methods this type overrides
            public List<ScriptComponent> slots = new List<ScriptComponent>(); // Contains nulls for removed components
            public List<int> freeSlots = new List<int>(); // Indices of null slots that can be reused
        }

        /// <summary>
        /// Cached location of a component for O(1) removal.
        /// Using struct to avoid heap allocations.
        /// </summary>
        private struct ComponentLocation
        {
            public TypeBucket bucket;
            public int slotIndex;
        }

        /// <summary>
        /// Cached information about which methods a type overrides.
        /// </summary>
        private struct MethodOverrides
        {
            public bool hasUpdate;
            public bool hasFixedUpdate;
            public bool hasLateUpdate;
        }

        private struct PendingOp
        {
            public bool add;
            public ScriptComponent Comp;
        }
    }


    /// <summary>
    /// Tracks managed GC activity and optionally logs collection/memory deltas.
    /// </summary>
    public class GCMonitor
    {
        private int lastGen0Count;
        private int lastGen1Count;
        private int lastGen2Count;
        private long lastMemory;

        /// <summary>
        /// Creates a monitor and captures the current GC baseline.
        /// </summary>
        public GCMonitor()
        {
            Reset();
        }

        /// <summary>
        /// Resets the baseline collection counts and managed memory size.
        /// </summary>
        public void Reset()
        {
            lastGen0Count = GC.CollectionCount(0);
            lastGen1Count = GC.CollectionCount(1);
            lastGen2Count = GC.CollectionCount(2);
            lastMemory = GC.GetTotalMemory(false);
        }

        /// <summary>
        /// Compares current GC stats to the baseline and logs notable changes.
        /// </summary>
        /// <param name="context">Optional label included in the log message.</param>
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
                long monoHeap = internal_m2n_get_dotnet_heap_size();
                long monoUsed = internal_m2n_get_dotnet_used_size();

                Log.Info($"[GC] {context} - Collections: Gen0={gen0Delta}, Gen1={gen1Delta}, Gen2={gen2Delta} | Managed: {memDelta / 1024.0:F2} KB (Total: {memory / 1024.0:F2} KB) | Mono Heap: {monoHeap / (1024.0 * 1024.0):F2} MB (Used: {monoUsed / (1024.0 * 1024.0):F2} MB)");
            }

            lastGen0Count = gen0;
            lastGen1Count = gen1;
            lastGen2Count = gen2;
            lastMemory = memory;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long internal_m2n_get_dotnet_heap_size();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern long internal_m2n_get_dotnet_used_size();
    }
    /// <summary>
    /// Entry point for native-to-managed frame updates and script component dispatch.
    /// </summary>
    [AutoStaticsCleanup]
    public static class SystemManager
    {
        /// <summary>
        /// Global manager that invokes script component update callbacks.
        /// </summary>
        public static ScriptComponentManager ScriptManager = new ScriptComponentManager();
        private static GCMonitor gcMonitor = new GCMonitor();

        /// <summary>
        /// Invoked by the runtime before a script domain unloads. Re-creates
        /// the manager so no script instances, Type buckets or method
        /// override caches keep the unloading domain alive, while native
        /// update callbacks keep working against a fresh, empty manager.
        /// </summary>
        private static void OnStaticsCleanup()
        {
            ScriptManager = new ScriptComponentManager();
            gcMonitor = new GCMonitor();
        }
        /// <summary>
        /// Native-to-managed frame update entry point. Updates <see cref="Time"/> and dispatches OnUpdate.
        /// </summary>
        /// <param name="info">Per-frame timing values from the engine.</param>
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

        /// <summary>
        /// Native-to-managed fixed update entry point. Updates fixed delta time and dispatches OnFixedUpdate.
        /// </summary>
        /// <param name="info">Fixed-step timing values from the engine.</param>
        public static void internal_n2m_fixed_update(FixedUpdateInfo info)
        {
            Time.fixedDeltaTime = info.deltaTime;

            ScriptManager.InvokeFixedUpdate();
        }

        /// <summary>
        /// Native-to-managed late update entry point. Dispatches OnLateUpdate.
        /// </summary>
        public static void internal_n2m_late_update()
        {
            ScriptManager.InvokeLateUpdate();
        }
    }




    /// <summary>
    /// Helpers for throwing managed exceptions from native bridge code.
    /// </summary>
    public static class ExceptionHelper
    {
        /// <summary>
        /// Throws an <see cref="InvalidOperationException"/> with the given message.
        /// </summary>
        /// <param name="message">Exception message.</param>
        public static void ThrowException(string message)
        {
            throw new InvalidOperationException(message);
        }
    }

}



