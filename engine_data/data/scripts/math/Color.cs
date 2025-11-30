using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Color : IEquatable<Color>, IFormattable
{
    /// <summary>
    /// Red component of the color.
    /// </summary>
    public float r;

    /// <summary>
    /// Green component of the color.
    /// </summary>
    public float g;

    /// <summary>
    /// Blue component of the color.
    /// </summary>
    public float b;

    /// <summary>
    /// Alpha component of the color (0 is transparent, 1 is opaque).
    /// </summary>
    public float a;

    /// <summary>
    /// Solid red. RGBA is (1, 0, 0, 1).
    /// </summary>
    public static Color red
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 0f, 0f, 1f);
        }
    }

    /// <summary>
    /// Solid green. RGBA is (0, 1, 0, 1).
    /// </summary>
    public static Color green
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 1f, 0f, 1f);
        }
    }

    /// <summary>
    /// Solid blue. RGBA is (0, 0, 1, 1).
    /// </summary>
    public static Color blue
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 1f, 1f);
        }
    }

    /// <summary>
    /// Solid white. RGBA is (1, 1, 1, 1).
    /// </summary>
    public static Color white
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 1f, 1f, 1f);
        }
    }

    /// <summary>
    /// Solid black. RGBA is (0, 0, 0, 1).
    /// </summary>
    public static Color black
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 0f, 1f);
        }
    }

    /// <summary>
    /// Yellow. RGBA is (1, 0.92, 0.016, 1), but the color is nice to look at!
    /// </summary>
    public static Color yellow
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 47f / 51f, 0.0156862754f, 1f);
        }
    }

    /// <summary>
    /// Cyan. RGBA is (0, 1, 1, 1).
    /// </summary>
    public static Color cyan
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 1f, 1f, 1f);
        }
    }

    /// <summary>
    /// Magenta. RGBA is (1, 0, 1, 1).
    /// </summary>
    public static Color magenta
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 0f, 1f, 1f);
        }
    }

    /// <summary>
    /// Gray. RGBA is (0.5, 0.5, 0.5, 1).
    /// </summary>
    public static Color gray
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0.5f, 0.5f, 0.5f, 1f);
        }
    }

    /// <summary>
    /// English spelling for gray. RGBA is the same (0.5, 0.5, 0.5, 1).
    /// </summary>
    public static Color grey
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0.5f, 0.5f, 0.5f, 1f);
        }
    }

    /// <summary>
    /// Completely transparent. RGBA is (0, 0, 0, 0).
    /// </summary>
    public static Color clear
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 0f, 0f);
        }
    }

    /// <summary>
    /// The grayscale value of the color. (Read Only)
    /// </summary>
    public float grayscale
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }

    /// <summary>
    /// Returns the maximum color component value: Max(r,g,b).
    /// </summary>
    public float maxColorComponent => Mathf.Max(Mathf.Max(r, g), b);

    /// <summary>
    /// Gets or sets the component at the specified index.
    /// </summary>
    /// <param name="index">The index of the component (0 = r, 1 = g, 2 = b, 3 = a).</param>
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
                    return r;
                case 1:
                    return g;
                case 2:
                    return b;
                case 3:
                    return a;
                default:
                    throw new IndexOutOfRangeException($"Invalid Color index({index})!");
            }
        }
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        set
        {
            switch (index)
            {
                case 0:
                    r = value;
                    break;
                case 1:
                    g = value;
                    break;
                case 2:
                    b = value;
                    break;
                case 3:
                    a = value;
                    break;
                default:
                    throw new IndexOutOfRangeException($"Invalid Color index({index})!");
            }
        }
    }

    /// <summary>
    /// Constructs a new Color with given r,g,b,a components.
    /// </summary>
    /// <param name="r">Red component.</param>
    /// <param name="g">Green component.</param>
    /// <param name="b">Blue component.</param>
    /// <param name="a">Alpha component.</param>
    public Color(float r, float g, float b, float a)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    /// <summary>
    /// Constructs a new Color with given r,g,b components and sets a to 1.
    /// </summary>
    /// <param name="r">Red component.</param>
    /// <param name="g">Green component.</param>
    /// <param name="b">Blue component.</param>
    public Color(float r, float g, float b)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        a = 1f;
    }

    /// <summary>
    /// Constructs a new Color with given r,g,b,a components as integers (0-255 range).
    /// </summary>
    /// <param name="r">Red component (0-255).</param>
    /// <param name="g">Green component (0-255).</param>
    /// <param name="b">Blue component (0-255).</param>
    /// <param name="a">Alpha component (0-255).</param>
    public Color(int r, int g, int b, int a)
    {
        this.r = r / 255f;
        this.g = g / 255f;
        this.b = b / 255f;
        this.a = a / 255f;
    }

    /// <summary>
    /// Constructs a new Color with given r,g,b components as integers (0-255 range) and sets a to 255.
    /// </summary>
    /// <param name="r">Red component (0-255).</param>
    /// <param name="g">Green component (0-255).</param>
    /// <param name="b">Blue component (0-255).</param>
    public Color(int r, int g, int b)
    {
        this.r = r / 255f;
        this.g = g / 255f;
        this.b = b / 255f;
        this.a = 1f;
    }


    /// <summary>
    /// Returns a formatted string of this color.
    /// </summary>
    /// <returns>A formatted string representation of the color.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    /// <summary>
    /// Returns a formatted string of this color.
    /// </summary>
    /// <returns>A formatted string representation of the color.</returns>
    public override string ToString()
    {
        return ToString(null, null);
    }

    /// <summary>
    /// Returns a formatted string of this color.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <returns>A formatted string representation of the color.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format)
    {
        return ToString(format, null);
    }

    /// <summary>
    /// Returns a formatted string of this color.
    /// </summary>
    /// <param name="format">A numeric format string.</param>
    /// <param name="formatProvider">An object that specifies culture-specific formatting.</param>
    /// <returns>A formatted string representation of the color.</returns>
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    public string ToString(string format, IFormatProvider formatProvider)
    {
        if (string.IsNullOrEmpty(format))
        {
            format = "F3";
        }

        if (formatProvider == null)
        {
            formatProvider = CultureInfo.InvariantCulture.NumberFormat;
        }

        return string.Format("rgba({0}, {1}, {2}, {3})", r.ToString(format, formatProvider), g.ToString(format, formatProvider), b.ToString(format, formatProvider), a.ToString(format, formatProvider));
    }

    /// <summary>
    /// Returns the hash code for this instance.
    /// </summary>
    /// <returns>A 32-bit signed integer that is the hash code for this instance.</returns>
    public override int GetHashCode()
    {
        return ((Vector4)this).GetHashCode();
    }

    /// <summary>
    /// Returns true if the given color is exactly equal to this color.
    /// </summary>
    /// <param name="other">The object to compare with the current instance.</param>
    /// <returns>True if the given color is exactly equal to this color.</returns>
    public override bool Equals(object other)
    {
        if (!(other is Color))
        {
            return false;
        }

        return Equals((Color)other);
    }

    /// <summary>
    /// Returns true if the given color is exactly equal to this color.
    /// </summary>
    /// <param name="other">The color to compare with the current instance.</param>
    /// <returns>True if the given color is exactly equal to this color.</returns>
    public bool Equals(Color other)
    {
        return r.Equals(other.r) && g.Equals(other.g) && b.Equals(other.b) && a.Equals(other.a);
    }

    /// <summary>
    /// Adds two colors component-wise.
    /// </summary>
    /// <param name="a">The first color.</param>
    /// <param name="b">The second color.</param>
    /// <returns>The sum of the two colors.</returns>
    public static Color operator +(Color a, Color b)
    {
        return new Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
    }

    /// <summary>
    /// Subtracts two colors component-wise.
    /// </summary>
    /// <param name="a">The first color.</param>
    /// <param name="b">The second color.</param>
    /// <returns>The difference of the two colors.</returns>
    public static Color operator -(Color a, Color b)
    {
        return new Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
    }

    /// <summary>
    /// Multiplies two colors component-wise.
    /// </summary>
    /// <param name="a">The first color.</param>
    /// <param name="b">The second color.</param>
    /// <returns>The component-wise product of the two colors.</returns>
    public static Color operator *(Color a, Color b)
    {
        return new Color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
    }

    /// <summary>
    /// Multiplies a color by a scalar.
    /// </summary>
    /// <param name="a">The color.</param>
    /// <param name="b">The scalar value.</param>
    /// <returns>The scaled color.</returns>
    public static Color operator *(Color a, float b)
    {
        return new Color(a.r * b, a.g * b, a.b * b, a.a * b);
    }

    /// <summary>
    /// Multiplies a scalar by a color.
    /// </summary>
    /// <param name="b">The scalar value.</param>
    /// <param name="a">The color.</param>
    /// <returns>The scaled color.</returns>
    public static Color operator *(float b, Color a)
    {
        return new Color(a.r * b, a.g * b, a.b * b, a.a * b);
    }

    /// <summary>
    /// Divides a color by a scalar.
    /// </summary>
    /// <param name="a">The color.</param>
    /// <param name="b">The scalar value.</param>
    /// <returns>The divided color.</returns>
    public static Color operator /(Color a, float b)
    {
        return new Color(a.r / b, a.g / b, a.b / b, a.a / b);
    }

    /// <summary>
    /// Determines whether two colors are equal.
    /// </summary>
    /// <param name="lhs">The left-hand side color.</param>
    /// <param name="rhs">The right-hand side color.</param>
    /// <returns>True if the colors are equal; otherwise, false.</returns>
    public static bool operator ==(Color lhs, Color rhs)
    {
        return (Vector4)lhs == (Vector4)rhs;
    }

    /// <summary>
    /// Determines whether two colors are not equal.
    /// </summary>
    /// <param name="lhs">The left-hand side color.</param>
    /// <param name="rhs">The right-hand side color.</param>
    /// <returns>True if the colors are not equal; otherwise, false.</returns>
    public static bool operator !=(Color lhs, Color rhs)
    {
        return !(lhs == rhs);
    }

    /// <summary>
    /// Linearly interpolates between colors a and b by t.
    /// </summary>
    /// <param name="a">Color a.</param>
    /// <param name="b">Color b.</param>
    /// <param name="t">Float for combining a and b.</param>
    /// <returns>The interpolated color.</returns>
    public static Color Lerp(Color a, Color b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }

    /// <summary>
    /// Linearly interpolates between colors a and b by t without clamping the interpolation parameter.
    /// </summary>
    /// <param name="a">The first color.</param>
    /// <param name="b">The second color.</param>
    /// <param name="t">The interpolation parameter (not clamped).</param>
    /// <returns>The interpolated color.</returns>
    public static Color LerpUnclamped(Color a, Color b, float t)
    {
        return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }

    internal Color RGBMultiplied(float multiplier)
    {
        return new Color(r * multiplier, g * multiplier, b * multiplier, a);
    }

    internal Color AlphaMultiplied(float multiplier)
    {
        return new Color(r, g, b, a * multiplier);
    }

    internal Color RGBMultiplied(Color multiplier)
    {
        return new Color(r * multiplier.r, g * multiplier.g, b * multiplier.b, a);
    }

    /// <summary>
    /// Implicitly converts a Color to a Vector4.
    /// </summary>
    /// <param name="c">The Color to convert.</param>
    /// <returns>A Vector4 with r, g, b, a components from the Color.</returns>
    public static implicit operator Vector4(Color c)
    {
        return new Vector4(c.r, c.g, c.b, c.a);
    }

    /// <summary>
    /// Implicitly converts a Vector4 to a Color.
    /// </summary>
    /// <param name="v">The Vector4 to convert.</param>
    /// <returns>A Color with r, g, b, a components from the Vector4.</returns>
    public static implicit operator Color(Vector4 v)
    {
        return new Color(v.x, v.y, v.z, v.w);
    }

    /// <summary>
    /// Converts an RGB color to HSV color space.
    /// </summary>
    /// <param name="rgbColor">The RGB color to convert.</param>
    /// <param name="H">The hue component (0-1).</param>
    /// <param name="S">The saturation component (0-1).</param>
    /// <param name="V">The value/brightness component (0-1).</param>
    public static void RGBToHSV(Color rgbColor, out float H, out float S, out float V)
    {
        if (rgbColor.b > rgbColor.g && rgbColor.b > rgbColor.r)
        {
            RGBToHSVHelper(4f, rgbColor.b, rgbColor.r, rgbColor.g, out H, out S, out V);
        }
        else if (rgbColor.g > rgbColor.r)
        {
            RGBToHSVHelper(2f, rgbColor.g, rgbColor.b, rgbColor.r, out H, out S, out V);
        }
        else
        {
            RGBToHSVHelper(0f, rgbColor.r, rgbColor.g, rgbColor.b, out H, out S, out V);
        }
    }

    private static void RGBToHSVHelper(float offset, float dominantcolor, float colorone, float colortwo, out float H, out float S, out float V)
    {
        V = dominantcolor;
        if (V != 0f)
        {
            float num = 0f;
            num = ((!(colorone > colortwo)) ? colorone : colortwo);
            float num2 = V - num;
            if (num2 != 0f)
            {
                S = num2 / V;
                H = offset + (colorone - colortwo) / num2;
            }
            else
            {
                S = 0f;
                H = offset + (colorone - colortwo);
            }

            H /= 6f;
            if (H < 0f)
            {
                H += 1f;
            }
        }
        else
        {
            S = 0f;
            H = 0f;
        }
    }

    /// <summary>
    /// Creates an RGB colour from HSV input.
    /// </summary>
    /// <param name="H">Hue [0..1].</param>
    /// <param name="S">Saturation [0..1].</param>
    /// <param name="V">Brightness value [0..1].</param>
    /// <returns>An opaque colour with HSV matching the input.</returns>
    public static Color HSVToRGB(float H, float S, float V)
    {
        return HSVToRGB(H, S, V, hdr: true);
    }

    /// <summary>
    /// Creates an RGB colour from HSV input.
    /// </summary>
    /// <param name="H">Hue [0..1].</param>
    /// <param name="S">Saturation [0..1].</param>
    /// <param name="V">Brightness value [0..1].</param>
    /// <param name="hdr">Output HDR colours. If true, the returned colour will not be clamped to [0..1].</param>
    /// <returns>An opaque colour with HSV matching the input.</returns>
    public static Color HSVToRGB(float H, float S, float V, bool hdr)
    {
        Color result = white;
        if (S == 0f)
        {
            result.r = V;
            result.g = V;
            result.b = V;
        }
        else if (V == 0f)
        {
            result.r = 0f;
            result.g = 0f;
            result.b = 0f;
        }
        else
        {
            result.r = 0f;
            result.g = 0f;
            result.b = 0f;
            float num = H * 6f;
            int num2 = (int)Mathf.Floor(num);
            float num3 = num - (float)num2;
            float num4 = V * (1f - S);
            float num5 = V * (1f - S * num3);
            float num6 = V * (1f - S * (1f - num3));
            switch (num2)
            {
                case 0:
                    result.r = V;
                    result.g = num6;
                    result.b = num4;
                    break;
                case 1:
                    result.r = num5;
                    result.g = V;
                    result.b = num4;
                    break;
                case 2:
                    result.r = num4;
                    result.g = V;
                    result.b = num6;
                    break;
                case 3:
                    result.r = num4;
                    result.g = num5;
                    result.b = V;
                    break;
                case 4:
                    result.r = num6;
                    result.g = num4;
                    result.b = V;
                    break;
                case 5:
                    result.r = V;
                    result.g = num4;
                    result.b = num5;
                    break;
                case 6:
                    result.r = V;
                    result.g = num6;
                    result.b = num4;
                    break;
                case -1:
                    result.r = V;
                    result.g = num4;
                    result.b = num5;
                    break;
            }

            if (!hdr)
            {
                result.r = Mathf.Clamp(result.r, 0f, 1f);
                result.g = Mathf.Clamp(result.g, 0f, 1f);
                result.b = Mathf.Clamp(result.b, 0f, 1f);
            }
        }

        return result;
    }
    

}

