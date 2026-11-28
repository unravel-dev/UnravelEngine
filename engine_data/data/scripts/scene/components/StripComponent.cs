using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// What a <see cref="LineComponent"/> and a <see cref="TrailComponent"/> share: the strip they draw through their
    /// points, with its shape, its width and colour along its length, and how a texture runs along it. Position 0 of
    /// the two curves is the strip's start (a line's first point, a trail's head at the entity) and 1 its end.
    /// </summary>
    public abstract class StripComponent : DynamicRendererComponent
    {
        /// <summary>The strip's width in metres: a tube's diameter.</summary>
        public float width
        {
            get => internal_m2n_strip_get_number(owner, rendererSlot, (int)StripNumber.Width);
            set => internal_m2n_strip_set_number(owner, rendererSlot, (int)StripNumber.Width, value);
        }

        /// <summary>Scales <see cref="width"/> along the strip, read at every cross-section: 1 to 0 tapers it to a
        /// point at its end.</summary>
        public Curve widthOverLength
        {
            get => new Curve(internal_m2n_strip_get_width_over_length(owner, rendererSlot).ToStructArray<CurveKey>());
            set => internal_m2n_strip_set_width_over_length(owner, rendererSlot, RequireKeys(value?.keys, nameof(widthOverLength)).ToByteArray());
        }

        /// <summary>The colour and opacity along the strip, written to its vertex colour. A material shows it through
        /// its Vertex Color node, as the engine's strip material does.</summary>
        public Gradient colorOverLength
        {
            get => new Gradient(internal_m2n_strip_get_color_over_length(owner, rendererSlot).ToStructArray<GradientKey>());
            set => internal_m2n_strip_set_color_over_length(owner, rendererSlot, RequireKeys(value?.keys, nameof(colorOverLength)).ToByteArray());
        }

        /// <summary>How the strip is built around its points.</summary>
        public StripShape shape
        {
            get => (StripShape)internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.Shape);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.Shape, (int)value);
        }

        /// <summary>What the face of a flat or rounded strip turns toward.</summary>
        public StripAlignment alignment
        {
            get => (StripAlignment)internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.Alignment);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.Alignment, (int)value);
        }

        /// <summary>The direction the strip's face turns toward, for <see cref="StripAlignment.LocalAxis"/> and
        /// <see cref="StripAlignment.WorldAxis"/>.</summary>
        public Vector3 axis
        {
            get => internal_m2n_strip_get_axis(owner, rendererSlot);
            set => internal_m2n_strip_set_axis(owner, rendererSlot, value);
        }

        /// <summary>How a texture runs along the strip.</summary>
        public StripUVMode uvMode
        {
            get => (StripUVMode)internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.UVMode);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.UVMode, (int)value);
        }

        /// <summary>Metres of strip per repeat of the texture, for <see cref="StripUVMode.Tile"/>.</summary>
        public float tileLength
        {
            get => internal_m2n_strip_get_number(owner, rendererSlot, (int)StripNumber.TileLength);
            set => internal_m2n_strip_set_number(owner, rendererSlot, (int)StripNumber.TileLength, value);
        }

        /// <summary>Points added between each pair along a smooth curve through them, 0 to 16. 0 keeps the segments
        /// straight.</summary>
        public int subdivisions
        {
            get => internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.Subdivisions);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.Subdivisions, value);
        }

        /// <summary>Cross-sections added around each sharp corner of a flat or rounded strip, 0 to 16.</summary>
        public int cornerVertices
        {
            get => internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.CornerVertices);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.CornerVertices, value);
        }

        /// <summary>Vertices of the half disc that closes each end of a flat or rounded strip, 0 to 16. Any number
        /// closes a tube's ends.</summary>
        public int capVertices
        {
            get => internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.CapVertices);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.CapVertices, value);
        }

        /// <summary>The sides of a tube's ring, 3 to 32.</summary>
        public int tubeSides
        {
            get => internal_m2n_strip_get_setting(owner, rendererSlot, (int)StripSetting.TubeSides);
            set => internal_m2n_strip_set_setting(owner, rendererSlot, (int)StripSetting.TubeSides, value);
        }

        private static T[] RequireKeys<T>(T[] keys, string property)
        {
            if (keys == null || keys.Length == 0)
            {
                throw new ArgumentException($"{property} needs at least one key", property);
            }
            return keys;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_strip_get_setting(Entity eid, uint slot, int setting);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_strip_set_setting(Entity eid, uint slot, int setting, int value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_strip_get_number(Entity eid, uint slot, int number);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_strip_set_number(Entity eid, uint slot, int number, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 internal_m2n_strip_get_axis(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_strip_set_axis(Entity eid, uint slot, Vector3 axis);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_strip_get_width_over_length(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_strip_set_width_over_length(Entity eid, uint slot, byte[] keys);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern byte[] internal_m2n_strip_get_color_over_length(Entity eid, uint slot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_strip_set_color_over_length(Entity eid, uint slot, byte[] keys);
    }
}
