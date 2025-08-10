using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Vector4 : IEquatable<Vector4>, IFormattable
{
    //
    // Summary:
    //     X component of the vector.
    public float x;

    //
    // Summary:
    //     Y component of the vector.
    public float y;

    //
    // Summary:
    //     Z component of the vector.
    public float z;

    //
    // Summary:
    //     W component of the vector.
    public float w;

    private static readonly Vector4 zeroVector = new Vector4(0f, 0f, 0f, 0f);

    private static readonly Vector4 oneVector = new Vector4(1f, 1f, 1f, 1f);

    private static readonly Vector4 positiveInfinityVector = new Vector4(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);

    private static readonly Vector4 negativeInfinityVector = new Vector4(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

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

    //
    // Summary:
    //     Returns this vector with a magnitude of 1 (Read Only).
    public Vector4 normalized
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return Normalize(this);
        }
    }

    //
    // Summary:
    //     Returns the length of this vector (Read Only).
    public float magnitude
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return (float)Math.Sqrt(Dot(this, this));
        }
    }

    //
    // Summary:
    //     Returns the squared length of this vector (Read Only).
    public float sqrMagnitude
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return Dot(this, this);
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector4(0,0,0,0).
    public static Vector4 zero
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return zeroVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector4(1,1,1,1).
    public static Vector4 one
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return oneVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector4(float.PositiveInfinity, float.PositiveInfinity,
    //     float.PositiveInfinity, float.PositiveInfinity).
    public static Vector4 positiveInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return positiveInfinityVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector4(float.NegativeInfinity, float.NegativeInfinity,
    //     float.NegativeInfinity, float.NegativeInfinity).
    public static Vector4 negativeInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return negativeInfinityVector;
        }
    }

    //
    // Summary:
    //     Creates a new vector with given x, y, z, w components.
    //
    // Parameters:
    //   x:
    //
    //   y:
    //
    //   z:
    //
    //   w:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y, float z, float w)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    //
    // Summary:
    //     Creates a new vector with given x, y, z components and sets w to zero.
    //
    // Parameters:
    //   x:
    //
    //   y:
    //
    //   z:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        w = 0f;
    }

    //
    // Summary:
    //     Creates a new vector with given x, y components and sets z and w to zero.
    //
    // Parameters:
    //   x:
    //
    //   y:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector4(float x, float y)
    {
        this.x = x;
        this.y = y;
        z = 0f;
        w = 0f;
    }

    //
    // Summary:
    //     Set x, y, z and w components of an existing Vector4.
    //
    // Parameters:
    //   newX:
    //
    //   newY:
    //
    //   newZ:
    //
    //   newW:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Set(float newX, float newY, float newZ, float newW)
    {
        x = newX;
        y = newY;
        z = newZ;
        w = newW;
    }

    //
    // Summary:
    //     Linearly interpolates between two vectors.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   t:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Lerp(Vector4 a, Vector4 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    //
    // Summary:
    //     Linearly interpolates between two vectors.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   t:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 LerpUnclamped(Vector4 a, Vector4 b, float t)
    {
        return new Vector4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    //
    // Summary:
    //     Moves a point current towards target.
    //
    // Parameters:
    //   current:
    //
    //   target:
    //
    //   maxDistanceDelta:
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

    //
    // Summary:
    //     Multiplies two vectors component-wise.
    //
    // Parameters:
    //   a:
    //
    //   b:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Scale(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
    }

    //
    // Summary:
    //     Multiplies every component of this vector by the same component of scale.
    //
    // Parameters:
    //   scale:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Scale(Vector4 scale)
    {
        x *= scale.x;
        y *= scale.y;
        z *= scale.z;
        w *= scale.w;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override int GetHashCode()
    {
        return x.GetHashCode() ^ (y.GetHashCode() << 2) ^ (z.GetHashCode() >> 2) ^ (w.GetHashCode() >> 1);
    }

    //
    // Summary:
    //     Returns true if the given vector is exactly equal to this vector.
    //
    // Parameters:
    //   other:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override bool Equals(object other)
    {
        if (!(other is Vector4))
        {
            return false;
        }

        return Equals((Vector4)other);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Equals(Vector4 other)
    {
        return x == other.x && y == other.y && z == other.z && w == other.w;
    }

    //
    // Parameters:
    //   a:
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

    //
    // Summary:
    //     Makes this vector have a magnitude of 1.
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

    //
    // Summary:
    //     Dot Product of two vectors.
    //
    // Parameters:
    //   a:
    //
    //   b:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(Vector4 a, Vector4 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    //
    // Summary:
    //     Projects a vector onto another vector.
    //
    // Parameters:
    //   a:
    //
    //   b:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Project(Vector4 a, Vector4 b)
    {
        return b * (Dot(a, b) / Dot(b, b));
    }

    //
    // Summary:
    //     Returns the distance between a and b.
    //
    // Parameters:
    //   a:
    //
    //   b:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Distance(Vector4 a, Vector4 b)
    {
        return Magnitude(a - b);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Magnitude(Vector4 a)
    {
        return (float)Math.Sqrt(Dot(a, a));
    }

    //
    // Summary:
    //     Returns a vector that is made from the smallest components of two vectors.
    //
    // Parameters:
    //   lhs:
    //
    //   rhs:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Min(Vector4 lhs, Vector4 rhs)
    {
        return new Vector4(Mathf.Min(lhs.x, rhs.x), Mathf.Min(lhs.y, rhs.y), Mathf.Min(lhs.z, rhs.z), Mathf.Min(lhs.w, rhs.w));
    }

    //
    // Summary:
    //     Returns a vector that is made from the largest components of two vectors.
    //
    // Parameters:
    //   lhs:
    //
    //   rhs:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 Max(Vector4 lhs, Vector4 rhs)
    {
        return new Vector4(Mathf.Max(lhs.x, rhs.x), Mathf.Max(lhs.y, rhs.y), Mathf.Max(lhs.z, rhs.z), Mathf.Max(lhs.w, rhs.w));
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator +(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator -(Vector4 a, Vector4 b)
    {
        return new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator -(Vector4 a)
    {
        return new Vector4(0f - a.x, 0f - a.y, 0f - a.z, 0f - a.w);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator *(Vector4 a, float d)
    {
        return new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator *(float d, Vector4 a)
    {
        return new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector4 operator /(Vector4 a, float d)
    {
        return new Vector4(a.x / d, a.y / d, a.z / d, a.w / d);
    }

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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Vector4 lhs, Vector4 rhs)
    {
        return !(lhs == rhs);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector4(Vector3 v)
    {
        return new Vector4(v.x, v.y, v.z, 0f);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector3(Vector4 v)
    {
        return new Vector3(v.x, v.y, v.z);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector4(Vector2 v)
    {
        return new Vector4(v.x, v.y, 0f, 0f);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static implicit operator Vector2(Vector4 v)
    {
        return new Vector2(v.x, v.y);
    }

    //
    // Summary:
    //     Returns a formatted string for this vector.
    //
    // Parameters:
    //   format:
    //     A numeric format string.
    //
    //   formatProvider:
    //     An object that specifies culture-specific formatting.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override string ToString()
    {
        return ToString(null, null);
    }

    //
    // Summary:
    //     Returns a formatted string for this vector.
    //
    // Parameters:
    //   format:
    //     A numeric format string.
    //
    //   formatProvider:
    //     An object that specifies culture-specific formatting.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format)
    {
        return ToString(format, null);
    }

    //
    // Summary:
    //     Returns a formatted string for this vector.
    //
    // Parameters:
    //   format:
    //     A numeric format string.
    //
    //   formatProvider:
    //     An object that specifies culture-specific formatting.
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

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SqrMagnitude(Vector4 a)
    {
        return Dot(a, a);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public float SqrMagnitude()
    {
        return Dot(this, this);
    }

}

