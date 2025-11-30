using System;
using System.Numerics;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Bounds : IEquatable<Bounds>
{
    /// <summary>
    /// The minimum corner of the box.
    /// </summary>
    public Vector3 Min;
    
    /// <summary>
    /// The maximum corner of the box.
    /// </summary>
    public Vector3 Max;
    
    /// <summary>
    /// An empty bounds box.
    /// </summary>
    public static readonly Bounds Empty = new Bounds(Vector3.one * float.MaxValue, Vector3.one * float.MinValue);

    /// <summary>
    /// Creates a box from two corners.
    /// </summary>
    /// <param name="min">The minimum corner.</param>
    /// <param name="max">The maximum corner.</param>
    public Bounds(Vector3 min, Vector3 max)
    {
        Min = min;
        Max = max;
    }

    /// <summary>
    /// Creates a box by specifying each coordinate.
    /// </summary>
    /// <param name="xMin">The minimum x coordinate.</param>
    /// <param name="yMin">The minimum y coordinate.</param>
    /// <param name="zMin">The minimum z coordinate.</param>
    /// <param name="xMax">The maximum x coordinate.</param>
    /// <param name="yMax">The maximum y coordinate.</param>
    /// <param name="zMax">The maximum z coordinate.</param>
    public Bounds(
        float xMin, float yMin, float zMin,
        float xMax, float yMax, float zMax)
    {
        Min = new Vector3(xMin, yMin, zMin);
        Max = new Vector3(xMax, yMax, zMax);
    }

    /// <summary>
    /// Resets to an "unpopulated" state—ready for Grow/Encapsulate.
    /// </summary>
    public void Reset()
    {
        Min = new Vector3(float.MaxValue, float.MaxValue, float.MaxValue);
        Max = new Vector3(-float.MaxValue, -float.MaxValue, -float.MaxValue);
    }

    /// <summary>
    /// True if Reset() has been called (i.e. not both corners = zero).
    /// </summary>
    /// <returns>True if the bounds have been populated; otherwise, false.</returns>
    public bool IsPopulated()
    {
        // if still at Reset() values, Min components are all float.MaxValue
        return Min.x != float.MaxValue ||
               Min.y != float.MaxValue ||
               Min.z != float.MaxValue ||
               Max.x != -float.MaxValue ||
               Max.y != -float.MaxValue ||
               Max.z != -float.MaxValue;
    }

    /// <summary>
    /// True if the box has zero volume (within epsilon).
    /// </summary>
    /// <param name="epsilon">The tolerance value.</param>
    /// <returns>True if the box has zero volume; otherwise, false.</returns>
    public bool IsDegenerate(float epsilon = float.Epsilon)
        => MathF.Abs(Max.x - Min.x) < epsilon
        && MathF.Abs(Max.y - Min.y) < epsilon
        && MathF.Abs(Max.z - Min.z) < epsilon;

    /// <summary>
    /// Expand to include this point.
    /// </summary>
    /// <param name="point">The point to include.</param>
    public void AddPoint(Vector3 point)
    {
        if (point.x < Min.x) Min.x = point.x;
        if (point.y < Min.y) Min.y = point.y;
        if (point.z < Min.z) Min.z = point.z;
        if (point.x > Max.x) Max.x = point.x;
        if (point.y > Max.y) Max.y = point.y;
        if (point.z > Max.z) Max.z = point.z;
    }

    /// <summary>
    /// True if the point is inside [Min,Max].
    /// </summary>
    /// <param name="p">The point to test.</param>
    /// <returns>True if the point is inside the bounds; otherwise, false.</returns>
    public bool ContainsPoint(Vector3 p)
        => p.x >= Min.x && p.x <= Max.x
        && p.y >= Min.y && p.y <= Max.y
        && p.z >= Min.z && p.z <= Max.z;

    /// <summary>
    /// Contains with per-axis tolerance.
    /// </summary>
    /// <param name="p">The point to test.</param>
    /// <param name="tol">The tolerance per axis.</param>
    /// <returns>True if the point is inside the bounds with tolerance; otherwise, false.</returns>
    public bool ContainsPoint(Vector3 p, Vector3 tol)
        => p.x >= Min.x - tol.x && p.x <= Max.x + tol.x
        && p.y >= Min.y - tol.y && p.y <= Max.y + tol.y
        && p.z >= Min.z - tol.z && p.z <= Max.z + tol.z;

    /// <summary>
    /// Contains with uniform tolerance.
    /// </summary>
    /// <param name="p">The point to test.</param>
    /// <param name="tol">The uniform tolerance value.</param>
    /// <returns>True if the point is inside the bounds with tolerance; otherwise, false.</returns>
    public bool ContainsPoint(Vector3 p, float tol)
        => ContainsPoint(p, new Vector3(tol, tol, tol));

    /// <summary>
    /// The size of the box = Max − Min.
    /// </summary>
    /// <returns>The dimensions of the box.</returns>
    public Vector3 GetDimensions() => Max - Min;

    /// <summary>
    /// Center = (Min + Max) / 2.
    /// </summary>
    /// <returns>The center point of the box.</returns>
    public Vector3 GetCenter() => (Min + Max) * 0.5f;

    /// <summary>
    /// Extents = half-dimensions.
    /// </summary>
    /// <returns>The extents (half-dimensions) of the box.</returns>
    public Vector3 GetExtents() => (Max - Min) * 0.5f;

    /// <summary>
    /// Grow/shrink all sides by a uniform amount.
    /// </summary>
    /// <param name="amount">The amount to inflate (positive) or deflate (negative) by.</param>
    public void Inflate(float amount)
    {
        Min -= new Vector3(amount, amount, amount);
        Max += new Vector3(amount, amount, amount);
    }

    /// <summary>
    /// Grow/shrink all sides by per-axis amounts.
    /// </summary>
    /// <param name="amount">The amount to inflate (positive) or deflate (negative) per axis.</param>
    public void Inflate(Vector3 amount)
    {
        Min -= amount;
        Max += amount;
    }

    /// <summary>
    /// The eight corners in the same order as your C++ code.
    /// </summary>
    /// <returns>An array of the eight corner points.</returns>
    public Vector3[] GetCorners()
        => new[]
        {
            new Vector3(Min.x, Min.y, Min.z),
            new Vector3(Max.x, Min.y, Min.z),
            new Vector3(Min.x, Max.y, Min.z),
            new Vector3(Max.x, Max.y, Min.z),
            new Vector3(Min.x, Min.y, Max.z),
            new Vector3(Max.x, Min.y, Max.z),
            new Vector3(Min.x, Max.y, Max.z),
            new Vector3(Max.x, Max.y, Max.z),
        };

    /// <summary>
    /// AABB-AABB overlap test.
    /// </summary>
    /// <param name="b">The other bounds to test against.</param>
    /// <returns>True if the bounds overlap; otherwise, false.</returns>
    public bool Intersect(Bounds b)
        => Min.x <= b.Max.x && Min.y <= b.Max.y && Min.z <= b.Max.z
        && Max.x >= b.Min.x && Max.y >= b.Min.y && Max.z >= b.Min.z;

    /// <summary>
    /// Overlap + full containment test. Returns true if they overlap; sets contained=true if b is entirely inside this.
    /// </summary>
    /// <param name="b">The other bounds to test against.</param>
    /// <param name="contained">Set to true if b is entirely inside this bounds; otherwise, false.</param>
    /// <returns>True if the bounds overlap; otherwise, false.</returns>
    public bool Intersect(Bounds b, out bool contained)
    {
        // start assuming full containment
        contained = true;

        if (b.Min.x < Min.x || b.Min.x > Max.x) contained = false;
        else if (b.Min.y < Min.y || b.Min.y > Max.y) contained = false;
        else if (b.Min.z < Min.z || b.Min.z > Max.z) contained = false;
        else if (b.Max.x < Min.x || b.Max.x > Max.x) contained = false;
        else if (b.Max.y < Min.y || b.Max.y > Max.y) contained = false;
        else if (b.Max.z < Min.z || b.Max.z > Max.z) contained = false;

        // if fully contained, we still say “overlap”
        if (contained) return true;

        // otherwise do the normal overlap test
        return Intersect(b);
    }

    /// <summary>
    /// Overlap + return the intersection box.
    /// </summary>
    /// <param name="b">The other bounds to test against.</param>
    /// <param name="intersection">The intersection bounds if they overlap; otherwise, default bounds.</param>
    /// <returns>True if the bounds overlap; otherwise, false.</returns>
    public bool Intersect(Bounds b, out Bounds intersection)
    {
        intersection.Min = Vector3.Max(Min, b.Min);
        intersection.Max = Vector3.Min(Max, b.Max);

        if (intersection.Min.x > intersection.Max.x ||
            intersection.Min.y > intersection.Max.y ||
            intersection.Min.z > intersection.Max.z)
        {
            intersection = default(Bounds);
            return false;
        }

        return true;
    }

    /// <summary>
    /// Overlap with a tolerance vector.
    /// </summary>
    /// <param name="b">The other bounds to test against.</param>
    /// <param name="tol">The tolerance vector.</param>
    /// <returns>True if the bounds overlap with tolerance; otherwise, false.</returns>
    public bool Intersect(Bounds b, Vector3 tol)
    {
        return 
        (Min - tol).x <= (b.Max + tol).x && 
        (Min - tol).y <= (b.Max + tol).y && 
        (Min - tol).z <= (b.Max + tol).z && 
        (Max + tol).x >= (b.Min - tol).x && 
        (Max + tol).y >= (b.Min - tol).y && 
        (Max + tol).z >= (b.Min - tol).z;
    }
       
    // helper to check one axis
    bool Slab(float o, float d, float mn, float mx, ref float t0, ref float t1)
    {
        if (MathF.Abs(d) > float.Epsilon)
        {
            float inv = 1f / d;
            float t1n = (mn - o) * inv;
            float t2n = (mx - o) * inv;
            if (t1n > t2n) (t1n, t2n) = (t2n, t1n);
            t0 = MathF.Max(t0, t1n);
            t1 = MathF.Min(t1, t2n);
            return t0 <= t1;
        }
        else
        {
            return o >= mn && o <= mx;
        }
    }

    /// <summary>
    /// Ray vs AABB (slab method). Returns true if hit; t = entry time in [0,1] if restrictRange.
    /// </summary>
    /// <param name="origin">The origin of the ray.</param>
    /// <param name="dir">The direction of the ray.</param>
    /// <param name="t">The intersection parameter along the ray.</param>
    /// <param name="restrictRange">If true, restricts t to [0,1].</param>
    /// <returns>True if the ray intersects the bounds; otherwise, false.</returns>
    public bool Intersect(Vector3 origin, Vector3 dir, out float t, bool restrictRange = true)
    {
        t = 0f;
        float tMin = float.MinValue;
        float tMax = float.MaxValue;

        if (!Slab(origin.x, dir.x, Min.x, Max.x, ref tMin, ref tMax)) return false;
        if (!Slab(origin.y, dir.y, Min.y, Max.y, ref tMin, ref tMax)) return false;
        if (!Slab(origin.z, dir.z, Min.z, Max.z, ref tMin, ref tMax)) return false;

        t = tMin > 0 ? tMin : tMax;
        return t >= 0 && (!restrictRange || t <= 1f);
    }

    /// <summary>
    /// Translates the bounds by a vector.
    /// </summary>
    /// <param name="b">The bounds.</param>
    /// <param name="shift">The translation vector.</param>
    /// <returns>The translated bounds.</returns>
    public static Bounds operator +(Bounds b, Vector3 shift)
        => new Bounds(b.Min + shift, b.Max + shift);

    /// <summary>
    /// Translates the bounds by the negative of a vector.
    /// </summary>
    /// <param name="b">The bounds.</param>
    /// <param name="shift">The translation vector.</param>
    /// <returns>The translated bounds.</returns>
    public static Bounds operator -(Bounds b, Vector3 shift)
        => new Bounds(b.Min - shift, b.Max - shift);

    /// <summary>
    /// Scales the bounds by a scalar.
    /// </summary>
    /// <param name="b">The bounds.</param>
    /// <param name="s">The scalar value.</param>
    /// <returns>The scaled bounds.</returns>
    public static Bounds operator *(Bounds b, float s)
        => new Bounds(b.Min * s, b.Max * s);

    /// <summary>
    /// Determines whether two bounds are equal.
    /// </summary>
    /// <param name="a">The first bounds.</param>
    /// <param name="b">The second bounds.</param>
    /// <returns>True if the bounds are equal; otherwise, false.</returns>
    public static bool operator ==(Bounds a, Bounds b)
        => a.Min == b.Min && a.Max == b.Max;

    /// <summary>
    /// Determines whether two bounds are not equal.
    /// </summary>
    /// <param name="a">The first bounds.</param>
    /// <param name="b">The second bounds.</param>
    /// <returns>True if the bounds are not equal; otherwise, false.</returns>
    public static bool operator !=(Bounds a, Bounds b)
        => !(a == b);

    /// <summary>
    /// Returns true if the given object is equal to this bounds.
    /// </summary>
    /// <param name="obj">The object to compare with the current instance.</param>
    /// <returns>True if the given object is equal to this bounds; otherwise, false.</returns>
    public override bool Equals(object obj)
        => obj is Bounds b && this == b;

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    public override int GetHashCode()
        => HashCode.Combine(Min, Max);

    /// <summary>
    /// Returns true if the given bounds is equal to this bounds.
    /// </summary>
    /// <param name="other">The bounds to compare with the current instance.</param>
    /// <returns>True if the given bounds is equal to this bounds; otherwise, false.</returns>
    public bool Equals(Bounds other)
        => this == other;
}
