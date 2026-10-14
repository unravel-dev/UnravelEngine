using System;
using System.Runtime.InteropServices;

/// <summary>
/// One key of a <see cref="Gradient"/>: a color at a time.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct GradientKey
{
    /// <summary>
    /// Position of the key, usually 0 to 1.
    /// </summary>
    public float time;

    /// <summary>
    /// Color at the key (a picker, sRGB, value; alpha included).
    /// </summary>
    public Color color;

    /// <summary>
    /// Creates a key.
    /// </summary>
    public GradientKey(float time, Color color)
    {
        this.time = time;
        this.color = color;
    }
}

/// <summary>
/// Colors keyed over time and interpolated linearly between the keys.
/// </summary>
public sealed class Gradient
{
    /// <summary>
    /// The keys, ordered by time.
    /// </summary>
    public GradientKey[] keys;

    /// <summary>
    /// Creates a gradient from its keys.
    /// </summary>
    public Gradient(params GradientKey[] keys)
    {
        this.keys = keys ?? Array.Empty<GradientKey>();
    }

    /// <summary>
    /// The color at @p time; times outside the keys clamp to the first or last key.
    /// </summary>
    public Color Evaluate(float time)
    {
        return CurveSampling.Evaluate(keys, time, k => k.time, k => k.color, Color.Lerp, Color.white);
    }
}

/// <summary>
/// One key of a <see cref="Curve"/>: a value at a time.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct CurveKey
{
    /// <summary>
    /// Position of the key, usually 0 to 1.
    /// </summary>
    public float time;

    /// <summary>
    /// Value at the key.
    /// </summary>
    public float value;

    /// <summary>
    /// Creates a key.
    /// </summary>
    public CurveKey(float time, float value)
    {
        this.time = time;
        this.value = value;
    }
}

/// <summary>
/// Values keyed over time and interpolated linearly between the keys.
/// </summary>
public sealed class Curve
{
    /// <summary>
    /// The keys, ordered by time.
    /// </summary>
    public CurveKey[] keys;

    /// <summary>
    /// Creates a curve from its keys.
    /// </summary>
    public Curve(params CurveKey[] keys)
    {
        this.keys = keys ?? Array.Empty<CurveKey>();
    }

    /// <summary>
    /// The value at @p time; times outside the keys clamp to the first or last key.
    /// </summary>
    public float Evaluate(float time)
    {
        return CurveSampling.Evaluate(keys, time, k => k.time, k => k.value, Mathf.Lerp, 0f);
    }
}

/// <summary>
/// One key of a <see cref="MinMaxCurve"/>: a range at a time.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct MinMaxKey
{
    /// <summary>
    /// Position of the key, usually 0 to 1.
    /// </summary>
    public float time;

    /// <summary>
    /// Lower end of the range at the key.
    /// </summary>
    public float min;

    /// <summary>
    /// Upper end of the range at the key.
    /// </summary>
    public float max;

    /// <summary>
    /// Creates a key.
    /// </summary>
    public MinMaxKey(float time, float min, float max)
    {
        this.time = time;
        this.min = min;
        this.max = max;
    }
}

/// <summary>
/// A range keyed over time ("random between two curves"): each use picks a value between the
/// min and max curves.
/// </summary>
public sealed class MinMaxCurve
{
    /// <summary>
    /// The keys, ordered by time.
    /// </summary>
    public MinMaxKey[] keys;

    /// <summary>
    /// Creates a curve from its keys.
    /// </summary>
    public MinMaxCurve(params MinMaxKey[] keys)
    {
        this.keys = keys ?? Array.Empty<MinMaxKey>();
    }

    /// <summary>
    /// A curve with the same range at the start and the end.
    /// </summary>
    public MinMaxCurve(float min, float max)
        : this(new MinMaxKey(0f, min, max), new MinMaxKey(1f, min, max))
    {
    }

    /// <summary>
    /// The range at @p time; times outside the keys clamp to the first or last key.
    /// </summary>
    public Range<float> Evaluate(float time)
    {
        return CurveSampling.Evaluate(keys,
                                      time,
                                      k => k.time,
                                      k => new Range<float>(k.min, k.max),
                                      (a, b, t) => new Range<float>(Mathf.Lerp(a.Min, b.Min, t), Mathf.Lerp(a.Max, b.Max, t)),
                                      new Range<float>(0f, 0f));
    }
}

/// <summary>
/// One key of a <see cref="Vector3Curve"/>: a vector at a time.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vector3Key
{
    /// <summary>
    /// Position of the key, usually 0 to 1.
    /// </summary>
    public float time;

    /// <summary>
    /// Value at the key.
    /// </summary>
    public Vector3 value;

    /// <summary>
    /// Creates a key.
    /// </summary>
    public Vector3Key(float time, Vector3 value)
    {
        this.time = time;
        this.value = value;
    }
}

/// <summary>
/// Vectors keyed over time and interpolated linearly between the keys.
/// </summary>
public sealed class Vector3Curve
{
    /// <summary>
    /// The keys, ordered by time.
    /// </summary>
    public Vector3Key[] keys;

    /// <summary>
    /// Creates a curve from its keys.
    /// </summary>
    public Vector3Curve(params Vector3Key[] keys)
    {
        this.keys = keys ?? Array.Empty<Vector3Key>();
    }

    /// <summary>
    /// The vector at @p time; times outside the keys clamp to the first or last key.
    /// </summary>
    public Vector3 Evaluate(float time)
    {
        return CurveSampling.Evaluate(keys, time, k => k.time, k => k.value, Vector3.Lerp, Vector3.zero);
    }
}

/// <summary>
/// Linear sampling shared by the keyed curve types.
/// </summary>
internal static class CurveSampling
{
    internal static TValue Evaluate<TKey, TValue>(TKey[] keys,
                                                  float time,
                                                  Func<TKey, float> timeOf,
                                                  Func<TKey, TValue> valueOf,
                                                  Func<TValue, TValue, float, TValue> lerp,
                                                  TValue empty)
    {
        if (keys == null || keys.Length == 0)
        {
            return empty;
        }
        if (time <= timeOf(keys[0]))
        {
            return valueOf(keys[0]);
        }
        for (int i = 1; i < keys.Length; i++)
        {
            float next = timeOf(keys[i]);
            if (time <= next)
            {
                float previous = timeOf(keys[i - 1]);
                float span = next - previous;
                float t = span > 0f ? (time - previous) / span : 1f;
                return lerp(valueOf(keys[i - 1]), valueOf(keys[i]), t);
            }
        }
        return valueOf(keys[keys.Length - 1]);
    }
}
