using System;
using System.Runtime.CompilerServices;

namespace Ace.Core
{
    /// <summary>
    /// Flags for horizontal and vertical text alignment, matching ace::align.
    /// </summary>
    [Flags]
    public enum Alignment : uint
    {
        Invalid          = 0,
        // Horizontal align (general)
        Left             = 1 << 0,
        Center           = 1 << 1,
        Right            = 1 << 2,
        HorizontalMask   = Left | Center | Right,

        // Vertical align (general)
        Top              = 1 << 3,
        Middle           = 1 << 4,
        Bottom           = 1 << 5,
        VerticalMask     = Top | Middle | Bottom ,
        // Text-specific (commented by default)
        Capline          = 1 << 6,
        Midline          = 1 << 7,
        Baseline         = 1 << 8,
        TypographicMask  = Capline | Midline | Baseline,
        VerticalTextMask = VerticalMask | TypographicMask,
    }

    /// <summary>
    /// Mirrors ace::text_component, letting scripts manage text rendering via properties.
    /// </summary>
    public class TextComponent : Component
    {
        private Font font_;
        /// <summary> Define the storage type for the vertex/index buffers </summary>
        public enum BufferType : uint
        {
            StaticBuffer    = 0,
            DynamicBuffer   = 1,
            TransientBuffer = 2
        }

        /// <summary> Defines the overflow behaviour </summary>
        public enum OverflowType : uint
        {
            None     = 0,
            Word     = 1,
            Grapheme = 2
        }

        /// <summary> The string to render. </summary>
        public string text
        {
            get => internal_m2n_text_get_text(owner);
            set => internal_m2n_text_set_text(owner, value);
        }

        /// <summary> Chooses static/dynamic/transient vertex buffer. </summary>
        public BufferType buffer
        {
            get => internal_m2n_text_get_buffer_type(owner);
            set => internal_m2n_text_set_buffer_type(owner, value);
        }

        /// <summary> Overflow handling (none, word, grapheme). </summary>
        public OverflowType overflow
        {
            get => internal_m2n_text_get_overflow_type(owner);
            set => internal_m2n_text_set_overflow_type(owner, value);
        }

        /// <summary> Font asset handle. </summary>
        public Font font
        {
            get
            {
                var uid = internal_m2n_text_get_font(owner);

                if (uid == Guid.Empty)
                {
                    font_ = null;
                }
                else if (font_ == null || font_.uid != uid)
                {
                    font_ = new Font { uid = uid };
                }

                return font_;
            }
            set
            {
                font_ = value;
                internal_m2n_text_set_font(owner, font_?.uid ?? Guid.Empty);
            }
        }

        /// <summary> Base font size in points. </summary>
        public uint fontSize
        {
            get => internal_m2n_text_get_font_size(owner);
            set => internal_m2n_text_set_font_size(owner, value);
        }

        /// <summary> Enables/disables automatic resizing. </summary>
        public bool autoSize
        {
            get => internal_m2n_text_get_auto_size(owner);
            set => internal_m2n_text_set_auto_size(owner, value);
        }

        /// <summary> Actual size used for rendering after auto-size. </summary>
        public int renderFontSize
        {
            get => internal_m2n_text_get_render_font_size(owner);
        }

        /// <summary> Layout area (width/height). </summary>
        public Vector2 area
        {
            get => internal_m2n_text_get_area(owner);
            set => internal_m2n_text_set_area(owner, value);
        }

        /// <summary> Min/max font for auto-size. </summary>
        public Range<uint> autoSizeRange
        {
             get => internal_m2n_text_get_auto_size_range(owner);
             set => internal_m2n_text_set_auto_size_range(owner, value);
         }

        /// <summary> Enables/disables rich-text parsing. </summary>
        public bool isRichText
        {
            get => internal_m2n_text_get_is_rich_text(owner);
            set => internal_m2n_text_set_is_rich_text(owner, value);
        }

        /// <summary> Horizontal + vertical alignment flags. </summary>
        public Alignment alignment
        {
            get => internal_m2n_text_get_alignment(owner);
            set => internal_m2n_text_set_alignment(owner, value);
        }

        /// <summary> Exact area actually used for render. </summary>
        public Vector2 renderArea
        {
            get => internal_m2n_text_get_render_area(owner);
        }

        /// <summary> Bounds of the text in local space. </summary>
        public Bounds bounds
        {
            get => internal_m2n_text_get_bounds(owner);
        }

        /// <summary> Bounds of the text after layouts and scaling in world space. </summary>
        public Bounds renderBounds
        {
            get => internal_m2n_text_get_render_bounds(owner);
        }

        // ==== Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_text(Entity eid, string text);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_text_get_text(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_buffer_type(Entity eid, BufferType type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern BufferType internal_m2n_text_get_buffer_type(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_overflow_type(Entity eid, OverflowType type);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern OverflowType internal_m2n_text_get_overflow_type(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_font(Entity eid, Guid fontHandle);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Guid internal_m2n_text_get_font(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_font_size(Entity eid, uint size);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_text_get_font_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_auto_size(Entity eid, bool autoSize);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_text_get_auto_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_text_get_render_font_size(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_area(Entity eid, Vector2 area);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_text_get_area(Entity eid);

        
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_text_get_render_area(Entity eid);


        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_auto_size_range(Entity eid, Range<uint> range);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Range<uint> internal_m2n_text_get_auto_size_range(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_is_rich_text(Entity eid, bool isRich);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_text_get_is_rich_text(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_alignment(Entity eid, Alignment align);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Alignment internal_m2n_text_get_alignment(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Bounds internal_m2n_text_get_bounds(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Bounds internal_m2n_text_get_render_bounds(Entity eid);
    }
}