using System;

/// <summary>
/// A seedable pseudo-random number generator (xoshiro256+). It is a value type with no shared state: give each
/// thread or job its own instance, seeded from a base seed and the job's index, for sequences that repeat run
/// after run. The static <see cref="Random"/> uses one per thread.
/// </summary>
public struct RandomGenerator
{
    private const double INCR_DOUBLE = 1.0 / (1UL << 53);
    private const float INCR_FLOAT = 1f / (1U << 24);
    private const ulong NONZERO_STATE = 0x9E3779B97F4A7C15UL;

    private ulong s0;
    private ulong s1;
    private ulong s2;
    private ulong s3;

    /// <summary>
    /// Creates a generator seeded with <paramref name="seed"/>.
    /// </summary>
    /// <param name="seed">The seed; the same seed gives the same sequence.</param>
    public RandomGenerator(ulong seed)
    {
        s0 = SplitMix64(seed);
        s1 = SplitMix64(s0);
        s2 = SplitMix64(s1);
        s3 = SplitMix64(s2);
        if ((s0 | s1 | s2 | s3) == 0)
        {
            s0 = NONZERO_STATE;
        }
    }

    /// <summary>
    /// Creates a generator seeded from the current time mixed with entropy from the system.
    /// </summary>
    /// <returns>A generator with an unpredictable sequence.</returns>
    public static RandomGenerator CreateFromTime()
    {
        ulong mixed = (ulong)DateTime.UtcNow.Ticks ^ (ulong)System.Random.Shared.NextInt64();
        mixed ^= mixed >> 33;
        mixed *= 0xff51afd7ed558ccdUL;
        mixed ^= mixed >> 33;
        mixed *= 0xc4ceb9fe1a85ec53UL;
        mixed ^= mixed >> 33;
        return new RandomGenerator(mixed);
    }

    /// <summary>
    /// Returns a random 64-bit unsigned integer.
    /// </summary>
    /// <returns>A random 64-bit unsigned integer.</returns>
    public ulong NextUInt64()
    {
        ulong result = RotateLeft(s1 * 5, 7) * 9;
        ulong t = s1 << 17;
        s2 ^= s0;
        s3 ^= s1;
        s1 ^= s2;
        s0 ^= s3;
        s2 ^= t;
        s3 = RotateLeft(s3, 45);
        return result;
    }

    /// <summary>
    /// Returns a random float in the range [0, 1).
    /// </summary>
    /// <returns>A random float in [0, 1).</returns>
    public float NextFloat()
    {
        return (NextUInt64() >> 40) * INCR_FLOAT;
    }

    /// <summary>
    /// Returns a random double in the range [0, 1).
    /// </summary>
    /// <returns>A random double in [0, 1).</returns>
    public double NextDouble()
    {
        return (NextUInt64() >> 11) * INCR_DOUBLE;
    }

    /// <summary>
    /// Returns 1 or -1.
    /// </summary>
    /// <returns>1.0f or -1.0f.</returns>
    public float NextSign()
    {
        return NextUInt64() % 2 == 0 ? 1.0f : -1.0f;
    }

    /// <summary>
    /// Returns a random float in the range [minValue, maxValue).
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random float in the range.</returns>
    public float Range(float minValue, float maxValue)
    {
        return NextFloat() * (maxValue - minValue) + minValue;
    }

    /// <summary>
    /// Returns a random integer in the range [minValue, maxValue); minValue when the range is empty.
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random integer in the range.</returns>
    public int Range(int minValue, int maxValue)
    {
        if (maxValue <= minValue)
        {
            return minValue;
        }
        return (int)((NextUInt64() >> 33) % (ulong)((long)maxValue - minValue)) + minValue;
    }

    /// <summary>
    /// Returns a vector with each component in [0, 1).
    /// </summary>
    /// <returns>A random vector.</returns>
    public Vector3 NextVector3()
    {
        return new Vector3(NextFloat(), NextFloat(), NextFloat());
    }

    /// <summary>
    /// Returns a color with each of red, green and blue in [0, 1).
    /// </summary>
    /// <returns>A random color.</returns>
    public Color NextColor()
    {
        return new Color(Range(0f, 1f), Range(0f, 1f), Range(0f, 1f));
    }

    /// <summary>
    /// Returns a random point on the surface of a unit sphere.
    /// </summary>
    /// <returns>A unit vector.</returns>
    public Vector3 OnUnitSphere()
    {
        float theta = Range(0f, 2f * MathF.PI);
        float z = Range(-1f, 1f);
        float radius = MathF.Sqrt(1 - z * z);
        return new Vector3(radius * MathF.Cos(theta), radius * MathF.Sin(theta), z);
    }

    /// <summary>
    /// Returns a random point inside a unit sphere.
    /// </summary>
    /// <returns>A point within the unit sphere.</returns>
    public Vector3 InsideUnitSphere()
    {
        float theta = Range(0f, 2f * MathF.PI);
        float z = Range(-1f, 1f);
        // The cube root makes the points uniform over the volume.
        float r = MathF.Pow(NextFloat(), 1f / 3f);
        float radius = MathF.Sqrt(1 - z * z);
        return new Vector3(r * radius * MathF.Cos(theta), r * radius * MathF.Sin(theta), r * z);
    }

    /// <summary>
    /// Returns a random point inside a unit circle.
    /// </summary>
    /// <returns>A point within the unit circle.</returns>
    public Vector2 InsideUnitCircle()
    {
        float angle = Range(0f, 2f * MathF.PI);
        // The square root makes the points uniform over the area.
        float radius = MathF.Sqrt(NextFloat());
        return new Vector2(radius * MathF.Cos(angle), radius * MathF.Sin(angle));
    }

    private static ulong SplitMix64(ulong state)
    {
        state += 0x9E3779B97f4A7C15;
        state = (state ^ (state >> 30)) * 0xBF58476D1CE4E5B9;
        state = (state ^ (state >> 27)) * 0x94D049BB133111EB;
        return state ^ (state >> 31);
    }

    private static ulong RotateLeft(ulong x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }
}
