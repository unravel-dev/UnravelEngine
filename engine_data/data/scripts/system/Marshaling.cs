using System;
using System.Runtime.InteropServices;

public static class ByteArrayExtensions
{
    // Public function to convert a byte array to a single struct
    public static T ToStruct<T>(this byte[] data) where T : struct
    {
        return ToStructImpl<T>(data, 0, data.Length);
    }

    // Public function to convert a byte array to an array of structs
    public static T[] ToStructArray<T>(this byte[] data) where T : struct
    {
        int structSize = Marshal.SizeOf<T>();

        // Ensure the byte array length is a multiple of the struct size
        if (data.Length % structSize != 0)
        {
            throw new ArgumentException($"The byte array length ({data.Length} bytes) is not a multiple of the struct size ({structSize} bytes).");
        }

        // Calculate the number of structs in the array
        int count = data.Length / structSize;

        // Allocate the result array
        T[] result = new T[count];

        for (int i = 0; i < count; i++)
        {
            // Convert directly from the byte array using a pointer
            result[i] = ToStructImpl<T>(data, i * structSize, structSize);
        }

        return result;
    }

    // Private helper function to convert a byte array segment to a struct
    private static T ToStructImpl<T>(byte[] data, int offset, int size) where T : struct
    {
        // Ensure the provided size matches the struct size
        if (size != Marshal.SizeOf<T>())
        {
            throw new ArgumentException($"The size of the byte array segment ({size} bytes) does not match the size of the struct ({Marshal.SizeOf<T>()} bytes).");
        }

        // Verify that T is a struct with sequential layout
        // if (!typeof(T).IsValueType || !Attribute.IsDefined(typeof(T), typeof(StructLayoutAttribute)))
        // {
        //     throw new InvalidOperationException($"The type {typeof(T).Name} must be a struct with [StructLayout(LayoutKind.Sequential)] attribute.");
        // }

        // // Verify the layout kind is Sequential
        // var layoutAttribute = (StructLayoutAttribute)Attribute.GetCustomAttribute(typeof(T), typeof(StructLayoutAttribute));
        // if (layoutAttribute.Value != LayoutKind.Sequential)
        // {
        //     throw new InvalidOperationException($"The type {typeof(T).Name} must have [StructLayout(LayoutKind.Sequential)].");
        // }

        IntPtr ptr = Marshal.AllocHGlobal(size);

        try
        {
            // Copy the segment of the byte array into unmanaged memory
            Marshal.Copy(data, offset, ptr, size);

            // Convert the unmanaged memory to a struct
            return Marshal.PtrToStructure<T>(ptr);
        }
        finally
        {
            // Free the allocated unmanaged memory
            Marshal.FreeHGlobal(ptr);
        }
    }
}
