using System;

namespace Unravel.Core
{
    /// <summary>
    /// Receives the items a <see cref="SpatialHashGrid"/> query finds. Implement it on a struct so the query
    /// is inlined and allocates nothing.
    /// </summary>
    public interface ISpatialHashVisitor
    {
        /// <summary>Called once per item within the query's radius, in no particular order.</summary>
        /// <param name="item">The item's index in the positions the grid was built from.</param>
        /// <param name="distance">Its distance from the query's centre on the ground plane.</param>
        void Visit(int item, float distance);
    }

    /// <summary>
    /// Finds items near a point on the ground plane (x, z) fast: a uniform grid of cells hashed into buckets,
    /// rebuilt from the items' positions when they move. Building is single-threaded; any number of threads may
    /// query a built grid at once (queries only read it), for example from Parallel.For.
    /// </summary>
    public sealed class SpatialHashGrid
    {
        private const uint CELL_HASH_X = 73856093u;
        private const uint CELL_HASH_Z = 19349663u;
        private const int MIN_BUCKETS = 64;
        private const float MIN_CELL_SIZE = 0.01f;

        private float currentCellSize;
        private float inverseCellSize;
        private int mask;
        private int[] bucketStarts = new int[MIN_BUCKETS + 1];
        private int[] items = Array.Empty<int>();
        private float[] itemX = Array.Empty<float>();
        private float[] itemZ = Array.Empty<float>();
        private int[] itemCellX = Array.Empty<int>();
        private int[] itemCellZ = Array.Empty<int>();
        private int[] itemBuckets = Array.Empty<int>();
        private int[] cursors = Array.Empty<int>();

        /// <summary>
        /// Creates an empty grid.
        /// </summary>
        /// <param name="cellSize">Side of a cell (m): about twice the usual query radius.</param>
        public SpatialHashGrid(float cellSize)
        {
            currentCellSize = MathF.Max(cellSize, MIN_CELL_SIZE);
            inverseCellSize = 1f / currentCellSize;
        }

        /// <summary>Side of a cell (m); changing it takes effect at the next build.</summary>
        public float cellSize
        {
            get => currentCellSize;
            set
            {
                currentCellSize = MathF.Max(value, MIN_CELL_SIZE);
                inverseCellSize = 1f / currentCellSize;
            }
        }

        /// <summary>How many items the last build holds.</summary>
        public int count { get; private set; }

        /// <summary>
        /// Rebuilds the grid from positions; queries then report indices into them. The grid keeps its own copy,
        /// so the positions may change after the build.
        /// </summary>
        /// <param name="positions">The items' positions; only x and z count.</param>
        /// <param name="include">Which items to hold; empty holds them all.</param>
        public void Build(ReadOnlySpan<Vector3> positions, ReadOnlySpan<bool> include = default)
        {
            if (!include.IsEmpty && include.Length != positions.Length)
            {
                throw new ArgumentException("include must be empty or as long as positions.", nameof(include));
            }
            int count = 0;
            for (int i = 0; i < positions.Length; i++)
            {
                if (include.IsEmpty || include[i])
                {
                    count++;
                }
            }
            int bucketCount = MIN_BUCKETS;
            while (bucketCount < count * 2)
            {
                bucketCount <<= 1;
            }
            mask = bucketCount - 1;
            if (bucketStarts.Length < bucketCount + 1)
            {
                bucketStarts = new int[bucketCount + 1];
            }
            Array.Clear(bucketStarts, 0, bucketCount + 1);
            EnsureCapacity(ref itemBuckets, positions.Length);
            for (int i = 0; i < positions.Length; i++)
            {
                if (!include.IsEmpty && !include[i])
                {
                    continue;
                }
                int bucket = Hash(GetCell(positions[i].x), GetCell(positions[i].z));
                itemBuckets[i] = bucket;
                bucketStarts[bucket + 1]++;
            }
            for (int b = 0; b < bucketCount; b++)
            {
                bucketStarts[b + 1] += bucketStarts[b];
            }
            EnsureCapacity(ref items, count);
            EnsureCapacity(ref itemX, count);
            EnsureCapacity(ref itemZ, count);
            EnsureCapacity(ref itemCellX, count);
            EnsureCapacity(ref itemCellZ, count);
            EnsureCapacity(ref cursors, bucketCount);
            Array.Copy(bucketStarts, cursors, bucketCount);
            for (int i = 0; i < positions.Length; i++)
            {
                if (!include.IsEmpty && !include[i])
                {
                    continue;
                }
                int slot = cursors[itemBuckets[i]]++;
                items[slot] = i;
                itemX[slot] = positions[i].x;
                itemZ[slot] = positions[i].z;
                itemCellX[slot] = GetCell(positions[i].x);
                itemCellZ[slot] = GetCell(positions[i].z);
            }
            this.count = count;
        }

        /// <summary>
        /// Calls <paramref name="visitor"/> for every item within <paramref name="radius"/> of
        /// <paramref name="center"/> on the ground plane, each once.
        /// </summary>
        /// <param name="center">The centre; only x and z count.</param>
        /// <param name="radius">The radius.</param>
        /// <param name="visitor">The visitor; a struct is passed by reference and keeps what it gathers.</param>
        public void Visit<TVisitor>(Vector3 center, float radius, ref TVisitor visitor) where TVisitor : struct, ISpatialHashVisitor
        {
            if (count == 0 || !(radius >= 0f))
            {
                return;
            }
            // Cells saturate at the int range for huge coordinates, so the loops count in long and never wrap.
            long minX = GetCell(center.x - radius);
            long maxX = GetCell(center.x + radius);
            long minZ = GetCell(center.z - radius);
            long maxZ = GetCell(center.z + radius);
            float radiusSquared = radius * radius;
            // A radius spanning more cells than there are items is cheaper to answer by testing every item.
            long width = maxX - minX + 1;
            long depth = maxZ - minZ + 1;
            if (width > count || depth > count || width * depth > count)
            {
                for (int slot = 0; slot < count; slot++)
                {
                    VisitIfWithin(slot, center, radiusSquared, ref visitor);
                }
                return;
            }
            for (long z = minZ; z <= maxZ; z++)
            {
                for (long x = minX; x <= maxX; x++)
                {
                    int bucket = Hash((int)x, (int)z);
                    int end = bucketStarts[bucket + 1];
                    for (int slot = bucketStarts[bucket]; slot < end; slot++)
                    {
                        // Other cells share the bucket: only this cell's items count, and each item once.
                        if (itemCellX[slot] == x && itemCellZ[slot] == z)
                        {
                            VisitIfWithin(slot, center, radiusSquared, ref visitor);
                        }
                    }
                }
            }
        }

        private void VisitIfWithin<TVisitor>(int slot, Vector3 center, float radiusSquared, ref TVisitor visitor) where TVisitor : struct, ISpatialHashVisitor
        {
            float dx = itemX[slot] - center.x;
            float dz = itemZ[slot] - center.z;
            float distanceSquared = dx * dx + dz * dz;
            if (distanceSquared <= radiusSquared)
            {
                visitor.Visit(items[slot], MathF.Sqrt(distanceSquared));
            }
        }

        /// <summary>
        /// Finds the items within <paramref name="radius"/> of <paramref name="center"/>, in no particular order.
        /// </summary>
        /// <param name="center">The centre; only x and z count.</param>
        /// <param name="radius">The radius.</param>
        /// <param name="results">Receives the items' indices.</param>
        /// <returns>How many were written, at most the buffer's length.</returns>
        public int Query(Vector3 center, float radius, Span<int> results)
        {
            var collector = new IndexCollector(results.Length);
            unsafe
            {
                fixed (int* buffer = results)
                {
                    collector.buffer = buffer;
                    Visit(center, radius, ref collector);
                }
            }
            return collector.count;
        }

        private unsafe struct IndexCollector : ISpatialHashVisitor
        {
            public int* buffer;
            public int count;
            private readonly int capacity;

            public IndexCollector(int capacity)
            {
                buffer = null;
                count = 0;
                this.capacity = capacity;
            }

            public void Visit(int item, float distance)
            {
                if (count < capacity)
                {
                    buffer[count++] = item;
                }
            }
        }

        private int GetCell(float coordinate)
        {
            return (int)MathF.Floor(coordinate * inverseCellSize);
        }

        private int Hash(int x, int z)
        {
            return (int)((((uint)x * CELL_HASH_X) ^ ((uint)z * CELL_HASH_Z)) & (uint)mask);
        }

        private static void EnsureCapacity<T>(ref T[] array, int count)
        {
            if (array.Length < count)
            {
                array = new T[Math.Max(count, array.Length * 2)];
            }
        }
    }
}
