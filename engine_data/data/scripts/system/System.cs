using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Linq;

namespace Ace
{
namespace Core
{
[StructLayout(LayoutKind.Sequential)]
public struct UpdateInfo
{
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
    public static float deltaTime;
    public static float timeScale;

    public static float fixedDeltaTime;

    public static long frameCount;
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
    // If we marked anything null during invocation, set this so we know to cleanup after.
    private bool anyNulls = false;  

    // The main list.  (component, priority).
    private List<Entry> entries = new List<Entry>();

    // List of comps to add after the invocation finishes
    private List<ScriptComponent> pendingAdd = new List<ScriptComponent>();

    // Type-based priorities
    private Dictionary<Type, int> typePriorities = new Dictionary<Type,int>();

    public ScriptComponentManager(Dictionary<Type,int> typePriorityMap = null)
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
    /// If we’re currently invoking, we defer it into 'pendingAdd'. 
    /// Otherwise, we insert it directly.
    /// </summary>
    public void Add(ScriptComponent comp)
    {
        if (comp == null) return;
        if (isInvoking)
        {
            // Defer until after invocation
            pendingAdd.Add(comp);
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
    /// If we are invoking, mark all matching comps as null and set 'anyNulls=true'.
    /// </summary>
    public void Remove(ScriptComponent comp)
    {
        if (comp == null) return;

        if (isInvoking)
        {
            // Mark them null, skip physically removing
            MarkAllNull(comp);
            anyNulls = true;
        }
        else
        {
            // Direct removal
            entries.RemoveAll(e => e.Comp == comp);
        }
    }

    /// <summary>
    /// Clears everything.
    /// </summary>
    public void Clear()
    {
        entries.Clear();
        pendingAdd.Clear();
        isInvoking = false;
        anyNulls = false;
    }

    // If you want more update passes, you can replicate:
    public void InvokeUpdate() => InvokeInternal(c => c.OnUpdate());
    public void InvokeFixedUpdate() => InvokeInternal(c => c.OnFixedUpdate());
    public void InvokeLateUpdate() => InvokeInternal(c => c.OnLateUpdate());

    // The core logic for iteration in priority order, plus deferred add & remove cleanup
    private void InvokeInternal(Action<ScriptComponent> action)
    {
        isInvoking = true;
        try
        {
            // Iterate in current sorted order
            foreach (var entry in entries)
            {
                if (entry.Comp != null)
                {
                    action(entry.Comp);
                }
            }
        }
        finally
        {
            isInvoking = false;
        }

        // If we marked anything null, remove them
        if (anyNulls)
        {
            entries.RemoveAll(e => e.Comp == null);
            anyNulls = false;
        }

        // Insert any pending additions
        if (pendingAdd.Count > 0)
        {
            foreach (var comp in pendingAdd)
            {
                InsertComponent(comp);
            }
            pendingAdd.Clear();

            // Re-sort
            Resort();
        }
    }

    void Resort()
    {
        // Re-sort
        //entries.Sort((a, b) => a.Priority.CompareTo(b.Priority));

        // Use your stable mergesort extension:
        entries.StableSort(e => e.Priority);
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

    // Mark every matching 'comp' as null in 'entries'
    // So we skip them in the iteration
    private void MarkAllNull(ScriptComponent comp)
    {
        for (int i = 0; i < entries.Count; i++)
        {
            if (entries[i].Comp == comp)
            {
                entries[i] = new Entry { Comp = null, Priority = entries[i].Priority };
                // if you want to remove duplicates, keep going; 
                // if you assume unique, break after first
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

    // Internal data
    private struct Entry
    {
        public ScriptComponent Comp;
        public int Priority;
    }

    private struct PendingAdd
    {
        public ScriptComponent Comp;
    }
}



public static class SystemManager
{
    public static ScriptComponentManager ScriptManager = new ScriptComponentManager();
    public static void internal_n2m_update(UpdateInfo info)
    {
        Time.deltaTime = info.deltaTime;
        Time.timeScale = info.timeScale;
        Time.frameCount = info.frameCount;

        ScriptManager.InvokeUpdate();

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

}


