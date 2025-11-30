using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;


/// <summary>
/// Represents a size with width and height components.
/// </summary>
/// <typeparam name="T">The type of the width and height components, must implement IComparable&lt;T&gt;.</typeparam>
[StructLayout(LayoutKind.Sequential /*, Pack = 1*/)]
public struct Size<T>
    where T : IComparable<T>
{
    /// <summary>
    /// The width component.
    /// </summary>
    public T Width;
    
    /// <summary>
    /// The height component.
    /// </summary>
    public T Height;

    /// <summary>
    /// Creates a new Size with the specified width and height.
    /// </summary>
    /// <param name="width">The width.</param>
    /// <param name="height">The height.</param>
    public Size(T width, T height)
    {
        Width  = width;
        Height = height;
    }

    /// <summary>
    /// Determines whether the size is valid (both width and height are not default values).
    /// </summary>
    /// <returns>True if the size is valid; otherwise, false.</returns>
    public bool IsValid()
        => !Width.Equals(default(T)) && !Height.Equals(default(T));

    /// <summary>
    /// Returns true if the given object is equal to this size.
    /// </summary>
    /// <param name="obj">The object to compare with the current instance.</param>
    /// <returns>True if the given object is equal to this size; otherwise, false.</returns>
    public override bool Equals(object obj)
        => obj is Size<T> o && Width.Equals(o.Width) && Height.Equals(o.Height);

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    public override int GetHashCode()
        => HashCode.Combine(Width, Height);

    /// <summary>
    /// Determines whether two sizes are equal.
    /// </summary>
    /// <param name="a">The first size.</param>
    /// <param name="b">The second size.</param>
    /// <returns>True if the sizes are equal; otherwise, false.</returns>
    public static bool operator ==(Size<T> a, Size<T> b) => a.Width.Equals(b.Width) && a.Height.Equals(b.Height);
    
    /// <summary>
    /// Determines whether two sizes are not equal.
    /// </summary>
    /// <param name="a">The first size.</param>
    /// <param name="b">The second size.</param>
    /// <returns>True if the sizes are not equal; otherwise, false.</returns>
    public static bool operator !=(Size<T> a, Size<T> b) => !(a == b);

    /// <summary>
    /// Determines whether the first size is less than the second size.
    /// </summary>
    /// <param name="a">The first size.</param>
    /// <param name="b">The second size.</param>
    /// <returns>True if a is less than b; otherwise, false.</returns>
    public static bool operator <(Size<T> a, Size<T> b)
    {
        int w = a.Width.CompareTo(b.Width);
        return w < 0 || (w == 0 && a.Height.CompareTo(b.Height) < 0);
    }

    /// <summary>
    /// Determines whether the first size is greater than the second size.
    /// </summary>
    /// <param name="a">The first size.</param>
    /// <param name="b">The second size.</param>
    /// <returns>True if a is greater than b; otherwise, false.</returns>
    public static bool operator >(Size<T> a, Size<T> b)
    {
        int w = a.Width.CompareTo(b.Width);
        return w > 0 || (w == 0 && a.Height.CompareTo(b.Height) > 0);
    }
}

/// <summary>
/// Represents a range with minimum and maximum values.
/// </summary>
/// <typeparam name="T">The type of the min and max values, must implement IComparable&lt;T&gt;.</typeparam>
[StructLayout(LayoutKind.Sequential /*, Pack = 1*/)]
public struct Range<T>
    where T : IComparable<T>
{
    /// <summary>
    /// The minimum value of the range.
    /// </summary>
    public T Min;
    
    /// <summary>
    /// The maximum value of the range.
    /// </summary>
    public T Max;

    /// <summary>
    /// Creates a new Range with the specified minimum and maximum values.
    /// </summary>
    /// <param name="min">The minimum value.</param>
    /// <param name="max">The maximum value.</param>
    public Range(T min, T max)
    {
        Min = min;
        Max = max;
    }

    /// <summary>
    /// Determines whether the specified value is within the range [Min, Max].
    /// </summary>
    /// <param name="value">The value to test.</param>
    /// <returns>True if the value is within the range; otherwise, false.</returns>
    public bool Contains(T value)
        => Min.CompareTo(value) <= 0 && value.CompareTo(Max) <= 0;

    /// <summary>
    /// Returns true if the given object is equal to this range.
    /// </summary>
    /// <param name="obj">The object to compare with the current instance.</param>
    /// <returns>True if the given object is equal to this range; otherwise, false.</returns>
    public override bool Equals(object obj)
        => obj is Range<T> other && Min.Equals(other.Min) && Max.Equals(other.Max);

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    public override int GetHashCode()
        => HashCode.Combine(Min, Max);

    /// <summary>
    /// Determines whether two ranges are equal.
    /// </summary>
    /// <param name="a">The first range.</param>
    /// <param name="b">The second range.</param>
    /// <returns>True if the ranges are equal; otherwise, false.</returns>
    public static bool operator ==(Range<T> a, Range<T> b) => a.Min.Equals(b.Min) && a.Max.Equals(b.Max);
    
    /// <summary>
    /// Determines whether two ranges are not equal.
    /// </summary>
    /// <param name="a">The first range.</param>
    /// <param name="b">The second range.</param>
    /// <returns>True if the ranges are not equal; otherwise, false.</returns>
    public static bool operator !=(Range<T> a, Range<T> b) => !(a == b);
}

