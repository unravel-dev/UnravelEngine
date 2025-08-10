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
    /// Represents information about a single hit during a raycasting operation.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct RaycastHit : IFormattable
    {
        /// <summary>
        /// The <see cref="Entity"/> that was hit by the ray.
        /// </summary>
        public Entity entity;

        /// <summary>
        /// The point in 3D space where the ray hit the entity.
        /// </summary>
        public Vector3 point;

        /// <summary>
        /// The normal vector at the point of contact on the entity's surface.
        /// </summary>
        public Vector3 normal;

        /// <summary>
        /// The distance from the ray's origin to the point of contact.
        /// </summary>
        public float distance;

        /// <summary>
        /// Returns a string representation of the raycast hit.
        /// </summary>
        /// <returns>A string that represents the raycast hit.</returns>
        public override string ToString()
        {
            return ToString(null, null);
        }

        /// <summary>
        /// Returns a string representation of the raycast hit using a specified format.
        /// </summary>
        /// <param name="format">The format string to use.</param>
        /// <returns>A string that represents the raycast hit.</returns>
        public string ToString(string format)
        {
            return ToString(format, null);
        }

        /// <summary>
        /// Returns a string representation of the raycast hit using a specified format and format provider.
        /// </summary>
        /// <param name="format">The format string to use.</param>
        /// <param name="formatProvider">
        /// An object that provides culture-specific formatting information.
        /// </param>
        /// <returns>A string that represents the raycast hit.</returns>
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

            return string.Format(formatProvider, "(entity={0}, point={1}, normal={2}, distance={3})",
                entity.name,
                point.ToString(format, formatProvider),
                normal.ToString(format, formatProvider),
                distance.ToString(format, formatProvider));
        }
    }
}
}
