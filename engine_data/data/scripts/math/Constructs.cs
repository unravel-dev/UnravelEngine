using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;


[StructLayout(LayoutKind.Sequential /*, Pack = 1*/)]
public struct Size<T>
    where T : IComparable<T>
{
    public T Width;
    public T Height;

    public Size(T width, T height)
    {
        Width  = width;
        Height = height;
    }

    public bool IsValid()
        => !Width.Equals(default(T)) && !Height.Equals(default(T));

    public override bool Equals(object obj)
        => obj is Size<T> o && Width.Equals(o.Width) && Height.Equals(o.Height);

    public override int GetHashCode()
        => HashCode.Combine(Width, Height);

    public static bool operator ==(Size<T> a, Size<T> b) => a.Width.Equals(b.Width) && a.Height.Equals(b.Height);
    public static bool operator !=(Size<T> a, Size<T> b) => !(a == b);

    public static bool operator <(Size<T> a, Size<T> b)
    {
        int w = a.Width.CompareTo(b.Width);
        return w < 0 || (w == 0 && a.Height.CompareTo(b.Height) < 0);
    }

    public static bool operator >(Size<T> a, Size<T> b)
    {
        int w = a.Width.CompareTo(b.Width);
        return w > 0 || (w == 0 && a.Height.CompareTo(b.Height) > 0);
    }
}

[StructLayout(LayoutKind.Sequential /*, Pack = 1*/)]
public struct Range<T>
    where T : IComparable<T>
{
    // public fields only => exact layout control
    public T Min;
    public T Max;

    public Range(T min, T max)
    {
        Min = min;
        Max = max;
    }

    public bool Contains(T value)
        => Min.CompareTo(value) <= 0 && value.CompareTo(Max) <= 0;

    public override bool Equals(object obj)
        => obj is Range<T> other && Min.Equals(other.Min) && Max.Equals(other.Max);

    public override int GetHashCode()
        => HashCode.Combine(Min, Max);

    public static bool operator ==(Range<T> a, Range<T> b) => a.Min.Equals(b.Min) && a.Max.Equals(b.Max);
    public static bool operator !=(Range<T> a, Range<T> b) => !(a == b);
}

