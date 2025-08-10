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
    /// Represents a ray with an origin and a direction in 3D space.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct Ray : IFormattable
    {
        /// <summary>
        /// The origin point of the ray in 3D space.
        /// </summary>
        public Vector3 origin;

        /// <summary>
        /// The direction of the ray in 3D space.
        /// </summary>
        public Vector3 direction;

        /// <summary>
        /// Returns a string representation of the ray.
        /// </summary>
        /// <returns>A string that represents the ray.</returns>
        public override string ToString()
        {
            return ToString(null, null);
        }

        /// <summary>
        /// Returns a string representation of the ray using a specified format.
        /// </summary>
        /// <param name="format">The format string to use.</param>
        /// <returns>A string that represents the ray.</returns>
        public string ToString(string format)
        {
            return ToString(format, null);
        }

        /// <summary>
        /// Returns a string representation of the ray using a specified format and format provider.
        /// </summary>
        /// <param name="format">The format string to use.</param>
        /// <param name="formatProvider">
        /// An object that provides culture-specific formatting information.
        /// </param>
        /// <returns>A string that represents the ray.</returns>
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

            return string.Format(formatProvider, "(origin={0}, direction={1})",
                origin.ToString(format, formatProvider),
                direction.ToString(format, formatProvider));
        }
    }
}
}
