using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

/// <summary>
/// Representation of four-dimensional vectors.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vector4 : IEquatable<Vector4>, IFormattable
{
    /// <summary>
    /// X component of the vector.
    /// </summary>
    public float x;

    /// <summary>
    /// Y component of the vector.
    /// </summary>
    public float y;

    /// <summary>
    /// Z component of the vector.
    /// </summary>
    public float z;

    /// <summary>
    /// W component of the vector.
    /// </summary>
    public float w;

    private static readonly Vector4 zeroVector = new Vector4(0f, 0f, 0f, 0f);

    private static readonly Vector4 oneVector = new Vector4(1f, 1f, 1f, 1f);

    private static readonly Vector4 positiveInfinityVector = new Vector4(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);

    private static readonly Vector4 negativeInfinityVector = new Vector4(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

    /// <summary>
    /// Gets or sets the component at the specified index.
    /// </summary>
    /// <param name="index">The index of the component (0 = x, 1 = y, 2 = z, 3 = w).</param>
    /// <returns>The component value at the specified index.</returns>
    /// <exception cref="IndexOutOfRangeException">Thrown when index is out of range [0, 3].</exception>
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
                case 2:
                    return z;
                case 3:
                    return w;
                default:
                    throw new IndexOutOfRangeException("Invalid Vector4 index!");
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
                case 2:
                    z = value;
                    break;
                case 3:
                    w = value;
                    break;
                default:
                    throw new IndexOutOfRangeException("Invalid Vector4 index!");
            }
        }
    }

    /// <summary>
    /// Returns this vector with a magnitude of 1 (Read Only).
    /// </summary>
    public Vector4 normalized
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return Normalize(this);
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
            return (float)Math.Sqrt(Dot(this, this));
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
            return Dot(this, this);
        }
    }

    /// <summary>
    /// Shorthand for writing Vector4(0,0,0,0).
    /// </summary>
    public static Vector4 zero
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return zeroVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector4(1,1,1,1).
    /// </summary>
    public static Vector4 one
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return oneVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector4(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity).
    /// </summary>
    public static Vector4 positiveInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return positiveInfinityVector;
        }
    }

    /// <summary>
    /// Shorthand for writing Vector4(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity).
    /// </summary>
    public static Vector4 negativeInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return negativeInfinityVector;
        }
    }

    /// <summary>
    /// Creates a new vector with given x, y, z, w components.
    /// </summary>
    /// <param name="x">The x component.</param>
    /// <param name="y">The y component.</param>
    /// <param name="z">The z component.</param>
    /// <param name="w">The w component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y, float z, float w)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    /// <summary>
    /// Creates a new vector with given x, y, z components and sets w to zero.
    /// </summary>
    /// <param name="x">The x component.</param>
    /// <param name="y">The y component.</param>
    /// <param name="z">The z component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        w = 0f;
    }

    /// <summary>
    /// Creates a new vector with given x, y components and sets z and w to zero.
    /// </summary>
    /// <param name="x">The x component.</param>
    /// <param name="y">The y component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y)
    {
        this.x = x;
        this.y = y;
        z = 0f;
        w = 0f;
    }

    /// <summary>
    /// Set x, y, z and w components of an existing Vector4.
    /// </summary>
    /// <param name="newX">The new x component.</param>
    /// <param name="newY">The new y component.</param>
    /// <param name="newZ">The new z component.</param>
    /// <param name="newW">The new w component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Set(float newX, float newY, float newZ, float newW)
    {
        x = newX;
        y = newY;
        z = newZ;
        w = newW;
    }

    /// <summary>
    /// Linearly interpolates between two vectors.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <param name="t">The interpolation parameter.</param>
    /// <returns>The interpolated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Lerp(Vector4 a, Vector4 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    /// <summary>
    /// Linearly interpolates between two vectors without clamping the interpolation parameter.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <param name="t">The interpolation parameter (not clamped).</param>
    /// <returns>The interpolated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 LerpUnclamped(Vector4 a, Vector4 b, float t)
    {
        return new Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    /// <summary>
    /// Moves a point current towards target.
    /// </summary>
    /// <param name="current">The current position.</param>
    /// <param name="target">The target position.</param>
    /// <param name="maxDistanceDelta">The maximum distance to move.</param>
    /// <returns>The new position.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 MoveTowards(Vector4 current, Vector4 target, float maxDistanceDelta)
    {
        float num = target.x - current.x;
        float num2 = target.y - current.y;
        float num3 = target.z - current.z;
        float num4 = target.w - current.w;
        float num5 = num * num + num2 * num2 + num3 * num3 + num4 * num4;
        if (num5 == 0f || (maxDistanceDelta >= 0f && num5 <= maxDistanceDelta * maxDistanceDelta))
        {
            return target;
        }

        float num6 = (float)Math.Sqrt(num5);
        return new Vector4(current.x + num / num6 * maxDistanceDelta, current.y + num2 / num6 * maxDistanceDelta, current.z + num3 / num6 * maxDistanceDelta, current.w + num4 / num6 * maxDistanceDelta);
    }

    /// <summary>
    /// Multiplies two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>A new vector with components multiplied.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Scale(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
    }

    /// <summary>
    /// Multiplies every component of this vector by the same component of scale.
    /// </summary>
    /// <param name="scale">The scale vector.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Scale(Vector4 scale)
    {
        x *= scale.x;
        y *= scale.y;
        z *= scale.z;
        w *= scale.w;
    }

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override int GetHashCode()
    {
        return x.GetHashCode() ^ (y.GetHashCode() << 2) ^ (z.GetHashCode() >> 2) ^ (w.GetHashCode() >> 1);
    }

    /// <summary>
    /// Returns true if the given vector is exactly equal to this vector.
    /// </summary>
    /// <param name="other">The object to compare with the current instance.</param>
    /// <returns>True if the given vector is exactly equal to this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override bool Equals(object other)
    {
        if (!(other is Vector4))
        {
            return false;
        }

        return Equals((Vector4)other);
    }

    /// <summary>
    /// Returns true if the given vector is exactly equal to this vector.
    /// </summary>
    /// <param name="other">The vector to compare with the current instance.</param>
    /// <returns>True if the given vector is exactly equal to this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Equals(Vector4 other)
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    /// <summary>
    /// Makes this vector have a magnitude of 1.
    /// </summary>
    /// <param name="a">The vector to normalize.</param>
    /// <returns>The normalized vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Normalize(Vector4 a)
    {
        float num = Magnitude(a);
        if (num > 1E-05f)
        {
            return a / num;
        }

        return zero;
    }

    /// <summary>
    /// Makes this vector have a magnitude of 1.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Normalize()
    {
        float num = Magnitude(this);
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
    /// Dot Product of two vectors.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The dot product of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(Vector4 a, Vector4 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    /// <summary>
    /// Projects a vector onto another vector.
    /// </summary>
    /// <param name="a">The vector to project.</param>
    /// <param name="b">The normal vector to project onto.</param>
    /// <returns>The projected vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Project(Vector4 a, Vector4 b)
    {
        return b * (Dot(a, b) / Dot(b, b));
    }

    /// <summary>
    /// Returns the distance between a and b.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The distance between the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Distance(Vector4 a, Vector4 b)
    {
        return Magnitude(a - b);
    }

    /// <summary>
    /// Returns the length of the vector.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <returns>The length of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Magnitude(Vector4 a)
    {
        return (float)Math.Sqrt(Dot(a, a));
    }

    /// <summary>
    /// Returns a vector that is made from the smallest components of two vectors.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>A vector with the minimum components.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Min(Vector4 lhs, Vector4 rhs)
    {
        return new Vector4(Mathf.Min(lhs.x, rhs.x), Mathf.Min(lhs.y, rhs.y), Mathf.Min(lhs.z, rhs.z), Mathf.Min(lhs.w, rhs.w));
    }

    /// <summary>
    /// Returns a vector that is made from the largest components of two vectors.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>A vector with the maximum components.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Max(Vector4 lhs, Vector4 rhs)
    {
        return new Vector4(Mathf.Max(lhs.x, rhs.x), Mathf.Max(lhs.y, rhs.y), Mathf.Max(lhs.z, rhs.z), Mathf.Max(lhs.w, rhs.w));
    }

    /// <summary>
    /// Adds two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The sum of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator +(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    }

    /// <summary>
    /// Subtracts two vectors component-wise.
    /// </summary>
    /// <param name="a">The first vector.</param>
    /// <param name="b">The second vector.</param>
    /// <returns>The difference of the two vectors.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator -(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    }

    /// <summary>
    /// Negates a vector.
    /// </summary>
    /// <param name="a">The vector to negate.</param>
    /// <returns>The negated vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator -(Vector4 a)
    {
        return new Vector4(0f - a.x, 0f - a.y, 0f - a.z, 0f - a.w);
    }

    /// <summary>
    /// Multiplies a vector by a scalar.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <param name="d">The scalar value.</param>
    /// <returns>The scaled vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator *(Vector4 a, float d)
    {
        return new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
    }

    /// <summary>
    /// Multiplies a scalar by a vector.
    /// </summary>
    /// <param name="d">The scalar value.</param>
    /// <param name="a">The vector.</param>
    /// <returns>The scaled vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator *(float d, Vector4 a)
    {
        return new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
    }

    /// <summary>
    /// Divides a vector by a scalar.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <param name="d">The scalar value.</param>
    /// <returns>The divided vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator /(Vector4 a, float d)
    {
        return new Vector4(a.x / d, a.y / d, a.z / d, a.w / d);
    }

    /// <summary>
    /// Determines whether two vectors are approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>True if the vectors are approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(Vector4 lhs, Vector4 rhs)
    {
        float num = lhs.x - rhs.x;
        float num2 = lhs.y - rhs.y;
        float num3 = lhs.z - rhs.z;
        float num4 = lhs.w - rhs.w;
        float num5 = num * num + num2 * num2 + num3 * num3 + num4 * num4;
        return num5 < 9.99999944E-11f;
    }

    /// <summary>
    /// Determines whether two vectors are not approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side vector.</param>
    /// <param name="rhs">The right-hand side vector.</param>
    /// <returns>True if the vectors are not approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Vector4 lhs, Vector4 rhs)
    {
        return !(lhs == rhs);
    }

    /// <summary>
    /// Implicitly converts a Vector3 to a Vector4 by adding a w component of 0.
    /// </summary>
    /// <param name="v">The Vector3 to convert.</param>
    /// <returns>A Vector4 with x, y, z from Vector3 and w = 0.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector4(Vector3 v)
    {
        return new Vector4(v.x, v.y, v.z, 0f);
    }

    /// <summary>
    /// Implicitly converts a Vector4 to a Vector3 by taking the x, y, z components.
    /// </summary>
    /// <param name="v">The Vector4 to convert.</param>
    /// <returns>A Vector3 with x, y, z components from the Vector4.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector3(Vector4 v)
    {
        return new Vector3(v.x, v.y, v.z);
    }

    /// <summary>
    /// Implicitly converts a Vector2 to a Vector4 by adding z and w components of 0.
    /// </summary>
    /// <param name="v">The Vector2 to convert.</param>
    /// <returns>A Vector4 with x, y from Vector2 and z = w = 0.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector4(Vector2 v)
    {
        return new Vector4(v.x, v.y, 0f, 0f);
    }

    /// <summary>
    /// Implicitly converts a Vector4 to a Vector2 by taking the x and y components.
    /// </summary>
    /// <param name="v">The Vector4 to convert.</param>
    /// <returns>A Vector2 with x and y components from the Vector4.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector2(Vector4 v)
    {
        return new Vector2(v.x, v.y);
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

        return string.Format("({0}, {1}, {2}, {3})", x.ToString(format, formatProvider), y.ToString(format, formatProvider), z.ToString(format, formatProvider), w.ToString(format, formatProvider));
    }

    /// <summary>
    /// Returns the squared length of the vector.
    /// </summary>
    /// <param name="a">The vector.</param>
    /// <returns>The squared length of the vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SqrMagnitude(Vector4 a)
    {
        return Dot(a, a);
    }

    /// <summary>
    /// Returns the squared length of this vector.
    /// </summary>
    /// <returns>The squared length of this vector.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public float SqrMagnitude()
    {
        return Dot(this, this);
    }

}

