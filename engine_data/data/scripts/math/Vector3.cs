using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Vector3 : IEquatable<Vector3>, IFormattable
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


    private static readonly Vector3 zeroVector = new Vector3(0f, 0f, 0f);

    private static readonly Vector3 oneVector = new Vector3(1f, 1f, 1f);

    private static readonly Vector3 upVector = new Vector3(0f, 1f, 0f);

    private static readonly Vector3 downVector = new Vector3(0f, -1f, 0f);

    private static readonly Vector3 leftVector = new Vector3(-1f, 0f, 0f);

    private static readonly Vector3 rightVector = new Vector3(1f, 0f, 0f);

    private static readonly Vector3 forwardVector = new Vector3(0f, 0f, 1f);

    private static readonly Vector3 backVector = new Vector3(0f, 0f, -1f);

    private static readonly Vector3 positiveInfinityVector = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);

    private static readonly Vector3 negativeInfinityVector = new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

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
                default:
                    throw new IndexOutOfRangeException("Invalid Vector3 index!");
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
                default:
                    throw new IndexOutOfRangeException("Invalid Vector3 index!");
            }
        }
    }

    //
    // Summary:
    //     Returns this vector with a magnitude of 1 (Read Only).
    public Vector3 normalized
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
            return (float)Math.Sqrt(x * x + y * y + z * z);
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
            return x * x + y * y + z * z;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(0, 0, 0).
    public static Vector3 zero
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return zeroVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(1, 1, 1).
    public static Vector3 one
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return oneVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(0, 0, 1).
    public static Vector3 forward
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return forwardVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(0, 0, -1).
    public static Vector3 back
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return backVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(0, 1, 0).
    public static Vector3 up
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return upVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(0, -1, 0).
    public static Vector3 down
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return downVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(-1, 0, 0).
    public static Vector3 left
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return leftVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(1, 0, 0).
    public static Vector3 right
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return rightVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(float.PositiveInfinity, float.PositiveInfinity,
    //     float.PositiveInfinity).
    public static Vector3 positiveInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return positiveInfinityVector;
        }
    }

    //
    // Summary:
    //     Shorthand for writing Vector3(float.NegativeInfinity, float.NegativeInfinity,
    //     float.NegativeInfinity).
    public static Vector3 negativeInfinity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return negativeInfinityVector;
        }
    }

    //
    // Summary:
    //     Spherically interpolates between two vectors.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   t:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Slerp(Vector3 a, Vector3 b, float t)
    {
        // get cosine of angle between vectors (-1 -> 1)
		float CosAlpha = Vector3.Dot(a, b);
		// get angle (0 -> pi)
		float Alpha = Mathf.Acos(CosAlpha);
		// get sine of angle between vectors (0 -> 1)
		float SinAlpha = Mathf.Sin(Alpha);
		// this breaks down when SinAlpha = 0, i.e. Alpha = 0 or pi
		float t1 = Mathf.Sin((1.0f - t) * Alpha) / SinAlpha;
		float t2 = Mathf.Sin(t * Alpha) / SinAlpha;

		// interpolate src vectors
		return a * t1 + b * t2;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 SlerpClamped(Vector3 a, Vector3 b, float t)
    {
        t = Mathf.Clamp01(t);
        return Slerp(a, b, t);
    }

    //
    // Summary:
    //     Linearly interpolates between two points.
    //
    // Parameters:
    //   a:
    //     Start value, returned when t = 0.
    //
    //   b:
    //     End value, returned when t = 1.
    //
    //   t:
    //     Value used to interpolate between a and b.
    //
    // Returns:
    //     Interpolated value, equals to a + (b - a) * t.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
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
    public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t)
    {
        return new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }

    //
    // Summary:
    //     Calculate a position between the points specified by current and target, moving
    //     no farther than the distance specified by maxDistanceDelta.
    //
    // Parameters:
    //   current:
    //     The position to move from.
    //
    //   target:
    //     The position to move towards.
    //
    //   maxDistanceDelta:
    //     Distance to move current per call.
    //
    // Returns:
    //     The new position.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
    {
        float num = target.x - current.x;
        float num2 = target.y - current.y;
        float num3 = target.z - current.z;
        float num4 = num * num + num2 * num2 + num3 * num3;
        if (num4 == 0f || (maxDistanceDelta >= 0f && num4 <= maxDistanceDelta * maxDistanceDelta))
        {
            return target;
        }

        float num5 = (float)Math.Sqrt(num4);
        return new Vector3(current.x + num / num5 * maxDistanceDelta, current.y + num2 / num5 * maxDistanceDelta, current.z + num3 / num5 * maxDistanceDelta);
    }


    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
    {
        float num = 0f;
        float num2 = 0f;
        float num3 = 0f;
        smoothTime = Mathf.Max(0.0001f, smoothTime);
        float num4 = 2f / smoothTime;
        float num5 = num4 * deltaTime;
        float num6 = 1f / (1f + num5 + 0.48f * num5 * num5 + 0.235f * num5 * num5 * num5);
        float num7 = current.x - target.x;
        float num8 = current.y - target.y;
        float num9 = current.z - target.z;
        Vector3 vector = target;
        float num10 = maxSpeed * smoothTime;
        float num11 = num10 * num10;
        float num12 = num7 * num7 + num8 * num8 + num9 * num9;
        if (num12 > num11)
        {
            float num13 = (float)Math.Sqrt(num12);
            num7 = num7 / num13 * num10;
            num8 = num8 / num13 * num10;
            num9 = num9 / num13 * num10;
        }

        target.x = current.x - num7;
        target.y = current.y - num8;
        target.z = current.z - num9;
        float num14 = (currentVelocity.x + num4 * num7) * deltaTime;
        float num15 = (currentVelocity.y + num4 * num8) * deltaTime;
        float num16 = (currentVelocity.z + num4 * num9) * deltaTime;
        currentVelocity.x = (currentVelocity.x - num4 * num14) * num6;
        currentVelocity.y = (currentVelocity.y - num4 * num15) * num6;
        currentVelocity.z = (currentVelocity.z - num4 * num16) * num6;
        num = target.x + (num7 + num14) * num6;
        num2 = target.y + (num8 + num15) * num6;
        num3 = target.z + (num9 + num16) * num6;
        float num17 = vector.x - current.x;
        float num18 = vector.y - current.y;
        float num19 = vector.z - current.z;
        float num20 = num - vector.x;
        float num21 = num2 - vector.y;
        float num22 = num3 - vector.z;
        if (num17 * num20 + num18 * num21 + num19 * num22 > 0f)
        {
            num = vector.x;
            num2 = vector.y;
            num3 = vector.z;
            currentVelocity.x = (num - vector.x) / deltaTime;
            currentVelocity.y = (num2 - vector.y) / deltaTime;
            currentVelocity.z = (num3 - vector.z) / deltaTime;
        }

        return new Vector3(num, num2, num3);
    }

    //
    // Summary:
    //     Creates a new vector with given x, y, z components.
    //
    // Parameters:
    //   x:
    //
    //   y:
    //
    //   z:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector3(float x, float y, float z)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        //this._ = 0.0f;
    }

    //
    // Summary:
    //     Creates a new vector with given x, y components and sets z to zero.
    //
    // Parameters:
    //   x:
    //
    //   y:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Vector3(float x, float y)
    {
        this.x = x;
        this.y = y;
        this.z = 0f;
        //this._ = 0.0f;

    }

    //
    // Summary:
    //     Set x, y and z components of an existing Vector3.
    //
    // Parameters:
    //   newX:
    //
    //   newY:
    //
    //   newZ:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Set(float newX, float newY, float newZ)
    {
        x = newX;
        y = newY;
        z = newZ;
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
    public static Vector3 Scale(Vector3 a, Vector3 b)
    {
        return new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
    }

    //
    // Summary:
    //     Multiplies every component of this vector by the same component of scale.
    //
    // Parameters:
    //   scale:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Scale(Vector3 scale)
    {
        x *= scale.x;
        y *= scale.y;
        z *= scale.z;
    }

    //
    // Summary:
    //     Cross Product of two vectors.
    //
    // Parameters:
    //   lhs:
    //
    //   rhs:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Cross(Vector3 lhs, Vector3 rhs)
    {
        return new Vector3(lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override int GetHashCode()
    {
        return x.GetHashCode() ^ (y.GetHashCode() << 2) ^ (z.GetHashCode() >> 2);
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
        if (!(other is Vector3))
        {
            return false;
        }

        return Equals((Vector3)other);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Equals(Vector3 other)
    {
        return x == other.x && y == other.y && z == other.z;
    }

    //
    // Summary:
    //     Reflects a vector off the plane defined by a normal.
    //
    // Parameters:
    //   inDirection:
    //
    //   inNormal:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Reflect(Vector3 inDirection, Vector3 inNormal)
    {
        float num = -2f * Dot(inNormal, inDirection);
        return new Vector3(num * inNormal.x + inDirection.x, num * inNormal.y + inDirection.y, num * inNormal.z + inDirection.z);
    }

    //
    // Summary:
    //     Makes this vector have a magnitude of 1.
    //
    // Parameters:
    //   value:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Normalize(Vector3 value)
    {
        float num = Magnitude(value);
        if (num > 1E-05f)
        {
            return value / num;
        }

        return zero;
    }

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
    //   lhs:
    //
    //   rhs:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(Vector3 lhs, Vector3 rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    //
    // Summary:
    //     Projects a vector onto another vector.
    //
    // Parameters:
    //   vector:
    //
    //   onNormal:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 Project(Vector3 vector, Vector3 onNormal)
    {
        float num = Dot(onNormal, onNormal);
        if (num < Mathf.Epsilon)
        {
            return zero;
        }

        float num2 = Dot(vector, onNormal);
        return new Vector3(onNormal.x * num2 / num, onNormal.y * num2 / num, onNormal.z * num2 / num);
    }

    //
    // Summary:
    //     Projects a vector onto a plane defined by a normal orthogonal to the plane.
    //
    // Parameters:
    //   planeNormal:
    //     The direction from the vector towards the plane.
    //
    //   vector:
    //     The location of the vector above the plane.
    //
    // Returns:
    //     The location of the vector on the plane.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 ProjectOnPlane(Vector3 vector, Vector3 planeNormal)
    {
        float num = Dot(planeNormal, planeNormal);
        if (num < Mathf.Epsilon)
        {
            return vector;
        }

        float num2 = Dot(vector, planeNormal);
        return new Vector3(vector.x - planeNormal.x * num2 / num, vector.y - planeNormal.y * num2 / num, vector.z - planeNormal.z * num2 / num);
    }

    //
    // Summary:
    //     Calculates the angle between vectors from and.
    //
    // Parameters:
    //   from:
    //     The vector from which the angular difference is measured.
    //
    //   to:
    //     The vector to which the angular difference is measured.
    //
    // Returns:
    //     The angle in degrees between the two vectors.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Angle(Vector3 from, Vector3 to)
    {
        float num = (float)Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
        if (num < 1E-15f)
        {
            return 0f;
        }

        float num2 = Mathf.Clamp(Dot(from, to) / num, -1f, 1f);
        return (float)Math.Acos(num2) * 57.29578f;
    }

    //
    // Summary:
    //     Calculates the signed angle between vectors from and to in relation to axis.
    //
    //
    // Parameters:
    //   from:
    //     The vector from which the angular difference is measured.
    //
    //   to:
    //     The vector to which the angular difference is measured.
    //
    //   axis:
    //     A vector around which the other vectors are rotated.
    //
    // Returns:
    //     Returns the signed angle between from and to in degrees.
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
    {
        float num = Angle(from, to);
        float num2 = from.y * to.z - from.z * to.y;
        float num3 = from.z * to.x - from.x * to.z;
        float num4 = from.x * to.y - from.y * to.x;
        float num5 = Mathf.Sign(axis.x * num2 + axis.y * num3 + axis.z * num4);
        return num * num5;
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
    public static float Distance(Vector3 a, Vector3 b)
    {
        float num = a.x - b.x;
        float num2 = a.y - b.y;
        float num3 = a.z - b.z;
        return (float)Math.Sqrt(num * num + num2 * num2 + num3 * num3);
    }

    //
    // Summary:
    //     Returns a copy of vector with its magnitude clamped to maxLength.
    //
    // Parameters:
    //   vector:
    //
    //   maxLength:
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 ClampMagnitude(Vector3 vector, float maxLength)
    {
        float num = vector.sqrMagnitude;
        if (num > maxLength * maxLength)
        {
            float num2 = (float)Math.Sqrt(num);
            float num3 = vector.x / num2;
            float num4 = vector.y / num2;
            float num5 = vector.z / num2;
            return new Vector3(num3 * maxLength, num4 * maxLength, num5 * maxLength);
        }

        return vector;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Magnitude(Vector3 vector)
    {
        return (float)Math.Sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float SqrMagnitude(Vector3 vector)
    {
        return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
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
    public static Vector3 Min(Vector3 lhs, Vector3 rhs)
    {
        return new Vector3(Mathf.Min(lhs.x, rhs.x), Mathf.Min(lhs.y, rhs.y), Mathf.Min(lhs.z, rhs.z));
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
    public static Vector3 Max(Vector3 lhs, Vector3 rhs)
    {
        return new Vector3(Mathf.Max(lhs.x, rhs.x), Mathf.Max(lhs.y, rhs.y), Mathf.Max(lhs.z, rhs.z));
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator +(Vector3 a, Vector3 b)
    {
        return new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator -(Vector3 a, Vector3 b)
    {
        return new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator -(Vector3 a)
    {
        return new Vector3(0f - a.x, 0f - a.y, 0f - a.z);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator *(Vector3 a, float d)
    {
        return new Vector3(a.x * d, a.y * d, a.z * d);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator *(float d, Vector3 a)
    {
        return new Vector3(a.x * d, a.y * d, a.z * d);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Vector3 operator /(Vector3 a, float d)
    {
        return new Vector3(a.x / d, a.y / d, a.z / d);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(Vector3 lhs, Vector3 rhs)
    {
        float num = lhs.x - rhs.x;
        float num2 = lhs.y - rhs.y;
        float num3 = lhs.z - rhs.z;
        float num4 = num * num + num2 * num2 + num3 * num3;
        return num4 < 9.99999944E-11f;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Vector3 lhs, Vector3 rhs)
    {
        return !(lhs == rhs);
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

        return string.Format(formatProvider, "({0}, {1}, {2})", x.ToString(format, formatProvider), y.ToString(format, formatProvider), z.ToString(format, formatProvider));
    }

}

