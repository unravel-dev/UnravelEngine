using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Light source component attached to an entity.
    /// </summary>
    public class LightComponent : Component
    {
        /// <summary>
        /// Color of the light.
        /// </summary>
        public Color color
        {
            get
            {
                return internal_m2n_light_get_color(owner);
            }
            set
            {
                internal_m2n_light_set_color(owner, value);
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_light_get_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_light_set_color(Entity eid, Color color);
    }

}



