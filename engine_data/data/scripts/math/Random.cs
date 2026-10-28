using System;

/// <summary>
/// Pseudo-random number generator utilities for gameplay and sampling.
/// </summary>
/// <remarks>
/// Safe to call from any thread: every thread draws from its own <see cref="RandomGenerator"/>, seeded from the
/// time when the thread first uses it. <see cref="Seed"/> seeds the calling thread's generator only; for
/// sequences that repeat across threads, give each job its own <see cref="RandomGenerator"/>.
/// </remarks>
public static class Random
{
    [ThreadStatic]
    private static RandomGenerator t_generator;

    [ThreadStatic]
    private static bool t_isSeeded;

    private static ref RandomGenerator Generator
    {
        get
        {
            if (!t_isSeeded)
            {
                t_generator = RandomGenerator.CreateFromTime();
                t_isSeeded = true;
            }
            return ref t_generator;
        }
    }

    /// <summary>
    /// Re-seeds the calling thread's generator with a specific 64-bit seed.
    /// </summary>
    /// <param name="seed">The seed; the same seed gives the same sequence.</param>
    public static void Seed(ulong seed)
    {
        t_generator = new RandomGenerator(seed);
        t_isSeeded = true;
    }

    /// <summary>
    /// Seeds the calling thread's generator using the current system time mixed with OS-provided entropy.
    /// </summary>
    public static void SeedWithTime()
    {
        t_generator = RandomGenerator.CreateFromTime();
        t_isSeeded = true;
    }

    /// <summary>
    /// Returns a random 64-bit unsigned integer.
    /// </summary>
    /// <returns>A random 64-bit unsigned integer.</returns>
    public static ulong UInt64()
    {
        return Generator.NextUInt64();
    }

    /// <summary>
    /// Returns a random float value between 0.0 and 1.0.
    /// </summary>
    /// <returns>A random float value in the range [0.0, 1.0).</returns>
    public static float Float()
    {
        return Generator.NextFloat();
    }

    /// <summary>
    /// Returns a random Vector3 with components in the range [0.0, 1.0).
    /// </summary>
    /// <returns>A random Vector3.</returns>
    public static Vector3 Vec3()
    {
        return Generator.NextVector3();
    }

    /// <summary>
    /// Returns a random double value between 0.0 and 1.0.
    /// </summary>
    /// <returns>A random double value in the range [0.0, 1.0).</returns>
    public static double Double()
    {
        return Generator.NextDouble();
    }

    /// <summary>
    /// Returns a random sign value: either 1.0f or -1.0f.
    /// </summary>
    /// <returns>1.0f or -1.0f.</returns>
    public static float SignF()
    {
        return Generator.NextSign();
    }

    /// <summary>
    /// Returns a random float value between minValue (inclusive) and maxValue (exclusive).
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random float value in the range [minValue, maxValue).</returns>
    public static float Range(float minValue, float maxValue)
    {
        return Generator.Range(minValue, maxValue);
    }

    /// <summary>
    /// Returns a random integer value between minValue (inclusive) and maxValue (exclusive); minValue when the
    /// range is empty.
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random integer value in the range [minValue, maxValue).</returns>
    public static int Range(int minValue, int maxValue)
    {
        return Generator.Range(minValue, maxValue);
    }

    /// <summary>
    /// Returns a random color with RGB components in the range [0.0, 1.0).
    /// </summary>
    public static Color color => Generator.NextColor();

    /// <summary>
    /// Returns a random point on the surface of a unit sphere.
    /// </summary>
    public static Vector3 onUnitSphere => Generator.OnUnitSphere();

    /// <summary>
    /// Returns a random point inside a unit sphere.
    /// </summary>
    public static Vector3 insideUnitSphere => Generator.InsideUnitSphere();

    /// <summary>
    /// Returns a random point inside a unit circle.
    /// </summary>
    public static Vector2 insideUnitCircle => Generator.InsideUnitCircle();
}
