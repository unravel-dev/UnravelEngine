using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Base class for managed wrappers around native engine objects.
    /// Equality accounts for validity; invalid instances are never equal.
    /// </summary>
    public abstract class NativeObject : IEquatable<NativeObject>
    {
        /// <summary>
        /// Returns whether this object still refers to a valid native instance.
        /// </summary>
        public abstract bool IsValid();

        /// <summary>
        /// Determines whether this instance is equal to another <see cref="NativeObject"/>.
        /// </summary>
        public bool Equals(NativeObject other)
        {
            // Check for null and compare run-time types.
            if (ReferenceEquals(other, null))
                return false;

            if (ReferenceEquals(this, other))
                return true;

            // If either object is not alive, they are not equal.
            if (!IsValid() || !other.IsValid())
                return false;

            // Add additional field comparisons here if needed.
            // For example, if you have an 'id' field:
            // return this.id == other.id;

            // If there's no additional state to compare, and 'IsValid()' is true, 
            // you might consider them equal only if they are the same instance.
            return false;
        }

        /// <summary>
        /// Determines whether this instance is equal to the specified object.
        /// </summary>
        public override bool Equals(object obj)
        {
            return Equals(obj as NativeObject);
        }

        /// <summary>
        /// Returns a hash code for this instance.
        /// </summary>
        public override int GetHashCode()
        {
            if (!IsValid())
                return 0; // Or some constant to represent 'not alive' state.

            // Include 'IsValid()' in the hash code if it's part of equality.
            // If you have other fields, include them in the hash code.
            // For example:
            // int hash = 17;
            // hash = hash * 23 + IsValid().GetHashCode();
            // hash = hash * 23 + id.GetHashCode();
            // return hash;

            // If no fields to include, you can use base.GetHashCode() or RuntimeHelpers.
            return base.GetHashCode();
        }

        /// <summary>
        /// Compares two <see cref="NativeObject"/> instances for equality.
        /// </summary>
        /// <remarks>
        /// Expensive: calls <see cref="IsValid"/>, which may involve native calls.
        /// For performance-critical null checks, prefer <see cref="object.ReferenceEquals"/>.
        /// </remarks>
        public static bool operator ==(NativeObject left, NativeObject right)
        {        // // Fast path: if either is actually null (reference), treat as null
            if (!ReferenceEquals(left, null))
                return left.Equals(right);


            if (!ReferenceEquals(right, null))
                return right.Equals(left);


            return true;
        }


        /// <summary>
        /// Compares two <see cref="NativeObject"/> instances for inequality.
        /// </summary>
        public static bool operator !=(NativeObject left, NativeObject right)
        {
            return !(left == right);
        }

        /// <summary>
        /// Implicit conversion to bool for null checking. Returns true if the object is valid.
        /// </summary>
        /// <param name="object">The object to check.</param>
        /// <returns><c>true</c> if the entity is valid; otherwise, <c>false</c>.</returns>
        public static implicit operator bool(NativeObject obj)
        {
            return obj.IsValid();
        }
    }
}


