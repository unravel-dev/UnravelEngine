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
    /// Specifies how forces are applied to physics components.
    /// </summary>
    public enum ForceMode : byte
    {
        /// <summary>
        /// Add a continuous force to the rigidbody, using its mass.
        ///
        /// Apply the force in each FixedUpdate over a duration of time. This mode depends on the mass of the rigidbody,
        /// so more force must be applied to push or twist higher-mass objects the same amount as lower-mass objects. 
        /// This mode is useful for setting up realistic physics where it takes more force to move heavier objects.
        /// In this mode, the unit of the force parameter is applied to the rigidbody as mass * distance / time^2.
        /// </summary>
        Force,

        /// <summary>
        /// Add a continuous acceleration to the rigidbody, ignoring its mass.
        ///
        /// Apply the acceleration in each FixedUpdate over a duration of time. In contrast to <see cref="ForceMode.Force"/>, 
        /// Acceleration will move every rigidbody the same way regardless of differences in mass. This mode is useful 
        /// if you just want to control the acceleration of an object directly.
        /// In this mode, the unit of the force parameter is applied to the rigidbody as distance / time^2.
        /// </summary>
        Acceleration,

        /// <summary>
        /// Add an instant force impulse to the rigidbody, using its mass.
        ///
        /// Apply the impulse force instantly with a single function call. This mode depends on the mass of the rigidbody, 
        /// so more force must be applied to push or twist higher-mass objects the same amount as lower-mass objects. 
        /// This mode is useful for applying forces that happen instantly, such as forces from explosions or collisions.
        /// In this mode, the unit of the force parameter is applied to the rigidbody as mass * distance / time.
        /// </summary>
        Impulse,

        /// <summary>
        /// Add an instant velocity change to the rigidbody, ignoring its mass.
        ///
        /// Apply the velocity change instantly with a single function call. In contrast to <see cref="ForceMode.Impulse"/>, 
        /// VelocityChange will change the velocity of every rigidbody the same way regardless of differences in mass. 
        /// This mode is useful for something like a fleet of differently-sized spaceships that you want to control 
        /// without accounting for differences in mass.
        /// In this mode, the unit of the force parameter is applied to the rigidbody as distance / time.
        /// </summary>
        VelocityChange
    }
}
}
