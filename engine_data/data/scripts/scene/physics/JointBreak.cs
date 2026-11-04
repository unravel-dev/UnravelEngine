using System;
using System.Globalization;

namespace Unravel.Core
{
    /// <summary>
    /// A joint that broke: the force or torque on it passed its break threshold, or a script broke it.
    /// </summary>
    public struct JointBreak : IFormattable
    {
        /// <summary>
        /// The entity of the joint component that broke.
        /// </summary>
        public Entity joint;

        /// <summary>
        /// The body the joint hung on; invalid when it hung on the world.
        /// </summary>
        public Entity bodyA;

        /// <summary>
        /// The body the joint held.
        /// </summary>
        public Entity bodyB;

        /// <summary>
        /// Force on the joint when it broke, in newtons.
        /// </summary>
        public float force;

        /// <summary>
        /// Torque on the joint when it broke, in newton meters.
        /// </summary>
        public float torque;

        /// <summary>
        /// Gets the joint component that broke.
        /// </summary>
        public JointComponent jointComponent => joint.GetComponent<JointComponent>();

        /// <summary>
        /// Returns a string with the joint, its bodies and the force that broke it.
        /// </summary>
        public override string ToString()
        {
            return ToString(null, null);
        }

        /// <summary>
        /// Returns a string with the joint, its bodies and the force that broke it.
        /// </summary>
        /// <param name="format">The numeric format for the force and torque.</param>
        /// <param name="formatProvider">The culture the numbers are formatted with.</param>
        public string ToString(string format, IFormatProvider formatProvider)
        {
            if (string.IsNullOrEmpty(format))
            {
                format = "F1";
            }
            formatProvider ??= CultureInfo.InvariantCulture.NumberFormat;
            return string.Format(formatProvider, "JointBreak(joint: {0}, bodyA: {1}, bodyB: {2}, force: {3} N, torque: {4} N m)",
                joint, bodyA, bodyB, force.ToString(format, formatProvider), torque.ToString(format, formatProvider));
        }
    }
}
