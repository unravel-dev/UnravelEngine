using System;
using System.Runtime.CompilerServices;

namespace Ace.Core
{
    /// <summary>
    /// Represents a base class for all components in the entity-component system.
    /// </summary>
    public abstract class Component : NativeObject
    {
        /// <summary>
        /// Gets the entity that owns this component.
        /// </summary>
        public Entity owner { get; internal set; }

        private bool cacheDirty = true;
        private TransformComponent cachedTransform = null;

        /// <summary>
        /// Gets the <see cref="TransformComponent"/> associated with this component's owner.
        /// </summary>
        /// <remarks>
        /// The transform is lazily loaded and cached for performance. The cache is updated
        /// if the owner entity is reassigned.
        /// </remarks>
        public TransformComponent transform
        {
            get
            {
                if (cacheDirty)
                {
                    cacheDirty = false;
                    cachedTransform = owner.transform;
                }
                return cachedTransform;
            }
        }

        /// <summary>
        /// Determines whether this component is valid.
        /// </summary>
        /// <returns>
        /// <c>true</c> if the owner entity is valid and this component exists on the owner;
        /// otherwise, <c>false</c>.
        /// </returns>
        public override bool IsValid()
        {
            if (!owner.IsValid())
            {
                return false;
            }
            return owner.HasComponent(GetType());
        }

        /// <summary>
        /// Sets the owning entity of this component.
        /// </summary>
        /// <param name="id">The unique identifier of the entity to associate with this component.</param>
        /// <remarks>
        /// This method is called internally and is not intended for direct use. It updates
        /// the owner entity and invalidates the cached transform.
        /// </remarks>
        private void internal_n2m_set_entity(uint id)
        {
            owner = new Entity(id);
            cacheDirty = true;
        }
    }
}
