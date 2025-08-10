using System;
using System.Collections.Generic;

public static class StableSortExtensions
{
    /// <summary>
    /// Stable-sorts the list in place according to a key selector (like OrderBy, but in-place).
    /// Items with equal keys keep their original relative ordering.
    /// 
    /// This is an in-place mergesort variant. For large n, it's O(n log n).
    /// </summary>
    public static void StableSort<T, TKey>(
        this IList<T> list,
        Func<T, TKey> keySelector,
        IComparer<TKey> keyComparer = null)
    {
        if (list == null) throw new ArgumentNullException(nameof(list));
        if (keySelector == null) throw new ArgumentNullException(nameof(keySelector));

        if (keyComparer == null)
            keyComparer = Comparer<TKey>.Default;

        // We'll do a mergesort with an auxiliary array
        // but we won't create a new array each time like .OrderBy() does
        // We'll do it in-place with partial copying.

        // We store "keys" in a parallel array so we only compute keySelector once per element
        int count = list.Count;
        var keys = new TKey[count];
        for (int i = 0; i < count; i++)
        {
            keys[i] = keySelector(list[i]);
        }

        // mergesort
        MergesortInPlace(list, keys, 0, count, keyComparer);
    }

    // Recursively split [start..end) into two halves, then merge them stably
    private static void MergesortInPlace<T, TKey>(
        IList<T> list,
        TKey[] keys,
        int start,
        int end,
        IComparer<TKey> comparer)
    {
        int length = end - start;
        if (length <= 1) return;

        int mid = (start + end) / 2;
        MergesortInPlace(list, keys, start, mid, comparer);
        MergesortInPlace(list, keys, mid, end, comparer);

        // Merge two sorted halves: [start..mid), [mid..end)
        Merge(list, keys, start, mid, end, comparer);
    }

    // Merge two sorted subranges in place, stably.
    private static void Merge<T, TKey>(
        IList<T> list,
        TKey[] keys,
        int start,
        int mid,
        int end,
        IComparer<TKey> comparer)
    {
        int leftSize  = mid - start;
        int rightSize = end - mid;

        // We copy the left half into a temp array
        // (the right half stays in place, and we merge them in 'list' itself)
        T[] leftElements = new T[leftSize];
        TKey[] leftKeys  = new TKey[leftSize];

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
    }
}
