using System;

namespace Ace
{
namespace Core
{
    /// <summary>
    /// Represents a generic asset with a unique identifier (UID).
    /// </summary>
    /// <typeparam name="T">The type of the asset.</typeparam>
    public class Asset<T> : IEquatable<Asset<T>>
    {
        /// <summary>
        /// Gets the unique identifier (UID) of the asset.
        /// </summary>
        public Guid uid { get; internal set; }

        /// <summary>
        /// Determines whether the specified object is equal to the current asset.
        /// </summary>
        /// <param name="obj">The object to compare with the current asset.</param>
        /// <returns>
        /// <c>true</c> if the specified object is an <see cref="Asset{T}"/> and is equal to the current asset; otherwise, <c>false</c>.
        /// </returns>
        public override bool Equals(object obj)
        {
            // Check for null and type compatibility
            if (obj is Asset<T> other)
                return Equals(other);

            return false;
        }

        /// <summary>
        /// Determines whether the specified <see cref="Asset{T}"/> is equal to the current asset.
        /// </summary>
        /// <param name="other">The asset to compare with the current asset.</param>
        /// <returns>
        /// <c>true</c> if the specified asset is equal to the current asset; otherwise, <c>false</c>.
        /// </returns>
        public bool Equals(Asset<T> other)
        {
            // Check for null and reference equality
            if (ReferenceEquals(other, null))
                return false;

            if (ReferenceEquals(this, other))
                return true;

            // Compare UIDs
            return this.uid == other.uid;
        }

        /// <summary>
        /// Determines whether two <see cref="Asset{T}"/> instances are equal.
        /// </summary>
        /// <param name="lhs">The first asset to compare.</param>
        /// <param name="rhs">The second asset to compare.</param>
        /// <returns>
        /// <c>true</c> if the two assets are equal; otherwise, <c>false</c>.
        /// </returns>
        public static bool operator ==(Asset<T> lhs, Asset<T> rhs)
        {
            // Handle null references safely
            if (ReferenceEquals(lhs, rhs))
                return true;

            if (ReferenceEquals(lhs, null) || ReferenceEquals(rhs, null))
                return false;

            return lhs.Equals(rhs);
        }

        /// <summary>
        /// Determines whether two <see cref="Asset{T}"/> instances are not equal.
        /// </summary>
        /// <param name="lhs">The first asset to compare.</param>
        /// <param name="rhs">The second asset to compare.</param>
        /// <returns>
        /// <c>true</c> if the two assets are not equal; otherwise, <c>false</c>.
        /// </returns>
        public static bool operator !=(Asset<T> lhs, Asset<T> rhs) => !(lhs == rhs);

        /// <summary>
        /// Serves as the default hash function for the asset.
        /// </summary>
        /// <returns>A hash code for the current asset.</returns>
        public override int GetHashCode() => uid.GetHashCode();
    }
}
}
