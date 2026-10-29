using System;
using System.Globalization;
using System.Runtime.InteropServices;

/// <summary>
/// A 4x4 transformation matrix. Fields are stored column by column (m00, m10, m20, m30, m01, ...), the layout the
/// engine's matrices use, so spans of matrices pass to the engine without conversion. mRC is row R, column C;
/// points are column vectors (<c>M * p</c>), so <c>A * B</c> applies B first.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Matrix4x4 : IEquatable<Matrix4x4>, IFormattable
{
    // The determinant over its largest possible value for rows of these lengths (Hadamard's bound) under which
    // the inverse counts as singular. Being relative, uniformly tiny or huge scales still invert.
    private const float SINGULAR_RELATIVE_DETERMINANT = 1.1920929e-7f;

    /// <summary>Row 0, column 0.</summary>
    public float m00;
    /// <summary>Row 1, column 0.</summary>
    public float m10;
    /// <summary>Row 2, column 0.</summary>
    public float m20;
    /// <summary>Row 3, column 0.</summary>
    public float m30;
    /// <summary>Row 0, column 1.</summary>
    public float m01;
    /// <summary>Row 1, column 1.</summary>
    public float m11;
    /// <summary>Row 2, column 1.</summary>
    public float m21;
    /// <summary>Row 3, column 1.</summary>
    public float m31;
    /// <summary>Row 0, column 2.</summary>
    public float m02;
    /// <summary>Row 1, column 2.</summary>
    public float m12;
    /// <summary>Row 2, column 2.</summary>
    public float m22;
    /// <summary>Row 3, column 2.</summary>
    public float m32;
    /// <summary>Row 0, column 3.</summary>
    public float m03;
    /// <summary>Row 1, column 3.</summary>
    public float m13;
    /// <summary>Row 2, column 3.</summary>
    public float m23;
    /// <summary>Row 3, column 3.</summary>
    public float m33;

    /// <summary>
    /// Creates a matrix from its four columns.
    /// </summary>
    /// <param name="column0">The first column.</param>
    /// <param name="column1">The second column.</param>
    /// <param name="column2">The third column.</param>
    /// <param name="column3">The fourth column (the translation of an affine matrix).</param>
    public Matrix4x4(Vector4 column0, Vector4 column1, Vector4 column2, Vector4 column3)
    {
        m00 = column0.x; m10 = column0.y; m20 = column0.z; m30 = column0.w;
        m01 = column1.x; m11 = column1.y; m21 = column1.z; m31 = column1.w;
        m02 = column2.x; m12 = column2.y; m22 = column2.z; m32 = column2.w;
        m03 = column3.x; m13 = column3.y; m23 = column3.z; m33 = column3.w;
    }

    /// <summary>The identity matrix.</summary>
    public static Matrix4x4 identity => new Matrix4x4(new Vector4(1, 0, 0, 0), new Vector4(0, 1, 0, 0), new Vector4(0, 0, 1, 0), new Vector4(0, 0, 0, 1));

    /// <summary>The matrix with every element zero.</summary>
    public static Matrix4x4 zero => new Matrix4x4();

    /// <summary>
    /// The element at a row and column.
    /// </summary>
    /// <param name="row">The row, 0 to 3.</param>
    /// <param name="column">The column, 0 to 3.</param>
    public float this[int row, int column]
    {
        get => this[row + column * 4];
        set => this[row + column * 4] = value;
    }

    /// <summary>
    /// The element at an index in storage order (column by column).
    /// </summary>
    /// <param name="index">The index, 0 to 15.</param>
    public float this[int index]
    {
        get
        {
            switch (index)
            {
                case 0: return m00;
                case 1: return m10;
                case 2: return m20;
                case 3: return m30;
                case 4: return m01;
                case 5: return m11;
                case 6: return m21;
                case 7: return m31;
                case 8: return m02;
                case 9: return m12;
                case 10: return m22;
                case 11: return m32;
                case 12: return m03;
                case 13: return m13;
                case 14: return m23;
                case 15: return m33;
                default: throw new IndexOutOfRangeException("Invalid matrix index.");
            }
        }
        set
        {
            switch (index)
            {
                case 0: m00 = value; break;
                case 1: m10 = value; break;
                case 2: m20 = value; break;
                case 3: m30 = value; break;
                case 4: m01 = value; break;
                case 5: m11 = value; break;
                case 6: m21 = value; break;
                case 7: m31 = value; break;
                case 8: m02 = value; break;
                case 9: m12 = value; break;
                case 10: m22 = value; break;
                case 11: m32 = value; break;
                case 12: m03 = value; break;
                case 13: m13 = value; break;
                case 14: m23 = value; break;
                case 15: m33 = value; break;
                default: throw new IndexOutOfRangeException("Invalid matrix index.");
            }
        }
    }

    /// <summary>
    /// Gets a column.
    /// </summary>
    /// <param name="index">The column, 0 to 3.</param>
    /// <returns>The column.</returns>
    public Vector4 GetColumn(int index)
    {
        return new Vector4(this[0, index], this[1, index], this[2, index], this[3, index]);
    }

    /// <summary>
    /// Sets a column.
    /// </summary>
    /// <param name="index">The column, 0 to 3.</param>
    /// <param name="column">The new column.</param>
    public void SetColumn(int index, Vector4 column)
    {
        this[0, index] = column.x;
        this[1, index] = column.y;
        this[2, index] = column.z;
        this[3, index] = column.w;
    }

    /// <summary>
    /// Gets a row.
    /// </summary>
    /// <param name="index">The row, 0 to 3.</param>
    /// <returns>The row.</returns>
    public Vector4 GetRow(int index)
    {
        return new Vector4(this[index, 0], this[index, 1], this[index, 2], this[index, 3]);
    }

    /// <summary>The translation of an affine matrix (its fourth column).</summary>
    public Vector3 position => new Vector3(m03, m13, m23);

    /// <summary>
    /// A matrix that translates, rotates and scales: <c>Translate(position) * Rotate(rotation) * Scale(scale)</c>.
    /// </summary>
    /// <param name="position">The translation.</param>
    /// <param name="rotation">The rotation.</param>
    /// <param name="scale">The scale along the local axes.</param>
    /// <returns>The transformation matrix.</returns>
    public static Matrix4x4 TRS(Vector3 position, Quaternion rotation, Vector3 scale)
    {
        Matrix4x4 result = Rotate(rotation);
        result.m00 *= scale.x; result.m10 *= scale.x; result.m20 *= scale.x;
        result.m01 *= scale.y; result.m11 *= scale.y; result.m21 *= scale.y;
        result.m02 *= scale.z; result.m12 *= scale.z; result.m22 *= scale.z;
        result.m03 = position.x;
        result.m13 = position.y;
        result.m23 = position.z;
        return result;
    }

    /// <summary>
    /// A translation matrix.
    /// </summary>
    /// <param name="vector">The translation.</param>
    /// <returns>The matrix.</returns>
    public static Matrix4x4 Translate(Vector3 vector)
    {
        Matrix4x4 result = identity;
        result.m03 = vector.x;
        result.m13 = vector.y;
        result.m23 = vector.z;
        return result;
    }

    /// <summary>
    /// A scale matrix.
    /// </summary>
    /// <param name="vector">The scale along each axis.</param>
    /// <returns>The matrix.</returns>
    public static Matrix4x4 Scale(Vector3 vector)
    {
        Matrix4x4 result = identity;
        result.m00 = vector.x;
        result.m11 = vector.y;
        result.m22 = vector.z;
        return result;
    }

    /// <summary>
    /// A rotation matrix.
    /// </summary>
    /// <param name="q">The rotation; it should be normalized.</param>
    /// <returns>The matrix.</returns>
    public static Matrix4x4 Rotate(Quaternion q)
    {
        float xx = q.x * q.x;
        float yy = q.y * q.y;
        float zz = q.z * q.z;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float yz = q.y * q.z;
        float wx = q.w * q.x;
        float wy = q.w * q.y;
        float wz = q.w * q.z;
        Matrix4x4 result = identity;
        result.m00 = 1f - 2f * (yy + zz);
        result.m10 = 2f * (xy + wz);
        result.m20 = 2f * (xz - wy);
        result.m01 = 2f * (xy - wz);
        result.m11 = 1f - 2f * (xx + zz);
        result.m21 = 2f * (yz + wx);
        result.m02 = 2f * (xz + wy);
        result.m12 = 2f * (yz - wx);
        result.m22 = 1f - 2f * (xx + yy);
        return result;
    }

    /// <summary>
    /// Transforms a point (w = 1) by the full matrix, with the perspective divide.
    /// </summary>
    /// <param name="point">The point.</param>
    /// <returns>The transformed point.</returns>
    public Vector3 MultiplyPoint(Vector3 point)
    {
        float x = m00 * point.x + m01 * point.y + m02 * point.z + m03;
        float y = m10 * point.x + m11 * point.y + m12 * point.z + m13;
        float z = m20 * point.x + m21 * point.y + m22 * point.z + m23;
        float w = m30 * point.x + m31 * point.y + m32 * point.z + m33;
        float inverseW = 1f / w;
        return new Vector3(x * inverseW, y * inverseW, z * inverseW);
    }

    /// <summary>
    /// Transforms a point (w = 1) by an affine matrix, without the perspective divide.
    /// </summary>
    /// <param name="point">The point.</param>
    /// <returns>The transformed point.</returns>
    public Vector3 MultiplyPoint3x4(Vector3 point)
    {
        return new Vector3(m00 * point.x + m01 * point.y + m02 * point.z + m03,
                           m10 * point.x + m11 * point.y + m12 * point.z + m13,
                           m20 * point.x + m21 * point.y + m22 * point.z + m23);
    }

    /// <summary>
    /// Transforms a direction (w = 0): no translation.
    /// </summary>
    /// <param name="vector">The direction.</param>
    /// <returns>The transformed direction.</returns>
    public Vector3 MultiplyVector(Vector3 vector)
    {
        return new Vector3(m00 * vector.x + m01 * vector.y + m02 * vector.z,
                           m10 * vector.x + m11 * vector.y + m12 * vector.z,
                           m20 * vector.x + m21 * vector.y + m22 * vector.z);
    }

    /// <summary>The transposed matrix.</summary>
    public Matrix4x4 transpose => new Matrix4x4(GetRow(0), GetRow(1), GetRow(2), GetRow(3));

    /// <summary>The determinant.</summary>
    public float determinant
    {
        get
        {
            float s0 = m00 * m11 - m10 * m01;
            float s1 = m00 * m12 - m10 * m02;
            float s2 = m00 * m13 - m10 * m03;
            float s3 = m01 * m12 - m11 * m02;
            float s4 = m01 * m13 - m11 * m03;
            float s5 = m02 * m13 - m12 * m03;
            float c5 = m22 * m33 - m32 * m23;
            float c4 = m21 * m33 - m31 * m23;
            float c3 = m21 * m32 - m31 * m22;
            float c2 = m20 * m33 - m30 * m23;
            float c1 = m20 * m32 - m30 * m22;
            float c0 = m20 * m31 - m30 * m21;
            return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
        }
    }

    /// <summary>The inverse; the zero matrix when the matrix is singular or nearly so.</summary>
    public Matrix4x4 inverse
    {
        get
        {
            float s0 = m00 * m11 - m10 * m01;
            float s1 = m00 * m12 - m10 * m02;
            float s2 = m00 * m13 - m10 * m03;
            float s3 = m01 * m12 - m11 * m02;
            float s4 = m01 * m13 - m11 * m03;
            float s5 = m02 * m13 - m12 * m03;
            float c5 = m22 * m33 - m32 * m23;
            float c4 = m21 * m33 - m31 * m23;
            float c3 = m21 * m32 - m31 * m22;
            float c2 = m20 * m33 - m30 * m23;
            float c1 = m20 * m32 - m30 * m22;
            float c0 = m20 * m31 - m30 * m21;
            float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
            float rowLengths = GetRow(0).magnitude * GetRow(1).magnitude * GetRow(2).magnitude * GetRow(3).magnitude;
            if (!(MathF.Abs(det) > SINGULAR_RELATIVE_DETERMINANT * rowLengths))
            {
                return zero;
            }
            float invDet = 1f / det;
            Matrix4x4 r;
            r.m00 = (m11 * c5 - m12 * c4 + m13 * c3) * invDet;
            r.m01 = (-m01 * c5 + m02 * c4 - m03 * c3) * invDet;
            r.m02 = (m31 * s5 - m32 * s4 + m33 * s3) * invDet;
            r.m03 = (-m21 * s5 + m22 * s4 - m23 * s3) * invDet;
            r.m10 = (-m10 * c5 + m12 * c2 - m13 * c1) * invDet;
            r.m11 = (m00 * c5 - m02 * c2 + m03 * c1) * invDet;
            r.m12 = (-m30 * s5 + m32 * s2 - m33 * s1) * invDet;
            r.m13 = (m20 * s5 - m22 * s2 + m23 * s1) * invDet;
            r.m20 = (m10 * c4 - m11 * c2 + m13 * c0) * invDet;
            r.m21 = (-m00 * c4 + m01 * c2 - m03 * c0) * invDet;
            r.m22 = (m30 * s4 - m31 * s2 + m33 * s0) * invDet;
            r.m23 = (-m20 * s4 + m21 * s2 - m23 * s0) * invDet;
            r.m30 = (-m10 * c3 + m11 * c1 - m12 * c0) * invDet;
            r.m31 = (m00 * c3 - m01 * c1 + m02 * c0) * invDet;
            r.m32 = (-m30 * s3 + m31 * s1 - m32 * s0) * invDet;
            r.m33 = (m20 * s3 - m21 * s1 + m22 * s0) * invDet;
            return r;
        }
    }

    /// <summary>
    /// Multiplies two matrices: the result applies <paramref name="rhs"/> first.
    /// </summary>
    public static Matrix4x4 operator *(Matrix4x4 lhs, Matrix4x4 rhs)
    {
        Matrix4x4 r;
        r.m00 = lhs.m00 * rhs.m00 + lhs.m01 * rhs.m10 + lhs.m02 * rhs.m20 + lhs.m03 * rhs.m30;
        r.m01 = lhs.m00 * rhs.m01 + lhs.m01 * rhs.m11 + lhs.m02 * rhs.m21 + lhs.m03 * rhs.m31;
        r.m02 = lhs.m00 * rhs.m02 + lhs.m01 * rhs.m12 + lhs.m02 * rhs.m22 + lhs.m03 * rhs.m32;
        r.m03 = lhs.m00 * rhs.m03 + lhs.m01 * rhs.m13 + lhs.m02 * rhs.m23 + lhs.m03 * rhs.m33;
        r.m10 = lhs.m10 * rhs.m00 + lhs.m11 * rhs.m10 + lhs.m12 * rhs.m20 + lhs.m13 * rhs.m30;
        r.m11 = lhs.m10 * rhs.m01 + lhs.m11 * rhs.m11 + lhs.m12 * rhs.m21 + lhs.m13 * rhs.m31;
        r.m12 = lhs.m10 * rhs.m02 + lhs.m11 * rhs.m12 + lhs.m12 * rhs.m22 + lhs.m13 * rhs.m32;
        r.m13 = lhs.m10 * rhs.m03 + lhs.m11 * rhs.m13 + lhs.m12 * rhs.m23 + lhs.m13 * rhs.m33;
        r.m20 = lhs.m20 * rhs.m00 + lhs.m21 * rhs.m10 + lhs.m22 * rhs.m20 + lhs.m23 * rhs.m30;
        r.m21 = lhs.m20 * rhs.m01 + lhs.m21 * rhs.m11 + lhs.m22 * rhs.m21 + lhs.m23 * rhs.m31;
        r.m22 = lhs.m20 * rhs.m02 + lhs.m21 * rhs.m12 + lhs.m22 * rhs.m22 + lhs.m23 * rhs.m32;
        r.m23 = lhs.m20 * rhs.m03 + lhs.m21 * rhs.m13 + lhs.m22 * rhs.m23 + lhs.m23 * rhs.m33;
        r.m30 = lhs.m30 * rhs.m00 + lhs.m31 * rhs.m10 + lhs.m32 * rhs.m20 + lhs.m33 * rhs.m30;
        r.m31 = lhs.m30 * rhs.m01 + lhs.m31 * rhs.m11 + lhs.m32 * rhs.m21 + lhs.m33 * rhs.m31;
        r.m32 = lhs.m30 * rhs.m02 + lhs.m31 * rhs.m12 + lhs.m32 * rhs.m22 + lhs.m33 * rhs.m32;
        r.m33 = lhs.m30 * rhs.m03 + lhs.m31 * rhs.m13 + lhs.m32 * rhs.m23 + lhs.m33 * rhs.m33;
        return r;
    }

    /// <summary>
    /// Transforms a 4D vector.
    /// </summary>
    public static Vector4 operator *(Matrix4x4 lhs, Vector4 vector)
    {
        return new Vector4(lhs.m00 * vector.x + lhs.m01 * vector.y + lhs.m02 * vector.z + lhs.m03 * vector.w,
                           lhs.m10 * vector.x + lhs.m11 * vector.y + lhs.m12 * vector.z + lhs.m13 * vector.w,
                           lhs.m20 * vector.x + lhs.m21 * vector.y + lhs.m22 * vector.z + lhs.m23 * vector.w,
                           lhs.m30 * vector.x + lhs.m31 * vector.y + lhs.m32 * vector.z + lhs.m33 * vector.w);
    }

    /// <summary>True when every element is equal.</summary>
    public static bool operator ==(Matrix4x4 lhs, Matrix4x4 rhs) => lhs.Equals(rhs);

    /// <summary>True when an element differs.</summary>
    public static bool operator !=(Matrix4x4 lhs, Matrix4x4 rhs) => !lhs.Equals(rhs);

    /// <summary>
    /// Compares every element.
    /// </summary>
    /// <param name="other">The other matrix.</param>
    /// <returns>True when every element is equal.</returns>
    public bool Equals(Matrix4x4 other)
    {
        return GetColumn(0).Equals(other.GetColumn(0)) && GetColumn(1).Equals(other.GetColumn(1)) &&
               GetColumn(2).Equals(other.GetColumn(2)) && GetColumn(3).Equals(other.GetColumn(3));
    }

    /// <summary>
    /// Compares with an object.
    /// </summary>
    /// <param name="other">The object.</param>
    /// <returns>True when it is an equal matrix.</returns>
    public override bool Equals(object other) => other is Matrix4x4 matrix && Equals(matrix);

    /// <summary>
    /// A hash of the elements.
    /// </summary>
    /// <returns>The hash.</returns>
    public override int GetHashCode()
    {
        return HashCode.Combine(GetColumn(0), GetColumn(1), GetColumn(2), GetColumn(3));
    }

    /// <summary>
    /// The matrix as four rows of text.
    /// </summary>
    /// <returns>The text.</returns>
    public override string ToString() => ToString(null, null);

    /// <summary>
    /// The matrix as four rows of text.
    /// </summary>
    /// <param name="format">The number format; F5 when empty.</param>
    /// <returns>The text.</returns>
    public string ToString(string format) => ToString(format, null);

    /// <summary>
    /// The matrix as four rows of text.
    /// </summary>
    /// <param name="format">The number format; F5 when empty.</param>
    /// <param name="formatProvider">The culture; invariant when null.</param>
    /// <returns>The text.</returns>
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
        return FormatRow(this, 0, format, formatProvider) + "\n" + FormatRow(this, 1, format, formatProvider) + "\n" +
               FormatRow(this, 2, format, formatProvider) + "\n" + FormatRow(this, 3, format, formatProvider) + "\n";
    }

    private static string FormatRow(in Matrix4x4 matrix, int row, string format, IFormatProvider formatProvider)
    {
        return string.Format("{0}\t{1}\t{2}\t{3}",
                             matrix[row, 0].ToString(format, formatProvider),
                             matrix[row, 1].ToString(format, formatProvider),
                             matrix[row, 2].ToString(format, formatProvider),
                             matrix[row, 3].ToString(format, formatProvider));
    }
}
