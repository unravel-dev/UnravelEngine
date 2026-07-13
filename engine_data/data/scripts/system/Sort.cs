using System;
using System.Collections.Generic;

// Pooled arrays are keyed by (and contain) script types, which would pin an
// unloading script domain. Fields are non-readonly, so the default cleanup
// (reset to null) applies; pools repopulate lazily on next use.
[Unravel.Core.AutoStaticsCleanup]
public static class StableSortExtensions
{
    // Thread-local pools to avoid allocations across frames
    [ThreadStatic]
    private static Dictionary<Type, object> keyArrayPool;
    
    [ThreadStatic]
    private static Dictionary<Type, object> elementArrayPool;
    
    /// <summary>
    /// Stable-sorts the list in place according to a key selector (like OrderBy, but in-place).
    /// Items with equal keys keep their original relative ordering.
    /// 
    /// This is an in-place mergesort variant. For large n, it's O(n log n).
    /// </summary>
    /// <param name="usePooling">If true, uses thread-local pooled arrays to minimize GC allocations. 
    /// Recommended for frequent sorting operations. Default is false for safety.</param>
    public static void StableSort<T, TKey>(
        this IList<T> list,
        Func<T, TKey> keySelector,
        IComparer<TKey> keyComparer = null,
        bool usePooling = false)
    {
        if (list == null) throw new ArgumentNullException(nameof(list));
        if (keySelector == null) throw new ArgumentNullException(nameof(keySelector));

        if (keyComparer == null)
            keyComparer = Comparer<TKey>.Default;

        // We store "keys" in a parallel array so we only compute keySelector once per element
        int count = list.Count;
        if (count <= 1) return; // Nothing to sort
        
        // Get or create key array (pooled or allocated)
        TKey[] keys;
        if (usePooling)
        {
            keys = GetOrCreateKeyArray<TKey>(count);
        }
        else
        {
            keys = new TKey[count];
        }
        
        for (int i = 0; i < count; i++)
        {
            keys[i] = keySelector(list[i]);
        }

        // mergesort
        MergesortInPlace(list, keys, 0, count, keyComparer, usePooling);
        
        // Clear pooled key array to avoid keeping references alive
        if (usePooling)
        {
            Array.Clear(keys, 0, count);
        }
    }
    
    private static TKey[] GetOrCreateKeyArray<TKey>(int minSize)
    {
        if (keyArrayPool == null)
            keyArrayPool = new Dictionary<Type, object>();
        
        var keyType = typeof(TKey);
        
        if (!keyArrayPool.TryGetValue(keyType, out object pooledObj) || pooledObj == null)
        {
            var newArray = new TKey[Math.Max(minSize, 64)]; // Start with reasonable size
            keyArrayPool[keyType] = newArray;
            return newArray;
        }
        
        var pooled = (TKey[])pooledObj;
        if (pooled.Length < minSize)
        {
            // Need to grow
            var newArray = new TKey[Math.Max(minSize, pooled.Length * 2)];
            keyArrayPool[keyType] = newArray;
            return newArray;
        }
        
        return pooled;
    }
    
    private static T[] GetOrCreateElementArray<T>(int minSize)
    {
        if (elementArrayPool == null)
            elementArrayPool = new Dictionary<Type, object>();
        
        var elemType = typeof(T);
        
        if (!elementArrayPool.TryGetValue(elemType, out object pooledObj) || pooledObj == null)
        {
            var newArray = new T[Math.Max(minSize, 64)]; // Start with reasonable size
            elementArrayPool[elemType] = newArray;
            return newArray;
        }
        
        var pooled = (T[])pooledObj;
        if (pooled.Length < minSize)
        {
            // Need to grow
            var newArray = new T[Math.Max(minSize, pooled.Length * 2)];
            elementArrayPool[elemType] = newArray;
            return newArray;
        }
        
        return pooled;
    }

    // Recursively split [start..end) into two halves, then merge them stably
    private static void MergesortInPlace<T, TKey>(
        IList<T> list,
        TKey[] keys,
        int start,
        int end,
        IComparer<TKey> comparer,
        bool usePooling)
    {
        int length = end - start;
        if (length <= 1) return;

        int mid = (start + end) / 2;
        MergesortInPlace(list, keys, start, mid, comparer, usePooling);
        MergesortInPlace(list, keys, mid, end, comparer, usePooling);

        // Merge two sorted halves: [start..mid), [mid..end)
        Merge(list, keys, start, mid, end, comparer, usePooling);
    }

    // Merge two sorted subranges in place, stably.
    private static void Merge<T, TKey>(
        IList<T> list,
        TKey[] keys,
        int start,
        int mid,
        int end,
        IComparer<TKey> comparer,
        bool usePooling)
    {
        int leftSize  = mid - start;
        int rightSize = end - mid;

        // We copy the left half into a temp array (pooled or allocated based on usePooling)
        // (the right half stays in place, and we merge them in 'list' itself)
        T[] leftElements;
        TKey[] leftKeys;
        
        if (usePooling)
        {
            leftElements = GetOrCreateElementArray<T>(leftSize);
            leftKeys = GetOrCreateKeyArray<TKey>(leftSize);
        }
        else
        {
            leftElements = new T[leftSize];
            leftKeys = new TKey[leftSize];
        }

        for (int i = 0; i < leftSize; i++)
        {
            leftElements[i] = list[start + i];
            leftKeys[i]     = keys[start + i];
        }

        // We'll merge them back into [start..end) of 'list'
        int leftIndex  = 0; // index into left half
        int rightIndex = mid; // index into 'list' for the right half
        int destIndex  = start;

        while (leftIndex < leftSize && rightIndex < end)
        {
            // Compare left vs. right
            int cmp = comparer.Compare(leftKeys[leftIndex], keys[rightIndex]);
            // If left <= right, we take left to maintain stability (i.e. we do <=, not <)
            if (cmp <= 0)
            {
                list[destIndex] = leftElements[leftIndex];
                keys[destIndex] = leftKeys[leftIndex];
                leftIndex++;
            }
            else
            {
                list[destIndex] = list[rightIndex];
                keys[destIndex] = keys[rightIndex];
                rightIndex++;
            }
            destIndex++;
        }

        // Copy any remaining left side
        while (leftIndex < leftSize)
        {
            list[destIndex] = leftElements[leftIndex];
            keys[destIndex] = leftKeys[leftIndex];
            leftIndex++;
            destIndex++;
        }

        // Right side is already in place if any remain, so no extra loop needed
        
        // Clear pooled arrays to avoid keeping references alive
        if (usePooling)
        {
            Array.Clear(leftElements, 0, leftSize);
            Array.Clear(leftKeys, 0, leftSize);
        }
    }
}
