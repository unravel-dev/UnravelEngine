using System;
using System.Globalization;
using System.Runtime.InteropServices;

/// <summary>
/// A 2D rectangle: position of its top left corner plus size. UI rects use document pixels with y
/// growing downwards, the space of <c>Input.mousePosition</c> for screen-space documents.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Rect : IEquatable<Rect>
{
    /// <summary>X of the left edge.</summary>
    public float x;

    /// <summary>Y of the top edge.</summary>
    public float y;

    /// <summary>Width.</summary>
    public float width;

    /// <summary>Height.</summary>
    public float height;

    /// <summary>
    /// Creates a rectangle from its top left corner and size.
    /// </summary>
    public Rect(float x, float y, float width, float height)
    {
        this.x = x;
        this.y = y;
        this.width = width;
        this.height = height;
    }

    /// <summary>
    /// Creates a rectangle from its top left corner and size.
    /// </summary>
    public Rect(Vector2 position, Vector2 size) : this(position.x, position.y, size.x, size.y)
    {
    }

    /// <summary>A rectangle at the origin with no size.</summary>
    public static Rect zero => new Rect(0f, 0f, 0f, 0f);

    /// <summary>The top left corner.</summary>
    public Vector2 position
    {
        get => new Vector2(x, y);
        set
        {
            x = value.x;
            y = value.y;
        }
    }

    /// <summary>Width and height.</summary>
    public Vector2 size
    {
        get => new Vector2(width, height);
        set
        {
            width = value.x;
            height = value.y;
        }
    }

    /// <summary>The middle of the rectangle.</summary>
    public Vector2 center => new Vector2(x + width * 0.5f, y + height * 0.5f);

    /// <summary>X of the left edge.</summary>
    public float xMin => x;

    /// <summary>Y of the top edge.</summary>
    public float yMin => y;

    /// <summary>X of the right edge.</summary>
    public float xMax => x + width;

    /// <summary>Y of the bottom edge.</summary>
    public float yMax => y + height;

    /// <summary>
    /// Whether @p point lies inside the rectangle (edges at the top left included).
    /// </summary>
    public bool Contains(Vector2 point)
    {
        return point.x >= xMin && point.x < xMax && point.y >= yMin && point.y < yMax;
    }

    /// <summary>
    /// Whether the two rectangles share any area.
    /// </summary>
    public bool Overlaps(Rect other)
    {
        return other.xMax > xMin && other.xMin < xMax && other.yMax > yMin && other.yMin < yMax;
    }

    /// <inheritdoc/>
    public bool Equals(Rect other)
    {
        return x.Equals(other.x) && y.Equals(other.y) && width.Equals(other.width) && height.Equals(other.height);
    }

    /// <inheritdoc/>
    public override bool Equals(object obj)
    {
        return obj is Rect other && Equals(other);
    }

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        return HashCode.Combine(x, y, width, height);
    }

    /// <summary>Whether two rectangles are equal.</summary>
    public static bool operator ==(Rect a, Rect b) => a.Equals(b);

    /// <summary>Whether two rectangles differ.</summary>
    public static bool operator !=(Rect a, Rect b) => !a.Equals(b);

    /// <inheritdoc/>
    public override string ToString()
    {
        return string.Format(CultureInfo.InvariantCulture, "(x:{0:F2}, y:{1:F2}, width:{2:F2}, height:{3:F2})",
                             x, y, width, height);
    }
}
