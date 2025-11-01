using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
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
    /// Text style flags for decorations and effects, matching gfx::text_style_flags.
    /// </summary>
    [Flags]
    public enum TextStyleFlags : uint
    {
        Normal         = 0,
        Overline       = 1,
        Underline      = 1 << 1,
        StrikeThrough  = 1 << 2,
        Background     = 1 << 3,
        Foreground     = 1 << 4,
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

        // ==== Text Style Properties ====

        /// <summary> Text opacity (0.0 to 1.0). </summary>
        public float opacity
        {
            get => internal_m2n_text_get_opacity(owner);
            set => internal_m2n_text_set_opacity(owner, value);
        }

        /// <summary> Main text color. </summary>
        public Color color
        {
            get => internal_m2n_text_get_text_color(owner);
            set => internal_m2n_text_set_text_color(owner, value);
        }

        /// <summary> Background color behind text. </summary>
        public Color backgroundColor
        {
            get => internal_m2n_text_get_background_color(owner);
            set => internal_m2n_text_set_background_color(owner, value);
        }

        /// <summary> Foreground color overlay. </summary>
        public Color foregroundColor
        {
            get => internal_m2n_text_get_foreground_color(owner);
            set => internal_m2n_text_set_foreground_color(owner, value);
        }

        /// <summary> Color of overline decoration. </summary>
        public Color overlineColor
        {
            get => internal_m2n_text_get_overline_color(owner);
            set => internal_m2n_text_set_overline_color(owner, value);
        }

        /// <summary> Color of underline decoration. </summary>
        public Color underlineColor
        {
            get => internal_m2n_text_get_underline_color(owner);
            set => internal_m2n_text_set_underline_color(owner, value);
        }

        /// <summary> Color of strikethrough decoration. </summary>
        public Color strikeColor
        {
            get => internal_m2n_text_get_strike_color(owner);
            set => internal_m2n_text_set_strike_color(owner, value);
        }

        /// <summary> Color of text outline. </summary>
        public Color outlineColor
        {
            get => internal_m2n_text_get_outline_color(owner);
            set => internal_m2n_text_set_outline_color(owner, value);
        }

        /// <summary> Width of text outline. </summary>
        public float outlineWidth
        {
            get => internal_m2n_text_get_outline_width(owner);
            set => internal_m2n_text_set_outline_width(owner, value);
        }

        /// <summary> Shadow offset (x, y). </summary>
        public Vector2 shadowOffsets
        {
            get => internal_m2n_text_get_shadow_offsets(owner);
            set => internal_m2n_text_set_shadow_offsets(owner, value);
        }

        /// <summary> Color of text shadow. </summary>
        public Color shadowColor
        {
            get => internal_m2n_text_get_shadow_color(owner);
            set => internal_m2n_text_set_shadow_color(owner, value);
        }

        /// <summary> Shadow softness/blur amount. </summary>
        public float shadowSoftener
        {
            get => internal_m2n_text_get_shadow_softener(owner);
            set => internal_m2n_text_set_shadow_softener(owner, value);
        }

        /// <summary> Text style flags for decorations and effects. </summary>
        public TextStyleFlags styleFlags
        {
            get => (TextStyleFlags)internal_m2n_text_get_style_flags(owner);
            set => internal_m2n_text_set_style_flags(owner, (uint)value);
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

        // ==== Text Style Internal Calls ====

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_opacity(Entity eid, float opacity);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_text_get_opacity(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_text_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_text_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_background_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_background_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_foreground_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_foreground_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_overline_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_overline_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_underline_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_underline_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_strike_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_strike_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_outline_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_outline_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_outline_width(Entity eid, float width);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_text_get_outline_width(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_shadow_offsets(Entity eid, Vector2 offsets);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_text_get_shadow_offsets(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_shadow_color(Entity eid, Color color);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Color internal_m2n_text_get_shadow_color(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_shadow_softener(Entity eid, float softener);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_text_get_shadow_softener(Entity eid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_text_set_style_flags(Entity eid, uint flags);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint internal_m2n_text_get_style_flags(Entity eid);
    }
}