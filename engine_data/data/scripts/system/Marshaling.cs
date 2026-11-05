using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

/// <summary>
/// Extension methods for converting byte arrays to blittable structs.
/// </summary>
/// <remarks>
/// The bytes are read in the struct's managed layout, which is how native code writes a struct array (bools as one
/// byte). <typeparamref name="T"/> must hold no references.
/// </remarks>
public static class ByteArrayExtensions
{
    /// <summary>
    /// Converts a byte array to a single struct of type <typeparamref name="T"/>.
    /// </summary>
    public static T ToStruct<T>(this byte[] data) where T : struct
    {
        int structSize = Unsafe.SizeOf<T>();
        if (data.Length != structSize)
        {
            throw new ArgumentException($"The byte array length ({data.Length} bytes) does not match the struct size ({structSize} bytes).");
        }
        return MemoryMarshal.Read<T>(data);
    }

    /// <summary>
    /// Converts a byte array to an array of structs of type <typeparamref name="T"/>, copying the bytes once without
    /// boxing.
    /// </summary>
    public static T[] ToStructArray<T>(this byte[] data) where T : struct
    {
        int structSize = Unsafe.SizeOf<T>();
        if (data.Length % structSize != 0)
        {
            throw new ArgumentException($"The byte array length ({data.Length} bytes) is not a multiple of the struct size ({structSize} bytes).");
        }
        return MemoryMarshal.Cast<byte, T>(data).ToArray();
    }

    /// <summary>
    /// Packs an array of blittable structs into bytes, the form native code reads a struct array in.
    /// </summary>
    public static byte[] ToByteArray<T>(this T[] items) where T : struct
    {
        if (items == null || items.Length == 0)
        {
            return Array.Empty<byte>();
        }
        return MemoryMarshal.AsBytes(items.AsSpan()).ToArray();
    }
}
