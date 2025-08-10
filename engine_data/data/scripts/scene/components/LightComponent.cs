using System;
using System.Runtime.CompilerServices;

namespace Ace
{
namespace Core
{
public class LightComponent : Component
{
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
}


