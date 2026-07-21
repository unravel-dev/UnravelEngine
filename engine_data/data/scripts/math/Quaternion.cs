using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

/// <summary>
/// Quaternions are used to represent rotations.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Quaternion : IEquatable<Quaternion>, IFormattable
{
    /// <summary>
    /// X component of the Quaternion. Don't modify this directly unless you know quaternions inside out.
    /// </summary>
    public float x;

    /// <summary>
    /// Y component of the Quaternion. Don't modify this directly unless you know quaternions inside out.
    /// </summary>
    public float y;

    /// <summary>
    /// Z component of the Quaternion. Don't modify this directly unless you know quaternions inside out.
    /// </summary>
    public float z;

    /// <summary>
    /// W component of the Quaternion. Do not directly modify quaternions.
    /// </summary>
    public float w;

    private static readonly Quaternion identityQuaternion = new Quaternion(0f, 0f, 0f, 1f);

    /// <summary>
    /// The identity rotation (Read Only).
    /// </summary>
    public static Quaternion identity
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return identityQuaternion;
        }
    }

    /// <summary>
    /// Returns or sets the euler angle representation of the rotation.
    /// </summary>
    public Vector3 eulerAngles
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return Internal_MakePositive(Internal_ToEulerRad(this) * 57.29578f);
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        set
        {
            this = Internal_FromEulerRad(value * (MathF.PI / 180f));
        }
    }

    /// <summary>
    /// Returns this quaternion with a magnitude of 1 (Read Only).
    /// </summary>
    public Quaternion normalized
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return Normalize(this);
        }
    }

    /// <summary>
    /// Creates a rotation which rotates from fromDirection to toDirection.
    /// </summary>
    /// <param name="fromDirection">The starting direction.</param>
    /// <param name="toDirection">The target direction.</param>
    /// <returns>A quaternion representing the rotation from fromDirection to toDirection.</returns>
    public static Quaternion FromToRotation(Vector3 fromDirection, Vector3 toDirection)
    {
        return internal_m2n_from_to_rotation(fromDirection, toDirection);
    }

    /// <summary>
    /// Returns the conjugate of a quaternion.
    /// </summary>
    /// <param name="q">The quaternion.</param>
    /// <returns>The conjugate of the quaternion.</returns>
    public static Quaternion Conjugate(Quaternion q)
    {
        Quaternion result = new Quaternion(-q.x, -q.y, -q.z, q.w);
        return result;
    }
    /// <summary>
    /// Returns the Inverse of rotation.
    /// </summary>
    /// <param name="q">The quaternion.</param>
    /// <returns>The inverse quaternion.</returns>
    public static Quaternion Inverse(Quaternion q)
    {
        return Conjugate(q) / Dot(q, q);
    }

    /// <summary>
    /// Spherically interpolates between quaternions a and b by ratio t. The parameter t is clamped to the range [0, 1].
    /// </summary>
    /// <param name="a">Start value, returned when t = 0.</param>
    /// <param name="b">End value, returned when t = 1.</param>
    /// <param name="t">Interpolation ratio.</param>
    /// <returns>A quaternion spherically interpolated between quaternions a and b.</returns>
    public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
    {
        t = Mathf.Clamp01(t);
        return SlerpUnclamped(a, b, t);
		
    }

    /// <summary>
    /// Spherically interpolates between a and b by t. The parameter t is not clamped.
    /// </summary>
    /// <param name="x">Start value, returned when t = 0.</param>
    /// <param name="y">End value, returned when t = 1.</param>
    /// <param name="a">Interpolation ratio (not clamped).</param>
    /// <returns>A quaternion spherically interpolated between quaternions x and y.</returns>
    public static Quaternion SlerpUnclamped(Quaternion x, Quaternion y, float a)
    {
        Quaternion z = y;

        float cosTheta = Quaternion.Dot(x, y);

		// If cosTheta < 0, the interpolation will take the long way around the sphere.
		// To fix this, one quat must be negated.
		if(cosTheta < 0.0f)
		{
			z = -y;
			cosTheta = -cosTheta;
		}

		// Perform a linear interpolation when cosTheta is close to 1 to avoid side effect of sin(angle) becoming a zero denominator
		if(cosTheta > 1.0f - Mathf.kEpsilon)
		{
            // Linear interpolation
            Quaternion result;
            result.x = Mathf.LerpUnclamped(x.x, z.x, a);
            result.y = Mathf.LerpUnclamped(x.y, z.y, a);
            result.z = Mathf.LerpUnclamped(x.z, z.z, a);
            result.w = Mathf.LerpUnclamped(x.w, z.w, a);
            return result;
		}
		else
		{
			// Essential Mathematics, page 467
			float angle = Mathf.Acos(cosTheta);
			return (Mathf.Sin((1.0f - a) * angle) * x + Mathf.Sin(a * angle) * z) / Mathf.Sin(angle);
		}
    }


    /// <summary>
    /// Interpolates between a and b by t and normalizes the result afterwards. The parameter t is clamped to the range [0, 1].
    /// </summary>
    /// <param name="a">Start value, returned when t = 0.</param>
    /// <param name="b">End value, returned when t = 1.</param>
    /// <param name="t">Interpolation ratio.</param>
    /// <returns>A quaternion interpolated between quaternions a and b.</returns>
    public static Quaternion Lerp(Quaternion a, Quaternion b, float t)
    {        
        t = Mathf.Clamp01(t);
        return LerpUnclamped(a, b, t);
    }

    /// <summary>
    /// Interpolates between a and b by t and normalizes the result afterwards. The parameter t is not clamped.
    /// </summary>
    /// <param name="a">Start value, returned when t = 0.</param>
    /// <param name="b">End value, returned when t = 1.</param>
    /// <param name="t">Interpolation ratio (not clamped).</param>
    /// <returns>A quaternion interpolated between quaternions a and b.</returns>
    public static Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t)
    {
        return a * (1.0f - t) + (b * t);
    }

    /// <summary>
    /// Creates a quaternion from Euler angles in radians.
    /// </summary>
    /// <param name="euler">The Euler angles in radians.</param>
    /// <returns>A quaternion representing the rotation.</returns>
    private static Quaternion Internal_FromEulerRad(Vector3 euler)
    {
        return internal_m2n_from_euler_rad(euler);
    }

    /// <summary>
    /// Converts a quaternion to Euler angles in radians.
    /// </summary>
    /// <param name="rotation">The quaternion.</param>
    /// <returns>The Euler angles in radians.</returns>
    private static Vector3 Internal_ToEulerRad(Quaternion rotation)
    {
        return internal_m2n_to_euler_rad(rotation);
    }


    /// <summary>
    /// Creates a rotation which rotates angle degrees around axis.
    /// </summary>
    /// <param name="angle">The angle in degrees.</param>
    /// <param name="axis">The axis of rotation.</param>
    /// <returns>A quaternion representing the rotation.</returns>
    public static Quaternion AngleAxis(float angle, Vector3 axis)
    {
        float rad = angle * (MathF.PI / 180f);
        return internal_m2n_angle_axis(rad, axis);
    }

    /// <summary>
    /// Creates a rotation with the specified forward and upwards directions.
    /// </summary>
    /// <param name="forward">The direction to look in.</param>
    /// <param name="upwards">The vector that defines in which direction up is.</param>
    /// <returns>A quaternion representing the rotation.</returns>
    public static Quaternion LookRotation(Vector3 forward, Vector3 upwards)
    {
        return internal_m2n_look_rotation(forward, upwards);
    }

    /// <summary>
    /// Creates a rotation with the specified forward direction, using Vector3.up as the up direction.
    /// </summary>
    /// <param name="forward">The direction to look in.</param>
    /// <returns>A quaternion representing the rotation.</returns>
    public static Quaternion LookRotation(Vector3 forward)
    {
        return LookRotation(forward, Vector3.up);
    }

    /// <summary>
    /// Constructs new Quaternion with given x,y,z,w components.
    /// </summary>
    /// <param name="x">The x component.</param>
    /// <param name="y">The y component.</param>
    /// <param name="z">The z component.</param>
    /// <param name="w">The w component.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public Quaternion(float x, float y, float z, float w)
    {
        this.x = x;
        this.y = y;
        this.z = z;
        this.w = w;
    }

    /// <summary>
    /// Set x, y, z and w components of an existing Quaternion.
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
    /// Divides a quaternion by a scalar.
    /// </summary>
    /// <param name="lhs">The quaternion.</param>
    /// <param name="rhs">The scalar value.</param>
    /// <returns>The divided quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion operator /(Quaternion lhs, float rhs)
    {
        return new Quaternion(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs);
    }

    /// <summary>
    /// Multiplies a quaternion by a scalar.
    /// </summary>
    /// <param name="lhs">The quaternion.</param>
    /// <param name="rhs">The scalar value.</param>
    /// <returns>The scaled quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion operator *(Quaternion lhs, float rhs)
    {
        return new Quaternion(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
    }

    /// <summary>
    /// Multiplies a scalar by a quaternion.
    /// </summary>
    /// <param name="lhs">The scalar value.</param>
    /// <param name="rhs">The quaternion.</param>
    /// <returns>The scaled quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion operator *(float lhs, Quaternion rhs)
    {
        return new Quaternion(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z, lhs * rhs.w);
    }

    /// <summary>
    /// Adds two quaternions component-wise.
    /// </summary>
    /// <param name="lhs">The left-hand side quaternion.</param>
    /// <param name="rhs">The right-hand side quaternion.</param>
    /// <returns>The sum of the two quaternions.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion operator +(Quaternion lhs, Quaternion rhs)
    {
        return new Quaternion(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
    }

    /// <summary>
    /// Multiplies two quaternions.
    /// </summary>
    /// <param name="lhs">The left-hand side quaternion.</param>
    /// <param name="rhs">The right-hand side quaternion.</param>
    /// <returns>The product of the two quaternions.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion operator *(Quaternion lhs, Quaternion rhs)
    {
        return new Quaternion(lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y, lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z, lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x, lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z);
    }

    /// <summary>
    /// Rotates a point by a quaternion.
    /// </summary>
    /// <param name="rotation">The rotation quaternion.</param>
    /// <param name="point">The point to rotate.</param>
    /// <returns>The rotated point.</returns>
    public static Vector3 operator *(Quaternion rotation, Vector3 point)
    {
        float num = rotation.x * 2f;
        float num2 = rotation.y * 2f;
        float num3 = rotation.z * 2f;
        float num4 = rotation.x * num;
        float num5 = rotation.y * num2;
        float num6 = rotation.z * num3;
        float num7 = rotation.x * num2;
        float num8 = rotation.x * num3;
        float num9 = rotation.y * num3;
        float num10 = rotation.w * num;
        float num11 = rotation.w * num2;
        float num12 = rotation.w * num3;
        Vector3 result = default(Vector3);
        result.x = (1f - (num5 + num6)) * point.x + (num7 - num12) * point.y + (num8 + num11) * point.z;
        result.y = (num7 + num12) * point.x + (1f - (num4 + num6)) * point.y + (num9 - num10) * point.z;
        result.z = (num8 - num11) * point.x + (num9 + num10) * point.y + (1f - (num4 + num5)) * point.z;
        return result;
    }

    /// <summary>
    /// Negates a quaternion.
    /// </summary>
    /// <param name="a">The quaternion to negate.</param>
    /// <returns>The negated quaternion.</returns>
    public static Quaternion operator -(Quaternion a)
    {
        return new Quaternion(-a.x, -a.y, -a.z, -a.w);
    }


    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static bool IsEqualUsingDot(float dot)
    {
        return dot > 0.999999f;
    }

    /// <summary>
    /// Determines whether two quaternions are approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side quaternion.</param>
    /// <param name="rhs">The right-hand side quaternion.</param>
    /// <returns>True if the quaternions are approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator ==(Quaternion lhs, Quaternion rhs)
    {
        return IsEqualUsingDot(Dot(lhs, rhs));
    }

    /// <summary>
    /// Determines whether two quaternions are not approximately equal.
    /// </summary>
    /// <param name="lhs">The left-hand side quaternion.</param>
    /// <param name="rhs">The right-hand side quaternion.</param>
    /// <returns>True if the quaternions are not approximately equal; otherwise, false.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static bool operator !=(Quaternion lhs, Quaternion rhs)
    {
        return !(lhs == rhs);
    }

    /// <summary>
    /// The dot product between two rotations.
    /// </summary>
    /// <param name="a">The first quaternion.</param>
    /// <param name="b">The second quaternion.</param>
    /// <returns>The dot product of the two quaternions.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Dot(Quaternion a, Quaternion b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    /// <summary>
    /// Creates a rotation with the specified forward direction, using Vector3.up as the up direction.
    /// </summary>
    /// <param name="view">The direction to look in.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void SetLookRotation(Vector3 view)
    {
        Vector3 up = Vector3.up;
        SetLookRotation(view, up);
    }

    /// <summary>
    /// Creates a rotation with the specified forward and upwards directions.
    /// </summary>
    /// <param name="view">The direction to look in.</param>
    /// <param name="up">The vector that defines in which direction up is.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void SetLookRotation(Vector3 view, Vector3 up)
    {
        this = LookRotation(view, up);
    }

    /// <summary>
    /// Returns the angle in degrees between two rotations a and b.
    /// </summary>
    /// <param name="a">The first quaternion.</param>
    /// <param name="b">The second quaternion.</param>
    /// <returns>The angle in degrees between the two rotations.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static float Angle(Quaternion a, Quaternion b)
    {
        float num = Mathf.Min(Mathf.Abs(Dot(a, b)), 1f);
        return IsEqualUsingDot(num) ? 0f : (Mathf.Acos(num) * 2f * 57.29578f);
    }

    private static Vector3 Internal_MakePositive(Vector3 euler)
    {
        float num = -0.005729578f;
        float num2 = 360f + num;
        if (euler.x < num)
        {
            euler.x += 360f;
        }
        else if (euler.x > num2)
        {
            euler.x -= 360f;
        }

        if (euler.y < num)
        {
            euler.y += 360f;
        }
        else if (euler.y > num2)
        {
            euler.y -= 360f;
        }

        if (euler.z < num)
        {
            euler.z += 360f;
        }
        else if (euler.z > num2)
        {
            euler.z -= 360f;
        }

        return euler;
    }

    /// <summary>
    /// Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis; applied in that order.
    /// </summary>
    /// <param name="x">Rotation around the x axis in degrees.</param>
    /// <param name="y">Rotation around the y axis in degrees.</param>
    /// <param name="z">Rotation around the z axis in degrees.</param>
    /// <returns>A quaternion representing the rotation.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion Euler(float x, float y, float z)
    {
        return Internal_FromEulerRad(new Vector3(x, y, z) * (MathF.PI / 180f));
    }

    /// <summary>
    /// Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis.
    /// </summary>
    /// <param name="euler">The Euler angles in degrees (x, y, z).</param>
    /// <returns>A quaternion representing the rotation.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion Euler(Vector3 euler)
    {
        return Internal_FromEulerRad(euler * (MathF.PI / 180f));
    }


    /// <summary>
    /// Creates a rotation which rotates from fromDirection to toDirection.
    /// </summary>
    /// <param name="fromDirection">The starting direction.</param>
    /// <param name="toDirection">The target direction.</param>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void SetFromToRotation(Vector3 fromDirection, Vector3 toDirection)
    {
        this = FromToRotation(fromDirection, toDirection);
    }

    /// <summary>
    /// Rotates a rotation from towards to.
    /// </summary>
    /// <param name="from">The starting rotation.</param>
    /// <param name="to">The target rotation.</param>
    /// <param name="maxDegreesDelta">The maximum angle in degrees to rotate.</param>
    /// <returns>The rotated quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
    {
        float num = Angle(from, to);
        if (num == 0f)
        {
            return to;
        }

        return SlerpUnclamped(from, to, Mathf.Min(1f, maxDegreesDelta / num));
    }

    /// <summary>
    /// Converts this quaternion to one with the same orientation but with a magnitude of 1.
    /// </summary>
    /// <param name="q">The quaternion to normalize.</param>
    /// <returns>The normalized quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public static Quaternion Normalize(Quaternion q)
    {
        float num = Mathf.Sqrt(Dot(q, q));
        if (num < Mathf.Epsilon)
        {
            return identity;
        }

        return new Quaternion(q.x / num, q.y / num, q.z / num, q.w / num);
    }

    /// <summary>
    /// Normalizes this quaternion to have a magnitude of 1.
    /// </summary>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public void Normalize()
    {
        this = Normalize(this);
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
    /// Returns true if the given quaternion is exactly equal to this quaternion.
    /// </summary>
    /// <param name="other">The object to compare with the current instance.</param>
    /// <returns>True if the given quaternion is exactly equal to this quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override bool Equals(object other)
    {
        if (!(other is Quaternion))
        {
            return false;
        }

        return Equals((Quaternion)other);
    }

    /// <summary>
    /// Returns true if the given quaternion is exactly equal to this quaternion.
    /// </summary>
    /// <param name="other">The quaternion to compare with the current instance.</param>
    /// <returns>True if the given quaternion is exactly equal to this quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public bool Equals(Quaternion other)
    {
        return x.Equals(other.x) && y.Equals(other.y) && z.Equals(other.z) && w.Equals(other.w);
    }

    /// <summary>
    /// Returns a formatted string for this quaternion.
    /// </summary>
    /// <returns>A formatted string representation of the quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public override string ToString()
    {
        return ToString(null, null);
    }

    /// <summary>
    /// Returns a formatted string for this quaternion.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <returns>A formatted string representation of the quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format)
    {
        return ToString(format, null);
    }

    /// <summary>
    /// Returns a formatted string for this quaternion.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <param name="formatProvider">An object that specifies culture-specific formatting.</param>
    /// <returns>A formatted string representation of the quaternion.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format, IFormatProvider formatProvider)
    {
        if (string.IsNullOrEmpty(format))
        {
            format = "F5";
        }

        if (formatProvider == null)
        {
            formatProvider = CultureInfo.InvariantCulture.NumberFormat;
        }

        return string.Format("({0}, {1}, {2}, {3})", x.ToString(format, formatProvider), y.ToString(format, formatProvider), z.ToString(format, formatProvider), w.ToString(format, formatProvider));
    }

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Quaternion internal_m2n_from_to_rotation(Vector3 fromDirection, Vector3 toDirection);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Quaternion internal_m2n_from_euler_rad(Vector3 euler);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Vector3 internal_m2n_to_euler_rad(Quaternion rotation);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Quaternion internal_m2n_angle_axis(float angle, Vector3 axis);

    [MethodImpl(MethodImplOptions.InternalCall)]
    private static extern Quaternion internal_m2n_look_rotation(Vector3 forward, Vector3 upwards);
}

