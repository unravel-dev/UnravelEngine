using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// A line: a strip through a list of points, drawn with a Surface material. A laser or a beam (two points a script
    /// moves), a rope, lightning, an aim arc, a path. The points are in the entity's space, or in the world's with
    /// <see cref="worldSpace"/>.
    /// </summary>
    public class LineComponent : StripComponent
    {
        internal override uint rendererSlot => MaterialInstance.LineSlot;

        /// <summary>How many points the line has.</summary>
        public int pointCount => internal_m2n_line_get_point_count(owner);

        /// <summary>Replaces the line's points. Null or an empty array leaves it without any. A line draws its first
        /// 4096 points, and leaves out a point that is no number.</summary>
        public void SetPoints(ReadOnlySpan<Vector3> points)
        {
            // A point is three floats in a row: the list goes over as its bytes.
            internal_m2n_line_set_points(owner, MemoryMarshal.AsBytes(points).ToArray());
        }

        /// <summary>A copy of the line's points.</summary>
        public Vector3[] GetPoints()
        {
            var values = internal_m2n_line_get_points(owner) ?? Array.Empty<float>();
            var points = new Vector3[values.Length / 3];
            for (int i = 0; i < points.Length; i++)
            {
                points[i] = new Vector3(values[i * 3], values[i * 3 + 1], values[i * 3 + 2]);
            }
            return points;
        }

        /// <summary>Moves point <paramref name="index"/>. An index the line does not have is ignored, and so is a
        /// point that is no number.</summary>
        public void SetPoint(int index, Vector3 point) => internal_m2n_line_set_point(owner, index, point);

        /// <summary>Point <paramref name="index"/>, or zero for an index the line does not have.</summary>
        public Vector3 GetPoint(int index) => internal_m2n_line_get_point(owner, index);

        /// <summary>The points are in the world's space: the line stays where it is when the entity moves. Off, they
        /// are in the entity's space.</summary>
        public bool worldSpace
        {
            get => internal_m2n_line_get_flag(owner, (int)Flag.WorldSpace);
            set => internal_m2n_line_set_flag(owner, (int)Flag.WorldSpace, value);
        }

        /// <summary>The last point joins the first.</summary>
        public bool loop
        {
            get => internal_m2n_line_get_flag(owner, (int)Flag.Loop);
            set => internal_m2n_line_set_flag(owner, (int)Flag.Loop, value);
        }

        private enum Flag
        {
            WorldSpace = 0,
            Loop = 1,
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float[] internal_m2n_line_get_points(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_line_set_points(Entity eid, byte[] values);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_line_get_point_count(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_line_get_point(Entity eid, int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_line_set_point(Entity eid, int index, Vector3 point);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_line_get_flag(Entity eid, int flag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_line_set_flag(Entity eid, int flag, bool value);
    }
}
