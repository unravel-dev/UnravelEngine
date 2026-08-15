using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Buffers;

namespace Unravel.Core
{
    /// <summary>
    /// Why a contact or sensor overlap ended.
    /// </summary>
    /// <remarks>
    /// Logic that only decrements a counter can ignore this. Logic that reacts to the other
    /// side leaving under its own power - spawning a trail, re-targeting, playing an exit
    /// cue - should check for <see cref="Separated"/> first.
    /// </remarks>
    public enum ContactEndReason : byte
    {
        /// <summary>The pair moved apart. The only reason an enter event ever carries.</summary>
        Separated = 0,
        /// <summary>
        /// The other entity is being destroyed. It is still fully valid for the duration of
        /// this callback and invalid immediately afterwards.
        /// </summary>
        OtherDestroyed = 1,
        /// <summary>The other entity was deactivated.</summary>
        OtherDisabled = 2,
        /// <summary>This entity is being destroyed.</summary>
        SelfDestroyed = 3,
        /// <summary>This entity was deactivated.</summary>
        SelfDisabled = 4,
    }

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
        /// Why the overlap ended. Always <see cref="ContactEndReason.Separated"/> for enter events.
        /// </summary>
        /// <remarks>
        /// Exits synthesized because one side is going away carry the last known contact points
        /// rather than fresh ones - the manifold no longer exists by the time they are reported.
        /// </remarks>
        public ContactEndReason reason;

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
