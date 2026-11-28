using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A trail: a strip along where the entity has been, drawn with a Surface material. Each point of it lives for
    /// <see cref="time"/> seconds. With <see cref="edge"/> it is the sheet two points of the entity sweep through space:
    /// a blade's swing. The points are never saved, and are gone when play begins or ends.
    /// </summary>
    public class TrailComponent : StripComponent
    {
        internal override uint rendererSlot => MaterialInstance.TrailSlot;

        /// <summary>Seconds each point of the trail lives.</summary>
        public float time
        {
            get => internal_m2n_trail_get_float(owner, (int)Field.Time);
            set => internal_m2n_trail_set_float(owner, (int)Field.Time, value);
        }

        /// <summary>How far the entity moves, in metres, before the trail takes another point.</summary>
        public float minDistance
        {
            get => internal_m2n_trail_get_float(owner, (int)Field.MinDistance);
            set => internal_m2n_trail_set_float(owner, (int)Field.MinDistance, value);
        }

        /// <summary>Whether the trail takes new points. Off, the ones it has age out and the trail ends: turn it on
        /// for the length of a swing. A trail turned on again starts a stretch of its own: no strip joins it to the
        /// one that is still fading.</summary>
        public bool emitting
        {
            get => internal_m2n_trail_get_flag(owner, (int)Flag.Emitting);
            set => internal_m2n_trail_set_flag(owner, (int)Flag.Emitting, value);
        }

        /// <summary>The trail is the sheet between <see cref="edgeBase"/> and <see cref="edgeTip"/> as they sweep through
        /// space. The edge's own length is the sheet's width: the strip's shape, alignment, width, corners and caps do
        /// not apply.</summary>
        public bool edge
        {
            get => internal_m2n_trail_get_flag(owner, (int)Flag.Edge);
            set => internal_m2n_trail_set_flag(owner, (int)Flag.Edge, value);
        }

        /// <summary>One end of the edge, in the entity's space: a blade's base.</summary>
        public Vector3 edgeBase
        {
            get => internal_m2n_trail_get_edge(owner, 0);
            set => internal_m2n_trail_set_edge(owner, 0, value);
        }

        /// <summary>The other end of the edge, in the entity's space: a blade's tip.</summary>
        public Vector3 edgeTip
        {
            get => internal_m2n_trail_get_edge(owner, 1);
            set => internal_m2n_trail_set_edge(owner, 1, value);
        }

        /// <summary>How many points the trail holds now.</summary>
        public int pointCount => internal_m2n_trail_get_point_count(owner);

        /// <summary>Forgets every point: the trail starts again where the entity is. Call it after teleporting the
        /// entity or taking it from a pool, so no strip joins the two places.</summary>
        public void Clear() => internal_m2n_trail_clear(owner);

        private enum Field
        {
            Time = 0,
            MinDistance = 1,
        }

        private enum Flag
        {
            Emitting = 0,
            Edge = 1,
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_trail_get_float(Entity eid, int field);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_trail_set_float(Entity eid, int field, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_trail_get_flag(Entity eid, int flag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_trail_set_flag(Entity eid, int flag, bool value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_trail_get_edge(Entity eid, int end);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_trail_set_edge(Entity eid, int end, Vector3 point);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_trail_clear(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_trail_get_point_count(Entity eid);
    }
}
