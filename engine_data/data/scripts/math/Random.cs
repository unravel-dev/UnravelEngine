using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

public static class Random
{
    private static ulong[] s_State;
    private static ulong s_Seed;
    private const double INCR_DOUBLE = 1.0 / (1UL << 53);
    private const float INCR_FLOAT = 1f / (1U << 24);

    // Initialize automatically with a time-based seed
    static Random()
    {
        SeedWithTime();
    }

    /// <summary>
    /// Re-seeds the generator with a specific 64-bit seed.
    /// </summary>
    public static void Seed(ulong seed)
    {
        s_Seed = seed;
        s_State = xorshift256_init(seed);

        // Ensure non-zero internal state
        if (s_State[0] == 0 && s_State[1] == 0 && s_State[2] == 0 && s_State[3] == 0)
            s_State[0] = 0x9E3779B97F4A7C15UL;
    }

    /// <summary>
    /// Seeds the generator using the current system time (ticks + process ID).
    /// </summary>
    public static void SeedWithTime()
    {
        ulong timePart = (ulong)DateTime.UtcNow.Ticks;
        ulong pidPart  = (ulong)System.Diagnostics.Process.GetCurrentProcess().Id;
        ulong mixed    = timePart ^ (pidPart << 32);

        // Mix it a bit more to avoid low-entropy time bits
        mixed ^= (mixed >> 33);
        mixed *= 0xff51afd7ed558ccdUL;
        mixed ^= (mixed >> 33);
        mixed *= 0xc4ceb9fe1a85ec53UL;
        mixed ^= (mixed >> 33);

        Seed(mixed);
    }

    private static ulong splitmix64(ulong state)
    {
        state += 0x9E3779B97f4A7C15;
        state = (state ^ (state >> 30)) * 0xBF58476D1CE4E5B9;
        state = (state ^ (state >> 27)) * 0x94D049BB133111EB;
        return state ^ (state >> 31);
    }

    private static ulong[] xorshift256_init(ulong seed)
    {
        ulong[] result = new ulong[4];
        result[0] = splitmix64(seed);
        result[1] = splitmix64(result[0]);
        result[2] = splitmix64(result[2]);
        result[3] = splitmix64(result[3]);
        return result;
    }

    private static ulong rol64(ulong x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    private static ulong xoshiro256p()
    {
        ulong[] state = s_State;

        ulong result = rol64(state[1] * 5, 7) * 9;
        ulong t = state[1] << 17;

        state[2] ^= state[0];
        state[3] ^= state[1];
        state[1] ^= state[2];
        state[0] ^= state[3];

        state[2] ^= t;
        state[3] = rol64(state[3], 45);

        return result;
    }

    /// <summary>
    /// Returns a random 64-bit unsigned integer.
    /// </summary>
    /// <returns>A random 64-bit unsigned integer.</returns>
    public static ulong UInt64()
    {
        return xoshiro256p();
    }

    /// <summary>
    /// Returns a random float value between 0.0 and 1.0.
    /// </summary>
    /// <returns>A random float value in the range [0.0, 1.0).</returns>
    public static float Float()
    {
        return (UInt64() >> 40) * INCR_FLOAT;
    }

    /// <summary>
    /// Returns a random Vector3 with components in the range [0.0, 1.0).
    /// </summary>
    /// <returns>A random Vector3.</returns>
	public static Vector3 Vec3()
	{
		return new Vector3(Float(), Float(), Float());
	}

    /// <summary>
    /// Returns a random double value between 0.0 and 1.0.
    /// </summary>
    /// <returns>A random double value in the range [0.0, 1.0).</returns>
    public static double Double()
    {
        return (UInt64() >> 11) * INCR_DOUBLE;
    }

    /// <summary>
    /// Returns a random sign value: either 1.0f or -1.0f.
    /// </summary>
    /// <returns>1.0f or -1.0f.</returns>
	public static float SignF()
	{
		return UInt64() % 2 == 0 ? 1.0f : -1.0f;
	}

    /// <summary>
    /// Returns a random float value between minValue (inclusive) and maxValue (exclusive).
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random float value in the range [minValue, maxValue).</returns>
	public static float Range(float minValue, float maxValue)
	{
		return Float() * (maxValue - minValue) + minValue;
	}

    /// <summary>
    /// Returns a random integer value between minValue (inclusive) and maxValue (exclusive).
    /// </summary>
    /// <param name="minValue">The minimum value (inclusive).</param>
    /// <param name="maxValue">The maximum value (exclusive).</param>
    /// <returns>A random integer value in the range [minValue, maxValue).</returns>
	public static int Range(int minValue, int maxValue)
    {
        return ((int)(UInt64()>>33) % (maxValue - minValue)) + minValue;
        // TODO: Make this better
        /*long range = (long)maxValue - minValue;
        if (range <= int.MaxValue)
            return NextInner((int)range) + minValue;

        // Call NextInner(long); i.e. the range is greater than int.MaxValue.
        return (int)(NextInner(range) + minValue);*/
    }

    /// <summary>
    /// Returns a random color with RGB components in the range [0.0, 1.0).
    /// </summary>
    public static Color color
    {
        get
        {
            return new Color(Range(0f, 1f), Range(0f, 1f), Range(0f, 1f));
        }
        
    }

    /// <summary>
    /// Returns a random point on the surface of a unit sphere.
    /// </summary>
    public static Vector3 onUnitSphere
    {
        get
        {
            // Generate random values for spherical coordinates
            float theta = Range(0f, 2f * (float)Math.PI); // Random angle in [0, 2π]
            float z = Range(-1f, 1f); // Random z-coordinate in [-1, 1] (cos(phi))

            // Calculate x and y based on the spherical coordinates
            float radius = (float)Math.Sqrt(1 - z * z); // Radius for the circle at this z
            float x = radius * (float)Math.Cos(theta);
            float y = radius * (float)Math.Sin(theta);

            return new Vector3(x, y, z);
        }
        
    }

    /// <summary>
    /// Returns a random point inside a unit sphere.
    /// </summary>
    public static Vector3 insideUnitSphere
    {
        get
        {
            // Generate random spherical coordinates
            float theta = Range(0f, 2f * (float)Math.PI); // Random azimuth angle
            float z = Range(-1f, 1f);                    // Random z-coordinate
            float r = (float)Math.Pow(Float(), 1f / 3f); // Random radius (cube root for uniformity)

            // Calculate x and y based on spherical coordinates
            float radius = (float)Math.Sqrt(1 - z * z);
            float x = radius * (float)Math.Cos(theta);
            float y = radius * (float)Math.Sin(theta);

            // Scale by random radius
            return new Vector3(r * x, r * y, r * z);
        }
        
    }

    /// <summary>
    /// Returns a random point inside a unit circle.
    /// </summary>
    public static Vector2 insideUnitCircle
    {
        get
        {
            // Generate a random angle in [0, 2π]
            float angle = Range(0f, 2f * (float)Math.PI);

            // Generate a random radius with uniform distribution (sqrt for area uniformity)
            float radius = (float)Math.Sqrt(Float());

            // Convert polar coordinates to Cartesian coordinates
            float x = radius * (float)Math.Cos(angle);
            float y = radius * (float)Math.Sin(angle);

            return new Vector2(x, y);
        }
        
    }
}
