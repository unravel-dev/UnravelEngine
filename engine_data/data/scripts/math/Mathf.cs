using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

/// <summary>
/// Internal helpers used by <see cref="Mathf"/>.
/// </summary>
[StructLayout(LayoutKind.Sequential, Size = 1)]
public struct MathfInternal
{
    public static volatile float FloatMinNormal = 1.17549435E-38f;

    public static volatile float FloatMinDenormal = float.Epsilon;

    public static bool IsFlushToZeroEnabled = FloatMinDenormal == 0f;
}

/// <summary>
/// A collection of common math functions.
/// </summary>
[StructLayout(LayoutKind.Sequential, Size = 1)]
public struct Mathf
{
    /// <summary>
    /// The well-known 3.14159265358979... value (Read Only).
    /// </summary>
    public const float PI = MathF.PI;

    /// <summary>
    /// A representation of positive infinity (Read Only).
    /// </summary>
    public const float Infinity = float.PositiveInfinity;

    /// <summary>
    /// A representation of negative infinity (Read Only).
    /// </summary>
    public const float NegativeInfinity = float.NegativeInfinity;

    /// <summary>
    /// Degrees-to-radians conversion constant (Read Only).
    /// </summary>
    public const float Deg2Rad = MathF.PI / 180f;

    /// <summary>
    /// Radians-to-degrees conversion constant (Read Only).
    /// </summary>
    public const float Rad2Deg = 57.29578f;

    internal const int kMaxDecimals = 15;


    /// <summary>
    /// A small epsilon value used for floating point comparisons.
    /// </summary>
    public const float kEpsilon = 1E-05f;

    /// <summary>
    /// A small epsilon value used for normal vector comparisons.
    /// </summary>
    public const float kEpsilonNormalSqrt = 1E-15f;
    /// <summary>
    /// A tiny floating point value (Read Only).
    /// </summary>
    public static readonly float Epsilon = (MathfInternal.IsFlushToZeroEnabled ? MathfInternal.FloatMinNormal : MathfInternal.FloatMinDenormal);

    /// <summary>
    /// Returns the closest power of two value.
    /// </summary>
    /// <param name="value">The input value.</param>
    /// <returns>The closest power of two.</returns>
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int ClosestPowerOfTwo(int value);

    /// <summary>
    /// Returns true if the value is power of two.
    /// </summary>
    /// <param name="value">The input value.</param>
    /// <returns>True if the value is a power of two; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool IsPowerOfTwo(int value);

    /// <summary>
    /// Returns the next power of two that is equal to, or greater than, the argument.
    /// </summary>
    /// <param name="value">The input value.</param>
    /// <returns>The next power of two.</returns>
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int NextPowerOfTwo(int value);

    /// <summary>
    /// Returns the sine of angle f.
    /// </summary>
    /// <param name="f">The input angle, in radians.</param>
    /// <returns>The return value between -1 and +1.</returns>
    public static float Sin(float f)
    {
        return (float)Math.Sin(f);
    }

    /// <summary>
    /// Returns the cosine of angle f.
    /// </summary>
    /// <param name="f">The input angle, in radians.</param>
    /// <returns>The return value between -1 and 1.</returns>
    public static float Cos(float f)
    {
        return (float)Math.Cos(f);
    }

    /// <summary>
    /// Returns the tangent of angle f in radians.
    /// </summary>
    /// <param name="f">The input angle, in radians.</param>
    /// <returns>The tangent of the angle.</returns>
    public static float Tan(float f)
    {
        return (float)Math.Tan(f);
    }

    /// <summary>
    /// Returns the arc-sine of f - the angle in radians whose sine is f.
    /// </summary>
    /// <param name="f">The input value, must be in the range [-1, 1].</param>
    /// <returns>The angle in radians.</returns>
    public static float Asin(float f)
    {
        return (float)Math.Asin(f);
    }

    /// <summary>
    /// Returns the arc-cosine of f - the angle in radians whose cosine is f.
    /// </summary>
    /// <param name="f">The input value, must be in the range [-1, 1].</param>
    /// <returns>The angle in radians.</returns>
    public static float Acos(float f)
    {
        return (float)Math.Acos(f);
    }

    /// <summary>
    /// Returns the arc-tangent of f - the angle in radians whose tangent is f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The angle in radians.</returns>
    public static float Atan(float f)
    {
        return (float)Math.Atan(f);
    }

    /// <summary>
    /// Returns the angle in radians whose Tan is y/x.
    /// </summary>
    /// <param name="y">The y coordinate.</param>
    /// <param name="x">The x coordinate.</param>
    /// <returns>The angle in radians.</returns>
    public static float Atan2(float y, float x)
    {
        return (float)Math.Atan2(y, x);
    }

    /// <summary>
    /// Returns square root of f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The square root of f.</returns>
    public static float Sqrt(float f)
    {
        return (float)Math.Sqrt(f);
    }

    /// <summary>
    /// Returns the absolute value of f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The absolute value of f.</returns>
    public static float Abs(float f)
    {
        return Math.Abs(f);
    }

    /// <summary>
    /// Returns the absolute value of value.
    /// </summary>
    /// <param name="value">The input value.</param>
    /// <returns>The absolute value of value.</returns>
    public static int Abs(int value)
    {
        return Math.Abs(value);
    }

    /// <summary>
    /// Returns the smallest of two values.
    /// </summary>
    /// <param name="a">The first value.</param>
    /// <param name="b">The second value.</param>
    /// <returns>The smallest value.</returns>
    public static float Min(float a, float b)
    {
        return (a < b) ? a : b;
    }

    /// <summary>
    /// Returns the smallest of two or more values.
    /// </summary>
    /// <param name="values">The array of values.</param>
    /// <returns>The smallest value.</returns>
    public static float Min(params float[] values)
    {
        int num = values.Length;
        if (num == 0)
        {
            return 0f;
        }

        float num2 = values[0];
        for (int i = 1; i < num; i++)
        {
            if (values[i] < num2)
            {
                num2 = values[i];
            }
        }

        return num2;
    }

    /// <summary>
    /// Returns the smallest of two values.
    /// </summary>
    /// <param name="a">The first value.</param>
    /// <param name="b">The second value.</param>
    /// <returns>The smallest value.</returns>
    public static int Min(int a, int b)
    {
        return (a < b) ? a : b;
    }

    /// <summary>
    /// Returns the smallest of two or more values.
    /// </summary>
    /// <param name="values">The array of values.</param>
    /// <returns>The smallest value.</returns>
    public static int Min(params int[] values)
    {
        int num = values.Length;
        if (num == 0)
        {
            return 0;
        }

        int num2 = values[0];
        for (int i = 1; i < num; i++)
        {
            if (values[i] < num2)
            {
                num2 = values[i];
            }
        }

        return num2;
    }

    /// <summary>
    /// Returns largest of two values.
    /// </summary>
    /// <param name="a">The first value.</param>
    /// <param name="b">The second value.</param>
    /// <returns>The largest value.</returns>
    public static float Max(float a, float b)
    {
        return (a > b) ? a : b;
    }

    /// <summary>
    /// Returns largest of two or more values.
    /// </summary>
    /// <param name="values">The array of values.</param>
    /// <returns>The largest value.</returns>
    public static float Max(params float[] values)
    {
        int num = values.Length;
        if (num == 0)
        {
            return 0f;
        }

        float num2 = values[0];
        for (int i = 1; i < num; i++)
        {
            if (values[i] > num2)
            {
                num2 = values[i];
            }
        }

        return num2;
    }

    /// <summary>
    /// Returns the largest of two values.
    /// </summary>
    /// <param name="a">The first value.</param>
    /// <param name="b">The second value.</param>
    /// <returns>The largest value.</returns>
    public static int Max(int a, int b)
    {
        return (a > b) ? a : b;
    }

    /// <summary>
    /// Returns the largest of two or more values.
    /// </summary>
    /// <param name="values">The array of values.</param>
    /// <returns>The largest value.</returns>
    public static int Max(params int[] values)
    {
        int num = values.Length;
        if (num == 0)
        {
            return 0;
        }

        int num2 = values[0];
        for (int i = 1; i < num; i++)
        {
            if (values[i] > num2)
            {
                num2 = values[i];
            }
        }

        return num2;
    }

    /// <summary>
    /// Returns f raised to power p.
    /// </summary>
    /// <param name="f">The base value.</param>
    /// <param name="p">The power.</param>
    /// <returns>f raised to power p.</returns>
    public static float Pow(float f, float p)
    {
        return (float)Math.Pow(f, p);
    }

    /// <summary>
    /// Returns e raised to the specified power.
    /// </summary>
    /// <param name="power">The power.</param>
    /// <returns>e raised to the specified power.</returns>
    public static float Exp(float power)
    {
        return (float)Math.Exp(power);
    }

    /// <summary>
    /// Returns the logarithm of a specified number in a specified base.
    /// </summary>
    /// <param name="f">The number.</param>
    /// <param name="p">The base.</param>
    /// <returns>The logarithm of f in base p.</returns>
    public static float Log(float f, float p)
    {
        return (float)Math.Log(f, p);
    }

    /// <summary>
    /// Returns the natural (base e) logarithm of a specified number.
    /// </summary>
    /// <param name="f">The number.</param>
    /// <returns>The natural logarithm of f.</returns>
    public static float Log(float f)
    {
        return (float)Math.Log(f);
    }

    /// <summary>
    /// Returns the base 10 logarithm of a specified number.
    /// </summary>
    /// <param name="f">The number.</param>
    /// <returns>The base 10 logarithm of f.</returns>
    public static float Log10(float f)
    {
        return (float)Math.Log10(f);
    }

    /// <summary>
    /// Returns the smallest integer greater to or equal to f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The smallest integer greater to or equal to f.</returns>
    public static float Ceil(float f)
    {
        return (float)Math.Ceiling(f);
    }

    /// <summary>
    /// Returns the largest integer smaller than or equal to f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The largest integer smaller than or equal to f.</returns>
    public static float Floor(float f)
    {
        return (float)Math.Floor(f);
    }

    /// <summary>
    /// Returns f rounded to the nearest integer.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>f rounded to the nearest integer.</returns>
    public static float Round(float f)
    {
        return (float)Math.Round(f);
    }

    /// <summary>
    /// Returns the smallest integer greater to or equal to f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The smallest integer greater to or equal to f.</returns>
    public static int CeilToInt(float f)
    {
        return (int)Math.Ceiling(f);
    }

    /// <summary>
    /// Returns the largest integer smaller to or equal to f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>The largest integer smaller to or equal to f.</returns>
    public static int FloorToInt(float f)
    {
        return (int)Math.Floor(f);
    }

    /// <summary>
    /// Returns f rounded to the nearest integer.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>f rounded to the nearest integer.</returns>
    public static int RoundToInt(float f)
    {
        return (int)Math.Round(f);
    }

    /// <summary>
    /// Returns the sign of f.
    /// </summary>
    /// <param name="f">The input value.</param>
    /// <returns>1 if f is positive, -1 if f is negative, 0 if f is zero.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Sign(float f)
    {
        return (f >= 0f) ? 1f : (-1f);
    }

    /// <summary>
    /// Clamps the given value between the given minimum float and maximum float values. Returns the given value if it is within the minimum and maximum range.
    /// </summary>
    /// <param name="value">The floating point value to restrict inside the range defined by the minimum and maximum values.</param>
    /// <param name="min">The minimum floating point value to compare against.</param>
    /// <param name="max">The maximum floating point value to compare against.</param>
    /// <returns>The float result between the minimum and maximum values.</returns>
    public static float Clamp(float value, float min, float max)
    {
        if (value < min)
        {
            value = min;
        }
        else if (value > max)
        {
            value = max;
        }

        return value;
    }

    /// <summary>
    /// Clamps the given value between a range defined by the given minimum integer and maximum integer values. Returns the given value if it is within min and max.
    /// </summary>
    /// <param name="value">The integer point value to restrict inside the min-to-max range.</param>
    /// <param name="min">The minimum integer point value to compare against.</param>
    /// <param name="max">The maximum integer point value to compare against.</param>
    /// <returns>The int result between min and max values.</returns>
    public static int Clamp(int value, int min, int max)
    {
        if (value < min)
        {
            value = min;
        }
        else if (value > max)
        {
            value = max;
        }

        return value;
    }

    /// <summary>
    /// Clamps value between 0 and 1 and returns value.
    /// </summary>
    /// <param name="value">The input value.</param>
    /// <returns>The clamped value between 0 and 1.</returns>
    public static float Clamp01(float value)
    {
        if (value < 0f)
        {
            return 0f;
        }

        if (value > 1f)
        {
            return 1f;
        }

        return value;
    }

    /// <summary>
    /// Linearly interpolates between a and b by t.
    /// </summary>
    /// <param name="a">The start value.</param>
    /// <param name="b">The end value.</param>
    /// <param name="t">The interpolation value between the two floats.</param>
    /// <returns>The interpolated float result between the two float values.</returns>
    public static float Lerp(float a, float b, float t)
    {
        return a + (b - a) * Clamp01(t);
    }

    /// <summary>
    /// Linearly interpolates between a and b by t with no limit to t.
    /// </summary>
    /// <param name="a">The start value.</param>
    /// <param name="b">The end value.</param>
    /// <param name="t">The interpolation between the two floats.</param>
    /// <returns>The float value as a result from the linear interpolation.</returns>
    public static float LerpUnclamped(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    /// <summary>
    /// Same as Lerp but makes sure the values interpolate correctly when they wrap around 360 degrees.
    /// </summary>
    /// <param name="a">The start angle. A float expressed in degrees.</param>
    /// <param name="b">The end angle. A float expressed in degrees.</param>
    /// <param name="t">The interpolation value between the start and end angles. This value is clamped to the range [0, 1].</param>
    /// <returns>Returns the interpolated float result between angle a and angle b, based on the interpolation value t.</returns>
    public static float LerpAngle(float a, float b, float t)
    {
        float num = Repeat(b - a, 360f);
        if (num > 180f)
        {
            num -= 360f;
        }

        return a + num * Clamp01(t);
    }

    /// <summary>
    /// Moves a value current towards target.
    /// </summary>
    /// <param name="current">The current value.</param>
    /// <param name="target">The value to move towards.</param>
    /// <param name="maxDelta">The maximum change that should be applied to the value.</param>
    /// <returns>The new value.</returns>
    public static float MoveTowards(float current, float target, float maxDelta)
    {
        if (Abs(target - current) <= maxDelta)
        {
            return target;
        }

        return current + Sign(target - current) * maxDelta;
    }

    /// <summary>
    /// Same as MoveTowards but makes sure the values interpolate correctly when they wrap around 360 degrees.
    /// </summary>
    /// <param name="current">The current angle in degrees.</param>
    /// <param name="target">The target angle in degrees.</param>
    /// <param name="maxDelta">The maximum change in degrees.</param>
    /// <returns>The new angle.</returns>
    public static float MoveTowardsAngle(float current, float target, float maxDelta)
    {
        float num = DeltaAngle(current, target);
        if (0f - maxDelta < num && num < maxDelta)
        {
            return target;
        }

        target = current + num;
        return MoveTowards(current, target, maxDelta);
    }

    /// <summary>
    /// Interpolates between min and max with smoothing at the limits.
    /// </summary>
    /// <param name="from">The start value.</param>
    /// <param name="to">The end value.</param>
    /// <param name="t">The interpolation parameter.</param>
    /// <returns>The smoothed interpolated value.</returns>
    public static float SmoothStep(float from, float to, float t)
    {
        t = Clamp01(t);
        t = -2f * t * t * t + 3f * t * t;
        return to * t + from * (1f - t);
    }


    /// <summary>
    /// Compares two floating point values and returns true if they are similar.
    /// </summary>
    /// <param name="a">The first value.</param>
    /// <param name="b">The second value.</param>
    /// <returns>True if the values are approximately equal; otherwise, false.</returns>
    public static bool Approximately(float a, float b)
    {
        return Abs(b - a) < Max(1E-06f * Max(Abs(a), Abs(b)), Epsilon * 8f);
    }


    /// <summary>
    /// Gradually changes a value towards a desired goal over time.
    /// </summary>
    /// <param name="current">The current position.</param>
    /// <param name="target">The position we are trying to reach.</param>
    /// <param name="currentVelocity">The current velocity, this value is modified by the function every time you call it.</param>
    /// <param name="smoothTime">Approximately the time it will take to reach the target. A smaller value will reach the target faster.</param>
    /// <param name="maxSpeed">Optionally allows you to clamp the maximum speed.</param>
    /// <param name="deltaTime">The time since the last call to this function.</param>
    /// <returns>The smoothed value.</returns>
    public static float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
    {
        smoothTime = Max(0.0001f, smoothTime);
        float num = 2f / smoothTime;
        float num2 = num * deltaTime;
        float num3 = 1f / (1f + num2 + 0.48f * num2 * num2 + 0.235f * num2 * num2 * num2);
        float value = current - target;
        float num4 = target;
        float num5 = maxSpeed * smoothTime;
        value = Clamp(value, 0f - num5, num5);
        target = current - value;
        float num6 = (currentVelocity + num * value) * deltaTime;
        currentVelocity = (currentVelocity - num * num6) * num3;
        float num7 = target + (value + num6) * num3;
        if (num4 - current > 0f == num7 > num4)
        {
            num7 = num4;
            currentVelocity = (num7 - num4) / deltaTime;
        }

        return num7;
    }

    /// <summary>
    /// Gradually changes an angle towards a desired goal over time.
    /// </summary>
    /// <param name="current">The current angle in degrees.</param>
    /// <param name="target">The angle we are trying to reach in degrees.</param>
    /// <param name="currentVelocity">The current angular velocity, this value is modified by the function every time you call it.</param>
    /// <param name="smoothTime">Approximately the time it will take to reach the target. A smaller value will reach the target faster.</param>
    /// <param name="maxSpeed">Optionally allows you to clamp the maximum angular speed.</param>
    /// <param name="deltaTime">The time since the last call to this function.</param>
    /// <returns>The smoothed angle.</returns>
    public static float SmoothDampAngle(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
    {
        target = current + DeltaAngle(current, target);
        return SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, deltaTime);
    }

    /// <summary>
    /// Loops the value t, so that it is never larger than length and never smaller than 0.
    /// </summary>
    /// <param name="t">The input value.</param>
    /// <param name="length">The length of the loop.</param>
    /// <returns>The looped value.</returns>
    public static float Repeat(float t, float length)
    {
        return Clamp(t - Floor(t / length) * length, 0f, length);
    }

    /// <summary>
    /// PingPong returns a value that will increment and decrement between the value 0 and length.
    /// </summary>
    /// <param name="t">The input value.</param>
    /// <param name="length">The length of the ping-pong range.</param>
    /// <returns>The ping-ponged value.</returns>
    public static float PingPong(float t, float length)
    {
        t = Repeat(t, length * 2f);
        return length - Abs(t - length);
    }

    /// <summary>
    /// Determines where a value lies between two points.
    /// </summary>
    /// <param name="a">The start of the range.</param>
    /// <param name="b">The end of the range.</param>
    /// <param name="value">The point within the range you want to calculate.</param>
    /// <returns>A value between zero and one, representing where the "value" parameter falls within the range defined by a and b.</returns>
    public static float InverseLerp(float a, float b, float value)
    {
        if (a != b)
        {
            return Clamp01((value - a) / (b - a));
        }

        return 0f;
    }

    /// <summary>
    /// Calculates the shortest difference between two given angles given in degrees.
    /// </summary>
    /// <param name="current">The current angle in degrees.</param>
    /// <param name="target">The target angle in degrees.</param>
    /// <returns>The shortest angular difference in degrees.</returns>
    public static float DeltaAngle(float current, float target)
    {
        float num = Repeat(target - current, 360f);
        if (num > 180f)
        {
            num -= 360f;
        }

        return num;
    }


    internal static long RandomToLong(System.Random r)
    {
        byte[] array = new byte[8];
        r.NextBytes(array);
        return (long)(BitConverter.ToUInt64(array, 0) & 0x7FFFFFFFFFFFFFFFL);
    }

    internal static float ClampToFloat(double value)
    {
        if (double.IsPositiveInfinity(value))
        {
            return float.PositiveInfinity;
        }

        if (double.IsNegativeInfinity(value))
        {
            return float.NegativeInfinity;
        }

        if (value < -3.4028234663852886E+38)
        {
            return float.MinValue;
        }

        if (value > 3.4028234663852886E+38)
        {
            return float.MaxValue;
        }

        return (float)value;
    }

    internal static int ClampToInt(long value)
    {
        if (value < int.MinValue)
        {
            return int.MinValue;
        }

        if (value > int.MaxValue)
        {
            return int.MaxValue;
        }

        return (int)value;
    }

    internal static uint ClampToUInt(long value)
    {
        if (value < 0)
        {
            return 0u;
        }

        if (value > uint.MaxValue)
        {
            return uint.MaxValue;
        }

        return (uint)value;
    }

    internal static float RoundToMultipleOf(float value, float roundingValue)
    {
        if (roundingValue == 0f)
        {
            return value;
        }

        return Round(value / roundingValue) * roundingValue;
    }

    internal static float GetClosestPowerOfTen(float positiveNumber)
    {
        if (positiveNumber <= 0f)
        {
            return 1f;
        }

        return Pow(10f, RoundToInt(Log10(positiveNumber)));
    }

    internal static int GetNumberOfDecimalsForMinimumDifference(float minDifference)
    {
        return Clamp(-FloorToInt(Log10(Abs(minDifference))), 0, 15);
    }

    internal static int GetNumberOfDecimalsForMinimumDifference(double minDifference)
    {
        return (int)Math.Max(0.0, 0.0 - Math.Floor(Math.Log10(Math.Abs(minDifference))));
    }

    internal static float RoundBasedOnMinimumDifference(float valueToRound, float minDifference)
    {
        if (minDifference == 0f)
        {
            return DiscardLeastSignificantDecimal(valueToRound);
        }

        return (float)Math.Round(valueToRound, GetNumberOfDecimalsForMinimumDifference(minDifference), MidpointRounding.AwayFromZero);
    }

    internal static double RoundBasedOnMinimumDifference(double valueToRound, double minDifference)
    {
        if (minDifference == 0.0)
        {
            return DiscardLeastSignificantDecimal(valueToRound);
        }

        return Math.Round(valueToRound, GetNumberOfDecimalsForMinimumDifference(minDifference), MidpointRounding.AwayFromZero);
    }

    internal static float DiscardLeastSignificantDecimal(float v)
    {
        int digits = Clamp((int)(5f - Log10(Abs(v))), 0, 15);
        return (float)Math.Round(v, digits, MidpointRounding.AwayFromZero);
    }

    internal static double DiscardLeastSignificantDecimal(double v)
    {
        int digits = Math.Max(0, (int)(5.0 - Math.Log10(Math.Abs(v))));
        try
        {
            return Math.Round(v, digits);
        }
        catch (ArgumentOutOfRangeException)
        {
            return 0.0;
        }
    }

}

