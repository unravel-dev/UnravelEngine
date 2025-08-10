using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
public abstract class NativeObject : IEquatable<NativeObject>
{
    public abstract bool IsValid();

    // Implement IEquatable<NativeObject>
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

    // Override Equals(object)
    public override bool Equals(object obj)
    {
        if (ReferenceEquals(obj, null))
            return false;

        if (!IsValid())
            return false;

        if (ReferenceEquals(this, obj))
            return true;

        // Ensure the object is of the same type.
        if (obj.GetType() != this.GetType())
            return false;

        return Equals(obj as NativeObject);
    }

    // Override GetHashCode()
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

    // Overload operator ==
    public static bool operator ==(NativeObject left, NativeObject right)
    {
        if (ReferenceEquals(left, right))
            return true;

        if (ReferenceEquals(left, null) || !left.IsValid())
            return ReferenceEquals(right, null) || !right.IsValid();

        if (ReferenceEquals(right, null) || !right.IsValid())
            return false;

        return left.Equals(right);
    }

    // Overload operator !=
    public static bool operator !=(NativeObject left, NativeObject right)
    {
        return !(left == right);
    }
}

}

}


