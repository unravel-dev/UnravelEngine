using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ace
{
namespace Core
{
    /// <summary>
    /// Represents a contact point where a collision occurs.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct ContactPoint : IFormattable
    {
        /// <summary>
        /// The point of contact in world space.
        /// </summary>
        public Vector3 point;

        /// <summary>
        /// The normal vector at the contact point.
        /// </summary>
        public Vector3 normal;

        /// <summary>
        /// The distance between the colliders at the contact point.
        /// </summary>
        public float distance;

        /// <summary>
        /// The impulse applied to resolve the collision at the contact point.
        /// </summary>
        public float impulse;

        /// <summary>
        /// Converts the contact point to its string representation.
        /// </summary>
        /// <returns>A string that represents the contact point.</returns>
        public override string ToString()
        {
            return ToString(null, null);
        }

        /// <summary>
        /// Converts the contact point to its string representation with a specified format.
        /// </summary>
        /// <param name="format">The format string.</param>
        /// <returns>A string that represents the contact point.</returns>
        public string ToString(string format)
        {
            return ToString(format, null);
        }

        /// <summary>
        /// Converts the contact point to its string representation with a specified format and format provider.
        /// </summary>
        /// <param name="format">The format string.</param>
        /// <param name="formatProvider">An object that supplies culture-specific formatting information.</param>
        /// <returns>A string that represents the contact point.</returns>
        public string ToString(string format, IFormatProvider formatProvider)
        {
            if (string.IsNullOrEmpty(format))
            {
                format = "F2";
            }

            if (formatProvider == null)
            {
                formatProvider = CultureInfo.InvariantCulture.NumberFormat;
            }

            return string.Format(formatProvider, "(point={0}, normal={1}, distance={2}, impulse={3})",
                point.ToString(format, formatProvider),
                normal.ToString(format, formatProvider),
                distance.ToString(format, formatProvider),
                impulse.ToString(format, formatProvider));
        }
    };

}
}
