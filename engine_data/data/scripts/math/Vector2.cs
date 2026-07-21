using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;


/// <summary>
/// Representation of 2D vectors and points.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vector2 : IEquatable<Vector2>, IFormattable
{
    /// <summary>
    /// X component of the vector.
    /// </summary>
    public float x;

    /// <summary>
    /// Y component of the vector.
    /// </summary>
    public float y;

    private static readonly Vector2 zeroVector = new Vector2(0f, 0f);

    private static readonly Vector2 oneVector = new Vector2(1f, 1f);

    private static readonly Vector2 upVector = new Vector2(0f, 1f);

    private static readonly Vector2 downVector = new Vector2(0f, -1f);

    private static readonly Vector2 leftVector = new Vector2(-1f, 0f);

    private static readonly Vector2 rightVector = new Vector2(1f, 0f);

    private static readonly Vector2 positiveInfinityVector = new Vector2(float.PositiveInfinity, float.PositiveInfinity);

    private static readonly Vector2 negativeInfinityVector = new Vector2(float.NegativeInfinity, float.NegativeInfinity);

    /// <summary>
    /// Gets or sets the component at the specified index.
    /// </summary>
    /// <param name="index">The index of the component (0 = x, 1 = y).</param>
    /// <returns>The component value at the specified index.</returns>
    /// <exception cref="IndexOutOfRangeException">Thrown when index is out of range [0, 1].</exception>
    public float this[int index]
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            switch (index)
            {
                case 0:
                    return x;
                case 1:
                    return y;
                default:
                    throw new IndexOutOfRangeException("Invalid Vector2 index!");
            }
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        set
        {
            switch (index)
            {
                case 0:
                    x = value;
                    break;
                case 1:
                    y = value;
                    break;
                default:
                    throw new IndexOutOfRangeException("Invalid Vector2 index!");
            }
        }
    }

    /// <summary>
    /// Returns this vector with a magnitude of 1 (Read Only).
    /// </summary>
    public Vector2 normalized
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            Vector2 result = new Vector2(x, y);
            result.Normalize();
            return result;
        }
    }

    /// <summary>
    /// Returns the length of this vector (Read Only).
    /// </summary>
    public float magnitude
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return (float)Math.Sqrt(x * x + y * y);
        }
    }

    /// <summary>
    /// Returns the squared length of this vector (Read Only).
    /// </summary>
    public float sqrMagnitude
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return x * x + y * y;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(0, 0).
    /// </summary>
    public static Vector2 zero
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return zeroVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(1, 1).
    /// </summary>
    public static Vector2 one
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return oneVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(0, 1).
    /// </summary>
    public static Vector2 up
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return upVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(0, -1).
    /// </summary>
    public static Vector2 down
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return downVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(-1, 0).
    /// </summary>
    public static Vector2 left
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return leftVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(1, 0).
    /// </summary>
    public static Vector2 right
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return rightVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(float.PositiveInfinity, float.PositiveInfinity).
    /// </summary>
    public static Vector2 positiveInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return positiveInfinityVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector2(float.NegativeInfinity, float.NegativeInfinity).
    /// </summary>
    public static Vector2 negativeInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return negativeInfinityVector;
        }
    }

    /// <summary>
    /// Constructs a new vector with given x, y components.
    /// </summary>
    /// <param name="x">The x component.</param>
    /// <param name="y">The y component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector2(float x, float y)
    {
        this.x = x;
        this.y = y;
    }

    /// <summary>
    /// Set x and y components of an existing Vector2.
    /// </summary>
    /// <param name="newX">The new x component.</param>
    /// <param name="newY">The new y component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Set(float newX, float newY)
    {
        x = newX;
        y = newY;
    }

    /// <summary>
    /// Linearly interpolates between vectors a and b by t.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <param name="t">The interpolation parameter.</param>
    /// <returns>The interpolated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    /// <summary>
    /// Linearly interpolates between vectors a and b by t without clamping the interpolation parameter.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <param name="t">The interpolation parameter (not clamped).</param>
    /// <returns>The interpolated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 LerpUnclamped(Vector2 a, Vector2 b, float t)
    {
        return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
    }

    /// <summary>
    /// Moves a point current towards target.
    /// </summary>
    /// <param name="current">The current position.</param>
    /// <param name="target">The target position.</param>
    /// <param name="maxDistanceDelta">The maximum distance to move.</param>
    /// <returns>The new position.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta)
    {
        float num = target.x - current.x;
        float num2 = target.y - current.y;
        float num3 = num * num + num2 * num2;
        if (num3 == 0f || (maxDistanceDelta >= 0f && num3 <= maxDistanceDelta * maxDistanceDelta))
        {
            return target;
        }

        float num4 = (float)Math.Sqrt(num3);
        return new Vector2(current.x + num / num4 * maxDistanceDelta, current.y + num2 / num4 * maxDistanceDelta);
    }

    /// <summary>
    /// Multiplies two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>A new vector with components multiplied.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Scale(Vector2 a, Vector2 b)
    {
        return new Vector2(a.x * b.x, a.y * b.y);
    }

    /// <summary>
    /// Multiplies every component of this vector by the same component of scale.
    /// </summary>
    /// <param name="scale">The scale vector.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Scale(Vector2 scale)
    {
        x *= scale.x;
        y *= scale.y;
    }

    /// <summary>
    /// Makes this vector have a magnitude of 1.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Normalize()
    {
        float num = magnitude;
        if (num > 1E-05f)
        {
            this /= num;
        }
        else
        {
            this = zero;
        }
    }

    /// <summary>
    /// Returns a formatted string for this vector.
    /// </summary>
    /// <returns>A formatted string representation of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override string ToString()
    {
        return ToString(null, null);
    }

    /// <summary>
    /// Returns a formatted string for this vector.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <returns>A formatted string representation of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format)
    {
        return ToString(format, null);
    }

    /// <summary>
    /// Returns a formatted string for this vector.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <param name="formatProvider">An object that specifies culture-specific formatting.</param>
    /// <returns>A formatted string representation of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format, IFormatProvider formatProvider)
    {
        if (string.IsNullOrEmpty(format))
        {
            format = "F2";
        }

        if (formatProvider == null)
        {
            formatProvider = CultureInfo.InvariantCulture.NumberFormat;
        }

        return string.Format("({0}, {1})", x.ToString(format, formatProvider), y.ToString(format, formatProvider));
    }

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override int GetHashCode()
    {
        return x.GetHashCode() ^ (y.GetHashCode() << 2);
    }

    /// <summary>
    /// Returns true if the given vector is exactly equal to this vector.
    /// </summary>
    /// <param name="other">The object to compare with the current instance.</param>
    /// <returns>True if the given vector is exactly equal to this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override bool Equals(object other)
    {
        if (!(other is Vector2))
        {
            return false;
        }

        return Equals((Vector2)other);
    }

    /// <summary>
    /// Returns true if the given vector is exactly equal to this vector.
    /// </summary>
    /// <param name="other">The vector to compare with the current instance.</param>
    /// <returns>True if the given vector is exactly equal to this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Equals(Vector2 other)
    {
        return x == other.x && y == other.y;
    }

    /// <summary>
    /// Reflects a vector off the vector defined by a normal.
    /// </summary>
    /// <param name="inDirection">The direction vector to reflect.</param>
    /// <param name="inNormal">The normal of the plane.</param>
    /// <returns>The reflected vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Reflect(Vector2 inDirection, Vector2 inNormal)
    {
        float num = -2f * Dot(inNormal, inDirection);
        return new Vector2(num * inNormal.x + inDirection.x, num * inNormal.y + inDirection.y);
    }

    /// <summary>
    /// Returns the 2D vector perpendicular to this 2D vector. The result is always rotated 90-degrees in a counter-clockwise direction for a 2D coordinate system where the positive Y axis goes up.
    /// </summary>
    /// <param name="inDirection">The input direction.</param>
    /// <returns>The perpendicular direction.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Perpendicular(Vector2 inDirection)
    {
        return new Vector2(0f - inDirection.y, inDirection.x);
    }

    /// <summary>
    /// Dot Product of two vectors.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>The dot product of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(Vector2 lhs, Vector2 rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y;
    }

    /// <summary>
    /// Gets the unsigned angle in degrees between from and to.
    /// </summary>
    /// <param name="from">The vector from which the angular difference is measured.</param>
    /// <param name="to">The vector to which the angular difference is measured.</param>
    /// <returns>The unsigned angle in degrees between the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Angle(Vector2 from, Vector2 to)
    {
        float num = (float)Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
        if (num < 1E-15f)
        {
            return 0f;
        }

        float num2 = Mathf.Clamp(Dot(from, to) / num, -1f, 1f);
        return (float)Math.Acos(num2) * 57.29578f;
    }

    /// <summary>
    /// Gets the signed angle in degrees between from and to.
    /// </summary>
    /// <param name="from">The vector from which the angular difference is measured.</param>
    /// <param name="to">The vector to which the angular difference is measured.</param>
    /// <returns>The signed angle in degrees between the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SignedAngle(Vector2 from, Vector2 to)
    {
        float num = Angle(from, to);
        float num2 = Mathf.Sign(from.x * to.y - from.y * to.x);
        return num * num2;
    }

    /// <summary>
    /// Returns the distance between a and b.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The distance between the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Distance(Vector2 a, Vector2 b)
    {
        float num = a.x - b.x;
        float num2 = a.y - b.y;
        return (float)Math.Sqrt(num * num + num2 * num2);
    }

    /// <summary>
    /// Returns a copy of vector with its magnitude clamped to maxLength.
    /// </summary>
    /// <param name="vector">The vector to clamp.</param>
    /// <param name="maxLength">The maximum length.</param>
    /// <returns>A copy of the vector with magnitude clamped to maxLength.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 ClampMagnitude(Vector2 vector, float maxLength)
    {
        float num = vector.sqrMagnitude;
        if (num > maxLength * maxLength)
        {
            float num2 = (float)Math.Sqrt(num);
            float num3 = vector.x / num2;
            float num4 = vector.y / num2;
            return new Vector2(num3 * maxLength, num4 * maxLength);
        }

        return vector;
    }

    /// <summary>
    /// Returns the squared length of the vector.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <returns>The squared length of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SqrMagnitude(Vector2 a)
    {
        return a.x * a.x + a.y * a.y;
    }

    /// <summary>
    /// Returns the squared length of this vector.
    /// </summary>
    /// <returns>The squared length of this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public float SqrMagnitude()
    {
        return x * x + y * y;
    }

    /// <summary>
    /// Returns a vector that is made from the smallest components of two vectors.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>A vector with the minimum components.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Min(Vector2 lhs, Vector2 rhs)
    {
        return new Vector2(Mathf.Min(lhs.x, rhs.x), Mathf.Min(lhs.y, rhs.y));
    }

    /// <summary>
    /// Returns a vector that is made from the largest components of two vectors.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>A vector with the maximum components.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 Max(Vector2 lhs, Vector2 rhs)
    {
        return new Vector2(Mathf.Max(lhs.x, rhs.x), Mathf.Max(lhs.y, rhs.y));
    }

    /// <summary>
    /// Gradually changes a vector towards a desired goal over time.
    /// </summary>
    /// <param name="current">The current position.</param>
    /// <param name="target">The position we are trying to reach.</param>
    /// <param name="currentVelocity">The current velocity, this value is modified by the function every time you call it.</param>
    /// <param name="smoothTime">Approximately the time it will take to reach the target. A smaller value will reach the target faster.</param>
    /// <param name="maxSpeed">Optionally allows you to clamp the maximum speed.</param>
    /// <param name="deltaTime">The time since the last call to this function.</param>
    /// <returns>The smoothed vector.</returns>
    public static Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
    {
        smoothTime = Mathf.Max(0.0001f, smoothTime);
        float num = 2f / smoothTime;
        float num2 = num * deltaTime;
        float num3 = 1f / (1f + num2 + 0.48f * num2 * num2 + 0.235f * num2 * num2 * num2);
        float num4 = current.x - target.x;
        float num5 = current.y - target.y;
        Vector2 vector = target;
        float num6 = maxSpeed * smoothTime;
        float num7 = num6 * num6;
        float num8 = num4 * num4 + num5 * num5;
        if (num8 > num7)
        {
            float num9 = (float)Math.Sqrt(num8);
            num4 = num4 / num9 * num6;
            num5 = num5 / num9 * num6;
        }

        target.x = current.x - num4;
        target.y = current.y - num5;
        float num10 = (currentVelocity.x + num * num4) * deltaTime;
        float num11 = (currentVelocity.y + num * num5) * deltaTime;
        currentVelocity.x = (currentVelocity.x - num * num10) * num3;
        currentVelocity.y = (currentVelocity.y - num * num11) * num3;
        float num12 = target.x + (num4 + num10) * num3;
        float num13 = target.y + (num5 + num11) * num3;
        float num14 = vector.x - current.x;
        float num15 = vector.y - current.y;
        float num16 = num12 - vector.x;
        float num17 = num13 - vector.y;
        if (num14 * num16 + num15 * num17 > 0f)
        {
            num12 = vector.x;
            num13 = vector.y;
            currentVelocity.x = (num12 - vector.x) / deltaTime;
            currentVelocity.y = (num13 - vector.y) / deltaTime;
        }

        return new Vector2(num12, num13);
    }

    /// <summary>
    /// Adds two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The sum of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator +(Vector2 a, Vector2 b)
    {
        return new Vector2(a.x + b.x, a.y + b.y);
    }

    /// <summary>
    /// Subtracts two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The difference of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator -(Vector2 a, Vector2 b)
    {
        return new Vector2(a.x - b.x, a.y - b.y);
    }

    /// <summary>
    /// Multiplies two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The component-wise product of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator *(Vector2 a, Vector2 b)
    {
        return new Vector2(a.x * b.x, a.y * b.y);
    }

    /// <summary>
    /// Divides two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The component-wise quotient of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator /(Vector2 a, Vector2 b)
    {
        return new Vector2(a.x / b.x, a.y / b.y);
    }

    /// <summary>
    /// Negates a vector.
    /// </summary>
    /// <param name="a">The vector to negate.</param>
    /// <returns>The negated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator -(Vector2 a)
    {
        return new Vector2(0f - a.x, 0f - a.y);
    }

    /// <summary>
    /// Multiplies a vector by a scalar.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <param name="d">The scalar value.</param>
    /// <returns>The scaled vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator *(Vector2 a, float d)
    {
        return new Vector2(a.x * d, a.y * d);
    }

    /// <summary>
    /// Multiplies a scalar by a vector.
    /// </summary>
    /// <param name="d">The scalar value.</param>
    /// <param name="a">The vector.</param>
    /// <returns>The scaled vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator *(float d, Vector2 a)
    {
        return new Vector2(a.x * d, a.y * d);
    }

    /// <summary>
    /// Divides a vector by a scalar.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <param name="d">The scalar value.</param>
    /// <returns>The divided vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector2 operator /(Vector2 a, float d)
    {
        return new Vector2(a.x / d, a.y / d);
    }

    /// <summary>
    /// Determines whether two vectors are approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>True if the vectors are approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(Vector2 lhs, Vector2 rhs)
    {
        float num = lhs.x - rhs.x;
        float num2 = lhs.y - rhs.y;
        return num * num + num2 * num2 < 9.99999944E-11f;
    }

    /// <summary>
    /// Determines whether two vectors are not approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>True if the vectors are not approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Vector2 lhs, Vector2 rhs)
    {
        return !(lhs == rhs);
    }

    /// <summary>
    /// Implicitly converts a Vector3 to a Vector2 by taking the x and y components.
    /// </summary>
    /// <param name="v">The Vector3 to convert.</param>
    /// <returns>A Vector2 with x and y components from the Vector3.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector2(Vector3 v)
    {
        return new Vector2(v.x, v.y);
    }

    /// <summary>
    /// Implicitly converts a Vector2 to a Vector3 by adding a z component of 0.
    /// </summary>
    /// <param name="v">The Vector2 to convert.</param>
    /// <returns>A Vector3 with x and y from Vector2 and z = 0.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector3(Vector2 v)
    {
        return new Vector3(v.x, v.y, 0f);
    }

}

