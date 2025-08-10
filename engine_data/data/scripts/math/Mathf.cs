using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential, Size = 1)]
public struct MathfInternal
{
    public static volatile float FloatMinNormal = 1.17549435E-38f;

    public static volatile float FloatMinDenormal = float.Epsilon;

    public static bool IsFlushToZeroEnabled = FloatMinDenormal == 0f;
}

[StructLayout(LayoutKind.Sequential, Size = 1)]
public struct Mathf
{
    //
    // Summary:
    //     The well-known 3.14159265358979... value (Read Only).
    public const float PI = MathF.PI;

    //
    // Summary:
    //     A representation of positive infinity (Read Only).
    public const float Infinity = float.PositiveInfinity;

    //
    // Summary:
    //     A representation of negative infinity (Read Only).
    public const float NegativeInfinity = float.NegativeInfinity;

    //
    // Summary:
    //     Degrees-to-radians conversion constant (Read Only).
    public const float Deg2Rad = MathF.PI / 180f;

    //
    // Summary:
    //     Radians-to-degrees conversion constant (Read Only).
    public const float Rad2Deg = 57.29578f;

    internal const int kMaxDecimals = 15;


    public const float kEpsilon = 1E-05f;

    public const float kEpsilonNormalSqrt = 1E-15f;
    //
    // Summary:
    //     A tiny floating point value (Read Only).
    public static readonly float Epsilon = (MathfInternal.IsFlushToZeroEnabled ? MathfInternal.FloatMinNormal : MathfInternal.FloatMinDenormal);

    //
    // Summary:
    //     Returns the closest power of two value.
    //
    // Parameters:
    //   value:
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int ClosestPowerOfTwo(int value);

    //
    // Summary:
    //     Returns true if the value is power of two.
    //
    // Parameters:
    //   value:
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern bool IsPowerOfTwo(int value);

    //
    // Summary:
    //     Returns the next power of two that is equal to, or greater than, the argument.
    //
    //
    // Parameters:
    //   value:
    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern int NextPowerOfTwo(int value);

    //
    // Summary:
    //     Returns the sine of angle f.
    //
    // Parameters:
    //   f:
    //     The input angle, in radians.
    //
    // Returns:
    //     The return value between -1 and +1.
    public static float Sin(float f)
    {
        return (float)Math.Sin(f);
    }

    //
    // Summary:
    //     Returns the cosine of angle f.
    //
    // Parameters:
    //   f:
    //     The input angle, in radians.
    //
    // Returns:
    //     The return value between -1 and 1.
    public static float Cos(float f)
    {
        return (float)Math.Cos(f);
    }

    //
    // Summary:
    //     Returns the tangent of angle f in radians.
    //
    // Parameters:
    //   f:
    public static float Tan(float f)
    {
        return (float)Math.Tan(f);
    }

    //
    // Summary:
    //     Returns the arc-sine of f - the angle in radians whose sine is f.
    //
    // Parameters:
    //   f:
    public static float Asin(float f)
    {
        return (float)Math.Asin(f);
    }

    //
    // Summary:
    //     Returns the arc-cosine of f - the angle in radians whose cosine is f.
    //
    // Parameters:
    //   f:
    public static float Acos(float f)
    {
        return (float)Math.Acos(f);
    }

    //
    // Summary:
    //     Returns the arc-tangent of f - the angle in radians whose tangent is f.
    //
    // Parameters:
    //   f:
    public static float Atan(float f)
    {
        return (float)Math.Atan(f);
    }

    //
    // Summary:
    //     Returns the angle in radians whose Tan is y/x.
    //
    // Parameters:
    //   y:
    //
    //   x:
    public static float Atan2(float y, float x)
    {
        return (float)Math.Atan2(y, x);
    }

    //
    // Summary:
    //     Returns square root of f.
    //
    // Parameters:
    //   f:
    public static float Sqrt(float f)
    {
        return (float)Math.Sqrt(f);
    }

    //
    // Summary:
    //     Returns the absolute value of f.
    //
    // Parameters:
    //   f:
    public static float Abs(float f)
    {
        return Math.Abs(f);
    }

    //
    // Summary:
    //     Returns the absolute value of value.
    //
    // Parameters:
    //   value:
    public static int Abs(int value)
    {
        return Math.Abs(value);
    }

    //
    // Summary:
    //     Returns the smallest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
    public static float Min(float a, float b)
    {
        return (a < b) ? a : b;
    }

    //
    // Summary:
    //     Returns the smallest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
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

    //
    // Summary:
    //     Returns the smallest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
    public static int Min(int a, int b)
    {
        return (a < b) ? a : b;
    }

    //
    // Summary:
    //     Returns the smallest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
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

    //
    // Summary:
    //     Returns largest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
    public static float Max(float a, float b)
    {
        return (a > b) ? a : b;
    }

    //
    // Summary:
    //     Returns largest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
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

    //
    // Summary:
    //     Returns the largest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
    public static int Max(int a, int b)
    {
        return (a > b) ? a : b;
    }

    //
    // Summary:
    //     Returns the largest of two or more values.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   values:
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

    //
    // Summary:
    //     Returns f raised to power p.
    //
    // Parameters:
    //   f:
    //
    //   p:
    public static float Pow(float f, float p)
    {
        return (float)Math.Pow(f, p);
    }

    //
    // Summary:
    //     Returns e raised to the specified power.
    //
    // Parameters:
    //   power:
    public static float Exp(float power)
    {
        return (float)Math.Exp(power);
    }

    //
    // Summary:
    //     Returns the logarithm of a specified number in a specified base.
    //
    // Parameters:
    //   f:
    //
    //   p:
    public static float Log(float f, float p)
    {
        return (float)Math.Log(f, p);
    }

    //
    // Summary:
    //     Returns the natural (base e) logarithm of a specified number.
    //
    // Parameters:
    //   f:
    public static float Log(float f)
    {
        return (float)Math.Log(f);
    }

    //
    // Summary:
    //     Returns the base 10 logarithm of a specified number.
    //
    // Parameters:
    //   f:
    public static float Log10(float f)
    {
        return (float)Math.Log10(f);
    }

    //
    // Summary:
    //     Returns the smallest integer greater to or equal to f.
    //
    // Parameters:
    //   f:
    public static float Ceil(float f)
    {
        return (float)Math.Ceiling(f);
    }

    //
    // Summary:
    //     Returns the largest integer smaller than or equal to f.
    //
    // Parameters:
    //   f:
    public static float Floor(float f)
    {
        return (float)Math.Floor(f);
    }

    //
    // Summary:
    //     Returns f rounded to the nearest integer.
    //
    // Parameters:
    //   f:
    public static float Round(float f)
    {
        return (float)Math.Round(f);
    }

    //
    // Summary:
    //     Returns the smallest integer greater to or equal to f.
    //
    // Parameters:
    //   f:
    public static int CeilToInt(float f)
    {
        return (int)Math.Ceiling(f);
    }

    //
    // Summary:
    //     Returns the largest integer smaller to or equal to f.
    //
    // Parameters:
    //   f:
    public static int FloorToInt(float f)
    {
        return (int)Math.Floor(f);
    }

    //
    // Summary:
    //     Returns f rounded to the nearest integer.
    //
    // Parameters:
    //   f:
    public static int RoundToInt(float f)
    {
        return (int)Math.Round(f);
    }

    //
    // Summary:
    //     Returns the sign of f.
    //
    // Parameters:
    //   f:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Sign(float f)
    {
        return (f >= 0f) ? 1f : (-1f);
    }

    //
    // Summary:
    //     Clamps the given value between the given minimum float and maximum float values.
    //     Returns the given value if it is within the minimum and maximum range.
    //
    // Parameters:
    //   value:
    //     The floating point value to restrict inside the range defined by the minimum
    //     and maximum values.
    //
    //   min:
    //     The minimum floating point value to compare against.
    //
    //   max:
    //     The maximum floating point value to compare against.
    //
    // Returns:
    //     The float result between the minimum and maximum values.
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

    //
    // Summary:
    //     Clamps the given value between a range defined by the given minimum integer and
    //     maximum integer values. Returns the given value if it is within min and max.
    //
    //
    // Parameters:
    //   value:
    //     The integer point value to restrict inside the min-to-max range.
    //
    //   min:
    //     The minimum integer point value to compare against.
    //
    //   max:
    //     The maximum integer point value to compare against.
    //
    // Returns:
    //     The int result between min and max values.
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

    //
    // Summary:
    //     Clamps value between 0 and 1 and returns value.
    //
    // Parameters:
    //   value:
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

    //
    // Summary:
    //     Linearly interpolates between a and b by t.
    //
    // Parameters:
    //   a:
    //     The start value.
    //
    //   b:
    //     The end value.
    //
    //   t:
    //     The interpolation value between the two floats.
    //
    // Returns:
    //     The interpolated float result between the two float values.
    public static float Lerp(float a, float b, float t)
    {
        return a + (b - a) * Clamp01(t);
    }

    //
    // Summary:
    //     Linearly interpolates between a and b by t with no limit to t.
    //
    // Parameters:
    //   a:
    //     The start value.
    //
    //   b:
    //     The end value.
    //
    //   t:
    //     The interpolation between the two floats.
    //
    // Returns:
    //     The float value as a result from the linear interpolation.
    public static float LerpUnclamped(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    //
    // Summary:
    //     Same as Lerp but makes sure the values interpolate correctly when they wrap around
    //     360 degrees.
    //
    // Parameters:
    //   a:
    //     The start angle. A float expressed in degrees.
    //
    //   b:
    //     The end angle. A float expressed in degrees.
    //
    //   t:
    //     The interpolation value between the start and end angles. This value is clamped
    //     to the range [0, 1].
    //
    // Returns:
    //     Returns the interpolated float result between angle a and angle b, based on the
    //     interpolation value t.
    public static float LerpAngle(float a, float b, float t)
    {
        float num = Repeat(b - a, 360f);
        if (num > 180f)
        {
            num -= 360f;
        }

        return a + num * Clamp01(t);
    }

    //
    // Summary:
    //     Moves a value current towards target.
    //
    // Parameters:
    //   current:
    //     The current value.
    //
    //   target:
    //     The value to move towards.
    //
    //   maxDelta:
    //     The maximum change that should be applied to the value.
    public static float MoveTowards(float current, float target, float maxDelta)
    {
        if (Abs(target - current) <= maxDelta)
        {
            return target;
        }

        return current + Sign(target - current) * maxDelta;
    }

    //
    // Summary:
    //     Same as MoveTowards but makes sure the values interpolate correctly when they
    //     wrap around 360 degrees.
    //
    // Parameters:
    //   current:
    //
    //   target:
    //
    //   maxDelta:
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

    //
    // Summary:
    //     Interpolates between min and max with smoothing at the limits.
    //
    // Parameters:
    //   from:
    //
    //   to:
    //
    //   t:
    public static float SmoothStep(float from, float to, float t)
    {
        t = Clamp01(t);
        t = -2f * t * t * t + 3f * t * t;
        return to * t + from * (1f - t);
    }


    //
    // Summary:
    //     Compares two floating point values and returns true if they are similar.
    //
    // Parameters:
    //   a:
    //
    //   b:
    public static bool Approximately(float a, float b)
    {
        return Abs(b - a) < Max(1E-06f * Max(Abs(a), Abs(b)), Epsilon * 8f);
    }


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

    public static float SmoothDampAngle(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
    {
        target = current + DeltaAngle(current, target);
        return SmoothDamp(current, target, ref currentVelocity, smoothTime, maxSpeed, deltaTime);
    }

    //
    // Summary:
    //     Loops the value t, so that it is never larger than length and never smaller than
    //     0.
    //
    // Parameters:
    //   t:
    //
    //   length:
    public static float Repeat(float t, float length)
    {
        return Clamp(t - Floor(t / length) * length, 0f, length);
    }

    //
    // Summary:
    //     PingPong returns a value that will increment and decrement between the value
    //     0 and length.
    //
    // Parameters:
    //   t:
    //
    //   length:
    public static float PingPong(float t, float length)
    {
        t = Repeat(t, length * 2f);
        return length - Abs(t - length);
    }

    //
    // Summary:
    //     Determines where a value lies between two points.
    //
    // Parameters:
    //   a:
    //     The start of the range.
    //
    //   b:
    //     The end of the range.
    //
    //   value:
    //     The point within the range you want to calculate.
    //
    // Returns:
    //     A value between zero and one, representing where the "value" parameter falls
    //     within the range defined by a and b.
    public static float InverseLerp(float a, float b, float value)
    {
        if (a != b)
        {
            return Clamp01((value - a) / (b - a));
        }

        return 0f;
    }

    //
    // Summary:
    //     Calculates the shortest difference between two given angles given in degrees.
    //
    //
    // Parameters:
    //   current:
    //
    //   target:
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

