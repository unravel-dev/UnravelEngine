using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Buffers;

namespace Ace
{
namespace Core
{
    /// <summary>
    /// Represents a collision that occurs between two entities.
    /// </summary>
    public class Collision : IFormattable
    {
        /// <summary>
        /// Gets the entity involved in the collision.
        /// </summary>
        public Entity entity;

        /// <summary>
        /// Gets the array of contact points where the collision occurred.
        /// </summary>
        public ContactPoint[] contacts;

        /// <summary>
        /// Converts the collision to its string representation.
        /// </summary>
        /// <returns>A string that represents the collision.</returns>
        public override string ToString()
        {
            return ToString(null, null);
        }

        /// <summary>
        /// Converts the collision to its string representation with a specified format.
        /// </summary>
        /// <param name="format">The format string.</param>
        /// <returns>A string that represents the collision.</returns>
        public string ToString(string format)
        {
            return ToString(format, null);
        }

        /// <summary>
        /// Converts the collision to its string representation with a specified format and format provider.
        /// </summary>
        /// <param name="format">The format string.</param>
        /// <param name="formatProvider">An object that supplies culture-specific formatting information.</param>
        /// <returns>A string that represents the collision.</returns>
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

            // Fall back to heap allocation for very large arrays
            string[] contactStrings = new string[contacts.Length];
            for (int i = 0; i < contacts.Length; i++)
            {
                contactStrings[i] = contacts[i].ToString();
            }
            return string.Format(formatProvider, "(entity={0}, contacts={1})",
                entity.name,
                string.Join(",", contactStrings));

        }
    }
}
}
