using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{

    /// <summary>
    /// Represents information about a single hit of a ray, sphere, box or capsule cast.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct RaycastHit : IFormattable
    {
        /// <summary>
        /// The <see cref="Entity"/> that was hit.
        /// </summary>
        public Entity entity;

        /// <summary>
        /// Where the cast touched the entity, in world space. For a sphere, box or capsule cast this is the contact
        /// point on the entity's surface, not where the swept shape's center was.
        /// </summary>
        public Vector3 point;

        /// <summary>
        /// The normal vector at the point of contact on the entity's surface.
        /// </summary>
        public Vector3 normal;

        /// <summary>
        /// How far the cast traveled along its normalized direction before it touched the entity, in meters. For a
        /// ray this is the distance from the origin to <see cref="point"/>; a swept shape was moved by this much from
        /// where it started.
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

