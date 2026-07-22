namespace Unravel.Core
{
    /// <summary>
    /// Simulation role of a rigid body.
    /// </summary>
    public enum RigidbodyType : byte
    {
        /// <summary>
        /// Not simulated. Transform may still be teleported; cost is AABB/broadphase update.
        /// Does not push dynamics with velocity.
        /// </summary>
        Static = 0,

        /// <summary>
        /// Driven by ECS transform. Pushes dynamic bodies using derived velocity over the fixed step.
        /// </summary>
        Kinematic = 1,

        /// <summary>
        /// Fully simulated. Physics writes transforms back when the body is awake.
        /// </summary>
        Dynamic = 2
    }
}
