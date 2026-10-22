using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A screen area for an on-screen control, such as a touch stick or a button. A finger, or a left
    /// mouse press, that starts inside a free zone belongs to the zone until it lifts: the game's mouse
    /// and <see cref="Input.GetTouch"/> never see it, so dragging a stick neither turns a mouse-look
    /// camera nor clicks. Feed what the zone's pointer does into the <see cref="VirtualGamepad"/> to turn it
    /// into input actions.
    /// </summary>
    /// <remarks>
    /// A zone holds one pointer at a time; where zones overlap, the newest free one takes a press. A UI
    /// element under the press takes it before any zone does, unless its RCSS has
    /// <c>pointer-events: none</c>. Zones end with the play session; <see cref="Dispose"/> removes one
    /// sooner.
    /// </remarks>
    public sealed class PointerZone : IDisposable
    {
        private uint id;
        private Rect rect_;

        /// <summary>
        /// Creates a zone with no area: it takes nothing until <see cref="rect"/> is set.
        /// </summary>
        public PointerZone()
        {
            id = internal_m2n_pointer_zone_create();
        }

        /// <summary>
        /// Creates a zone over an area.
        /// </summary>
        /// <param name="rect">The area, in the pixels of <see cref="Input.mousePosition"/>.</param>
        public PointerZone(Rect rect) : this()
        {
            this.rect = rect;
        }

        /// <summary>
        /// The area, in the pixels of <see cref="Input.mousePosition"/> (and of <see cref="UIElement.rect"/>
        /// for a screen-space document). A pointer holding the zone stays its own when the area moves away.
        /// </summary>
        public Rect rect
        {
            get => rect_;
            set
            {
                rect_ = value;
                internal_m2n_pointer_zone_set_area(id, value.x, value.y, value.width, value.height);
            }
        }

        /// <summary>
        /// Whether a pointer holds the zone this frame, the frame it lifts included.
        /// </summary>
        public bool isHeld => internal_m2n_pointer_zone_has_pointer(id);

        /// <summary>
        /// Gets the pointer holding the zone this frame. It reads as a <see cref="Touch"/>: it begins, is
        /// moved or stationary while it stays, and ends (or is canceled) for one frame.
        /// <see cref="Touch.FingerId"/> is -1 when the mouse holds the zone.
        /// </summary>
        /// <param name="pointer">The pointer, when there is one.</param>
        /// <returns><c>true</c> if a pointer holds the zone this frame.</returns>
        public bool TryGetPointer(out Touch pointer)
        {
            if (!internal_m2n_pointer_zone_has_pointer(id))
            {
                pointer = default;
                return false;
            }
            pointer = internal_m2n_pointer_zone_get_pointer(id);
            return true;
        }

        /// <summary>
        /// Removes the zone. A pointer it holds stays nobody's until it lifts.
        /// </summary>
        public void Dispose()
        {
            if (id == 0)
            {
                return;
            }
            internal_m2n_pointer_zone_destroy(id);
            id = 0;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_pointer_zone_create();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_pointer_zone_destroy(uint id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_pointer_zone_set_area(uint id, float x, float y, float width, float height);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_pointer_zone_has_pointer(uint id);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Touch internal_m2n_pointer_zone_get_pointer(uint id);
    }
}
