using System;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct Color : IEquatable<Color>, IFormattable
{
    //
    // Summary:
    //     Red component of the color.
    public float r;

    //
    // Summary:
    //     Green component of the color.
    public float g;

    //
    // Summary:
    //     Blue component of the color.
    public float b;

    //
    // Summary:
    //     Alpha component of the color (0 is transparent, 1 is opaque).
    public float a;

    //
    // Summary:
    //     Solid red. RGBA is (1, 0, 0, 1).
    public static Color red
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 0f, 0f, 1f);
        }
    }

    //
    // Summary:
    //     Solid green. RGBA is (0, 1, 0, 1).
    public static Color green
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 1f, 0f, 1f);
        }
    }

    //
    // Summary:
    //     Solid blue. RGBA is (0, 0, 1, 1).
    public static Color blue
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 1f, 1f);
        }
    }

    //
    // Summary:
    //     Solid white. RGBA is (1, 1, 1, 1).
    public static Color white
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 1f, 1f, 1f);
        }
    }

    //
    // Summary:
    //     Solid black. RGBA is (0, 0, 0, 1).
    public static Color black
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 0f, 1f);
        }
    }

    //
    // Summary:
    //     Yellow. RGBA is (1, 0.92, 0.016, 1), but the color is nice to look at!
    public static Color yellow
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 47f / 51f, 0.0156862754f, 1f);
        }
    }

    //
    // Summary:
    //     Cyan. RGBA is (0, 1, 1, 1).
    public static Color cyan
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 1f, 1f, 1f);
        }
    }

    //
    // Summary:
    //     Magenta. RGBA is (1, 0, 1, 1).
    public static Color magenta
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(1f, 0f, 1f, 1f);
        }
    }

    //
    // Summary:
    //     Gray. RGBA is (0.5, 0.5, 0.5, 1).
    public static Color gray
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0.5f, 0.5f, 0.5f, 1f);
        }
    }

    //
    // Summary:
    //     English spelling for gray. RGBA is the same (0.5, 0.5, 0.5, 1).
    public static Color grey
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0.5f, 0.5f, 0.5f, 1f);
        }
    }

    //
    // Summary:
    //     Completely transparent. RGBA is (0, 0, 0, 0).
    public static Color clear
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return new Color(0f, 0f, 0f, 0f);
        }
    }

    //
    // Summary:
    //     The grayscale value of the color. (Read Only)
    public float grayscale
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        get
        {
            return 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }

    //
    // Summary:
    //     Returns the maximum color component value: Max(r,g,b).
    public float maxColorComponent => Mathf.Max(Mathf.Max(r, g), b);

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

    //
    // Summary:
    //     Constructs a new Color with given r,g,b,a components.
    //
    // Parameters:
    //   r:
    //     Red component.
    //
    //   g:
    //     Green component.
    //
    //   b:
    //     Blue component.
    //
    //   a:
    //     Alpha component.
    public Color(float r, float g, float b, float a)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        this.a = a;
    }

    //
    // Summary:
    //     Constructs a new Color with given r,g,b components and sets a to 1.
    //
    // Parameters:
    //   r:
    //     Red component.
    //
    //   g:
    //     Green component.
    //
    //   b:
    //     Blue component.
    public Color(float r, float g, float b)
    {
        this.r = r;
        this.g = g;
        this.b = b;
        a = 1f;
    }

    //
    // Summary:
    //     Returns a formatted string of this color.
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
    //     Returns a formatted string of this color.
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
    //     Returns a formatted string of this color.
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
            format = "F3";
        }

        if (formatProvider == null)
        {
            formatProvider = CultureInfo.InvariantCulture.NumberFormat;
        }

        return string.Format("RGBA({0}, {1}, {2}, {3})", r.ToString(format, formatProvider), g.ToString(format, formatProvider), b.ToString(format, formatProvider), a.ToString(format, formatProvider));
    }

    public override int GetHashCode()
    {
        return ((Vector4)this).GetHashCode();
    }

    public override bool Equals(object other)
    {
        if (!(other is Color))
        {
            return false;
        }

        return Equals((Color)other);
    }

    public bool Equals(Color other)
    {
        return r.Equals(other.r) && g.Equals(other.g) && b.Equals(other.b) && a.Equals(other.a);
    }

    public static Color operator +(Color a, Color b)
    {
        return new Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
    }

    public static Color operator -(Color a, Color b)
    {
        return new Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
    }

    public static Color operator *(Color a, Color b)
    {
        return new Color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
    }

    public static Color operator *(Color a, float b)
    {
        return new Color(a.r * b, a.g * b, a.b * b, a.a * b);
    }

    public static Color operator *(float b, Color a)
    {
        return new Color(a.r * b, a.g * b, a.b * b, a.a * b);
    }

    public static Color operator /(Color a, float b)
    {
        return new Color(a.r / b, a.g / b, a.b / b, a.a / b);
    }

    public static bool operator ==(Color lhs, Color rhs)
    {
        return (Vector4)lhs == (Vector4)rhs;
    }

    public static bool operator !=(Color lhs, Color rhs)
    {
        return !(lhs == rhs);
    }

    //
    // Summary:
    //     Linearly interpolates between colors a and b by t.
    //
    // Parameters:
    //   a:
    //     Color a.
    //
    //   b:
    //     Color b.
    //
    //   t:
    //     Float for combining a and b.
    public static Color Lerp(Color a, Color b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
    }

    //
    // Summary:
    //     Linearly interpolates between colors a and b by t.
    //
    // Parameters:
    //   a:
    //
    //   b:
    //
    //   t:
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

    public static implicit operator Vector4(Color c)
    {
        return new Vector4(c.r, c.g, c.b, c.a);
    }

    public static implicit operator Color(Vector4 v)
    {
        return new Color(v.x, v.y, v.z, v.w);
    }

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

    //
    // Summary:
    //     Creates an RGB colour from HSV input.
    //
    // Parameters:
    //   H:
    //     Hue [0..1].
    //
    //   S:
    //     Saturation [0..1].
    //
    //   V:
    //     Brightness value [0..1].
    //
    //   hdr:
    //     Output HDR colours. If true, the returned colour will not be clamped to [0..1].
    //
    //
    // Returns:
    //     An opaque colour with HSV matching the input.
    public static Color HSVToRGB(float H, float S, float V)
    {
        return HSVToRGB(H, S, V, hdr: true);
    }

    //
    // Summary:
    //     Creates an RGB colour from HSV input.
    //
    // Parameters:
    //   H:
    //     Hue [0..1].
    //
    //   S:
    //     Saturation [0..1].
    //
    //   V:
    //     Brightness value [0..1].
    //
    //   hdr:
    //     Output HDR colours. If true, the returned colour will not be clamped to [0..1].
    //
    //
    // Returns:
    //     An opaque colour with HSV matching the input.
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

