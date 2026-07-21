<a id="textcomponent"></a>

# TextComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Mirrors ace::text_component, letting scripts manage text rendering via properties.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `string` | [`text`](#text)  | The string to render. |
| `BufferType` | [`buffer`](#buffer)  | Chooses static/dynamic/transient vertex buffer. |
| `OverflowType` | [`overflow`](#overflow)  | Overflow handling (none, word, grapheme). |
| `Font` | [`font`](#font-1)  | [Font](Unravel-Core-Font.md#font) asset handle. |
| `uint` | [`fontSize`](#fontsize)  | Base font size in points. |
| `bool` | [`autoSize`](#autosize)  | Enables/disables automatic resizing. |
| `int` | [`renderFontSize`](#renderfontsize)  | Actual size used for rendering after auto-size. |
| `Vector2` | [`area`](#area)  | Layout area (width/height). |
| `Range< uint >` | [`autoSizeRange`](#autosizerange)  | Min/max font for auto-size. |
| `bool` | [`isRichText`](#isrichtext)  | Enables/disables rich-text parsing. |
| `Alignment` | [`alignment`](#alignment-1)  | Horizontal + vertical alignment flags. |
| `Vector2` | [`renderArea`](#renderarea)  | Exact area actually used for render. |
| `Bounds` | [`bounds`](#bounds-3)  | [Bounds](Bounds.md#bounds) of the text in local space. |
| `Bounds` | [`renderBounds`](#renderbounds)  | [Bounds](Bounds.md#bounds) of the text after layouts and scaling in world space. |
| `float` | [`opacity`](#opacity-1)  | Text opacity (0.0 to 1.0). |
| `Color` | [`color`](#color-8)  | Main text color. |
| `Color` | [`backgroundColor`](#backgroundcolor)  | Background color behind text. |
| `Color` | [`foregroundColor`](#foregroundcolor)  | Foreground color overlay. |
| `Color` | [`overlineColor`](#overlinecolor)  | [Color](Color.md#color) of overline decoration. |
| `Color` | [`underlineColor`](#underlinecolor)  | [Color](Color.md#color) of underline decoration. |
| `Color` | [`strikeColor`](#strikecolor)  | [Color](Color.md#color) of strikethrough decoration. |
| `Color` | [`outlineColor`](#outlinecolor)  | [Color](Color.md#color) of text outline. |
| `float` | [`outlineWidth`](#outlinewidth)  | Width of text outline. |
| `Vector2` | [`shadowOffsets`](#shadowoffsets)  | Shadow offset (x, y). |
| `Color` | [`shadowColor`](#shadowcolor)  | [Color](Color.md#color) of text shadow. |
| `float` | [`shadowSoftener`](#shadowsoftener)  | Shadow softness/blur amount. |
| `TextStyleFlags` | [`styleFlags`](#styleflags)  | Text style flags for decorations and effects. |

---

<a id="text"></a>

### text

```java
string text
```

The string to render.

---

<a id="buffer"></a>

### buffer

```java
BufferType buffer
```

Chooses static/dynamic/transient vertex buffer.

---

<a id="overflow"></a>

### overflow

```java
OverflowType overflow
```

Overflow handling (none, word, grapheme).

---

<a id="font-1"></a>

### font

```java
Font font
```

[Font](Unravel-Core-Font.md#font) asset handle.

---

<a id="fontsize"></a>

### fontSize

```java
uint fontSize
```

Base font size in points.

---

<a id="autosize"></a>

### autoSize

```java
bool autoSize
```

Enables/disables automatic resizing.

---

<a id="renderfontsize"></a>

### renderFontSize

```java
int renderFontSize
```

Actual size used for rendering after auto-size.

---

<a id="area"></a>

### area

```java
Vector2 area
```

Layout area (width/height).

---

<a id="autosizerange"></a>

### autoSizeRange

```java
Range< uint > autoSizeRange
```

Min/max font for auto-size.

---

<a id="isrichtext"></a>

### isRichText

```java
bool isRichText
```

Enables/disables rich-text parsing.

---

<a id="alignment-1"></a>

### alignment

```java
Alignment alignment
```

Horizontal + vertical alignment flags.

---

<a id="renderarea"></a>

### renderArea

```java
Vector2 renderArea
```

Exact area actually used for render.

---

<a id="bounds-3"></a>

### bounds

```java
Bounds bounds
```

[Bounds](Bounds.md#bounds) of the text in local space.

---

<a id="renderbounds"></a>

### renderBounds

```java
Bounds renderBounds
```

[Bounds](Bounds.md#bounds) of the text after layouts and scaling in world space.

---

<a id="opacity-1"></a>

### opacity

```java
float opacity
```

Text opacity (0.0 to 1.0).

---

<a id="color-8"></a>

### color

```java
Color color
```

Main text color.

---

<a id="backgroundcolor"></a>

### backgroundColor

```java
Color backgroundColor
```

Background color behind text.

---

<a id="foregroundcolor"></a>

### foregroundColor

```java
Color foregroundColor
```

Foreground color overlay.

---

<a id="overlinecolor"></a>

### overlineColor

```java
Color overlineColor
```

[Color](Color.md#color) of overline decoration.

---

<a id="underlinecolor"></a>

### underlineColor

```java
Color underlineColor
```

[Color](Color.md#color) of underline decoration.

---

<a id="strikecolor"></a>

### strikeColor

```java
Color strikeColor
```

[Color](Color.md#color) of strikethrough decoration.

---

<a id="outlinecolor"></a>

### outlineColor

```java
Color outlineColor
```

[Color](Color.md#color) of text outline.

---

<a id="outlinewidth"></a>

### outlineWidth

```java
float outlineWidth
```

Width of text outline.

---

<a id="shadowoffsets"></a>

### shadowOffsets

```java
Vector2 shadowOffsets
```

Shadow offset (x, y).

---

<a id="shadowcolor"></a>

### shadowColor

```java
Color shadowColor
```

[Color](Color.md#color) of text shadow.

---

<a id="shadowsoftener"></a>

### shadowSoftener

```java
float shadowSoftener
```

Shadow softness/blur amount.

---

<a id="styleflags"></a>

### styleFlags

```java
TextStyleFlags styleFlags
```

Text style flags for decorations and effects.

## Public Types

| Name | Description |
|------|-------------|
| [`BufferType`](#buffertype)  | Define the storage type for the vertex/index buffers. |
| [`OverflowType`](#overflowtype)  | Defines the overflow behaviour. |

---

<a id="buffertype"></a>

### BufferType

```java
enum BufferType
```

Define the storage type for the vertex/index buffers.

| Value | Description |
|-------|-------------|
| `StaticBuffer` |  |
| `DynamicBuffer` |  |
| `TransientBuffer` |  |

---

<a id="overflowtype"></a>

### OverflowType

```java
enum OverflowType
```

Defines the overflow behaviour.

| Value | Description |
|-------|-------------|
| `None` |  |
| `Word` |  |
| `Grapheme` |  |

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Font` | [`font_`](#font_)  |  |

---

<a id="font_"></a>

### font_

```java
Font font_
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_text_set_text`](#internal_m2n_text_set_text)  |  |
| `string` | [`internal_m2n_text_get_text`](#internal_m2n_text_get_text)  |  |
| `void` | [`internal_m2n_text_set_buffer_type`](#internal_m2n_text_set_buffer_type)  |  |
| `BufferType` | [`internal_m2n_text_get_buffer_type`](#internal_m2n_text_get_buffer_type)  |  |
| `void` | [`internal_m2n_text_set_overflow_type`](#internal_m2n_text_set_overflow_type)  |  |
| `OverflowType` | [`internal_m2n_text_get_overflow_type`](#internal_m2n_text_get_overflow_type)  |  |
| `void` | [`internal_m2n_text_set_font`](#internal_m2n_text_set_font)  |  |
| `Guid` | [`internal_m2n_text_get_font`](#internal_m2n_text_get_font)  |  |
| `void` | [`internal_m2n_text_set_font_size`](#internal_m2n_text_set_font_size)  |  |
| `uint` | [`internal_m2n_text_get_font_size`](#internal_m2n_text_get_font_size)  |  |
| `void` | [`internal_m2n_text_set_auto_size`](#internal_m2n_text_set_auto_size)  |  |
| `bool` | [`internal_m2n_text_get_auto_size`](#internal_m2n_text_get_auto_size)  |  |
| `int` | [`internal_m2n_text_get_render_font_size`](#internal_m2n_text_get_render_font_size)  |  |
| `void` | [`internal_m2n_text_set_area`](#internal_m2n_text_set_area)  |  |
| `Vector2` | [`internal_m2n_text_get_area`](#internal_m2n_text_get_area)  |  |
| `Vector2` | [`internal_m2n_text_get_render_area`](#internal_m2n_text_get_render_area)  |  |
| `void` | [`internal_m2n_text_set_auto_size_range`](#internal_m2n_text_set_auto_size_range)  |  |
| `Range< uint >` | [`internal_m2n_text_get_auto_size_range`](#internal_m2n_text_get_auto_size_range)  |  |
| `void` | [`internal_m2n_text_set_is_rich_text`](#internal_m2n_text_set_is_rich_text)  |  |
| `bool` | [`internal_m2n_text_get_is_rich_text`](#internal_m2n_text_get_is_rich_text)  |  |
| `void` | [`internal_m2n_text_set_alignment`](#internal_m2n_text_set_alignment)  |  |
| `Alignment` | [`internal_m2n_text_get_alignment`](#internal_m2n_text_get_alignment)  |  |
| `Bounds` | [`internal_m2n_text_get_bounds`](#internal_m2n_text_get_bounds)  |  |
| `Bounds` | [`internal_m2n_text_get_render_bounds`](#internal_m2n_text_get_render_bounds)  |  |
| `void` | [`internal_m2n_text_set_opacity`](#internal_m2n_text_set_opacity)  |  |
| `float` | [`internal_m2n_text_get_opacity`](#internal_m2n_text_get_opacity)  |  |
| `void` | [`internal_m2n_text_set_text_color`](#internal_m2n_text_set_text_color)  |  |
| `Color` | [`internal_m2n_text_get_text_color`](#internal_m2n_text_get_text_color)  |  |
| `void` | [`internal_m2n_text_set_background_color`](#internal_m2n_text_set_background_color)  |  |
| `Color` | [`internal_m2n_text_get_background_color`](#internal_m2n_text_get_background_color)  |  |
| `void` | [`internal_m2n_text_set_foreground_color`](#internal_m2n_text_set_foreground_color)  |  |
| `Color` | [`internal_m2n_text_get_foreground_color`](#internal_m2n_text_get_foreground_color)  |  |
| `void` | [`internal_m2n_text_set_overline_color`](#internal_m2n_text_set_overline_color)  |  |
| `Color` | [`internal_m2n_text_get_overline_color`](#internal_m2n_text_get_overline_color)  |  |
| `void` | [`internal_m2n_text_set_underline_color`](#internal_m2n_text_set_underline_color)  |  |
| `Color` | [`internal_m2n_text_get_underline_color`](#internal_m2n_text_get_underline_color)  |  |
| `void` | [`internal_m2n_text_set_strike_color`](#internal_m2n_text_set_strike_color)  |  |
| `Color` | [`internal_m2n_text_get_strike_color`](#internal_m2n_text_get_strike_color)  |  |
| `void` | [`internal_m2n_text_set_outline_color`](#internal_m2n_text_set_outline_color)  |  |
| `Color` | [`internal_m2n_text_get_outline_color`](#internal_m2n_text_get_outline_color)  |  |
| `void` | [`internal_m2n_text_set_outline_width`](#internal_m2n_text_set_outline_width)  |  |
| `float` | [`internal_m2n_text_get_outline_width`](#internal_m2n_text_get_outline_width)  |  |
| `void` | [`internal_m2n_text_set_shadow_offsets`](#internal_m2n_text_set_shadow_offsets)  |  |
| `Vector2` | [`internal_m2n_text_get_shadow_offsets`](#internal_m2n_text_get_shadow_offsets)  |  |
| `void` | [`internal_m2n_text_set_shadow_color`](#internal_m2n_text_set_shadow_color)  |  |
| `Color` | [`internal_m2n_text_get_shadow_color`](#internal_m2n_text_get_shadow_color)  |  |
| `void` | [`internal_m2n_text_set_shadow_softener`](#internal_m2n_text_set_shadow_softener)  |  |
| `float` | [`internal_m2n_text_get_shadow_softener`](#internal_m2n_text_get_shadow_softener)  |  |
| `void` | [`internal_m2n_text_set_style_flags`](#internal_m2n_text_set_style_flags)  |  |
| `uint` | [`internal_m2n_text_get_style_flags`](#internal_m2n_text_get_style_flags)  |  |

---

<a id="internal_m2n_text_set_text"></a>

### internal_m2n_text_set_text

```java
void internal_m2n_text_set_text(Entity eid, string text)
```

---

<a id="internal_m2n_text_get_text"></a>

### internal_m2n_text_get_text

```java
string internal_m2n_text_get_text(Entity eid)
```

---

<a id="internal_m2n_text_set_buffer_type"></a>

### internal_m2n_text_set_buffer_type

```java
void internal_m2n_text_set_buffer_type(Entity eid, BufferType type)
```

---

<a id="internal_m2n_text_get_buffer_type"></a>

### internal_m2n_text_get_buffer_type

```java
BufferType internal_m2n_text_get_buffer_type(Entity eid)
```

---

<a id="internal_m2n_text_set_overflow_type"></a>

### internal_m2n_text_set_overflow_type

```java
void internal_m2n_text_set_overflow_type(Entity eid, OverflowType type)
```

---

<a id="internal_m2n_text_get_overflow_type"></a>

### internal_m2n_text_get_overflow_type

```java
OverflowType internal_m2n_text_get_overflow_type(Entity eid)
```

---

<a id="internal_m2n_text_set_font"></a>

### internal_m2n_text_set_font

```java
void internal_m2n_text_set_font(Entity eid, Guid fontHandle)
```

---

<a id="internal_m2n_text_get_font"></a>

### internal_m2n_text_get_font

```java
Guid internal_m2n_text_get_font(Entity eid)
```

---

<a id="internal_m2n_text_set_font_size"></a>

### internal_m2n_text_set_font_size

```java
void internal_m2n_text_set_font_size(Entity eid, uint size)
```

---

<a id="internal_m2n_text_get_font_size"></a>

### internal_m2n_text_get_font_size

```java
uint internal_m2n_text_get_font_size(Entity eid)
```

---

<a id="internal_m2n_text_set_auto_size"></a>

### internal_m2n_text_set_auto_size

```java
void internal_m2n_text_set_auto_size(Entity eid, bool autoSize)
```

---

<a id="internal_m2n_text_get_auto_size"></a>

### internal_m2n_text_get_auto_size

```java
bool internal_m2n_text_get_auto_size(Entity eid)
```

---

<a id="internal_m2n_text_get_render_font_size"></a>

### internal_m2n_text_get_render_font_size

```java
int internal_m2n_text_get_render_font_size(Entity eid)
```

---

<a id="internal_m2n_text_set_area"></a>

### internal_m2n_text_set_area

```java
void internal_m2n_text_set_area(Entity eid, Vector2 area)
```

---

<a id="internal_m2n_text_get_area"></a>

### internal_m2n_text_get_area

```java
Vector2 internal_m2n_text_get_area(Entity eid)
```

---

<a id="internal_m2n_text_get_render_area"></a>

### internal_m2n_text_get_render_area

```java
Vector2 internal_m2n_text_get_render_area(Entity eid)
```

---

<a id="internal_m2n_text_set_auto_size_range"></a>

### internal_m2n_text_set_auto_size_range

```java
void internal_m2n_text_set_auto_size_range(Entity eid, Range< uint > range)
```

---

<a id="internal_m2n_text_get_auto_size_range"></a>

### internal_m2n_text_get_auto_size_range

```java
Range< uint > internal_m2n_text_get_auto_size_range(Entity eid)
```

---

<a id="internal_m2n_text_set_is_rich_text"></a>

### internal_m2n_text_set_is_rich_text

```java
void internal_m2n_text_set_is_rich_text(Entity eid, bool isRich)
```

---

<a id="internal_m2n_text_get_is_rich_text"></a>

### internal_m2n_text_get_is_rich_text

```java
bool internal_m2n_text_get_is_rich_text(Entity eid)
```

---

<a id="internal_m2n_text_set_alignment"></a>

### internal_m2n_text_set_alignment

```java
void internal_m2n_text_set_alignment(Entity eid, Alignment align)
```

---

<a id="internal_m2n_text_get_alignment"></a>

### internal_m2n_text_get_alignment

```java
Alignment internal_m2n_text_get_alignment(Entity eid)
```

---

<a id="internal_m2n_text_get_bounds"></a>

### internal_m2n_text_get_bounds

```java
Bounds internal_m2n_text_get_bounds(Entity eid)
```

---

<a id="internal_m2n_text_get_render_bounds"></a>

### internal_m2n_text_get_render_bounds

```java
Bounds internal_m2n_text_get_render_bounds(Entity eid)
```

---

<a id="internal_m2n_text_set_opacity"></a>

### internal_m2n_text_set_opacity

```java
void internal_m2n_text_set_opacity(Entity eid, float opacity)
```

---

<a id="internal_m2n_text_get_opacity"></a>

### internal_m2n_text_get_opacity

```java
float internal_m2n_text_get_opacity(Entity eid)
```

---

<a id="internal_m2n_text_set_text_color"></a>

### internal_m2n_text_set_text_color

```java
void internal_m2n_text_set_text_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_text_color"></a>

### internal_m2n_text_get_text_color

```java
Color internal_m2n_text_get_text_color(Entity eid)
```

---

<a id="internal_m2n_text_set_background_color"></a>

### internal_m2n_text_set_background_color

```java
void internal_m2n_text_set_background_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_background_color"></a>

### internal_m2n_text_get_background_color

```java
Color internal_m2n_text_get_background_color(Entity eid)
```

---

<a id="internal_m2n_text_set_foreground_color"></a>

### internal_m2n_text_set_foreground_color

```java
void internal_m2n_text_set_foreground_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_foreground_color"></a>

### internal_m2n_text_get_foreground_color

```java
Color internal_m2n_text_get_foreground_color(Entity eid)
```

---

<a id="internal_m2n_text_set_overline_color"></a>

### internal_m2n_text_set_overline_color

```java
void internal_m2n_text_set_overline_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_overline_color"></a>

### internal_m2n_text_get_overline_color

```java
Color internal_m2n_text_get_overline_color(Entity eid)
```

---

<a id="internal_m2n_text_set_underline_color"></a>

### internal_m2n_text_set_underline_color

```java
void internal_m2n_text_set_underline_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_underline_color"></a>

### internal_m2n_text_get_underline_color

```java
Color internal_m2n_text_get_underline_color(Entity eid)
```

---

<a id="internal_m2n_text_set_strike_color"></a>

### internal_m2n_text_set_strike_color

```java
void internal_m2n_text_set_strike_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_strike_color"></a>

### internal_m2n_text_get_strike_color

```java
Color internal_m2n_text_get_strike_color(Entity eid)
```

---

<a id="internal_m2n_text_set_outline_color"></a>

### internal_m2n_text_set_outline_color

```java
void internal_m2n_text_set_outline_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_outline_color"></a>

### internal_m2n_text_get_outline_color

```java
Color internal_m2n_text_get_outline_color(Entity eid)
```

---

<a id="internal_m2n_text_set_outline_width"></a>

### internal_m2n_text_set_outline_width

```java
void internal_m2n_text_set_outline_width(Entity eid, float width)
```

---

<a id="internal_m2n_text_get_outline_width"></a>

### internal_m2n_text_get_outline_width

```java
float internal_m2n_text_get_outline_width(Entity eid)
```

---

<a id="internal_m2n_text_set_shadow_offsets"></a>

### internal_m2n_text_set_shadow_offsets

```java
void internal_m2n_text_set_shadow_offsets(Entity eid, Vector2 offsets)
```

---

<a id="internal_m2n_text_get_shadow_offsets"></a>

### internal_m2n_text_get_shadow_offsets

```java
Vector2 internal_m2n_text_get_shadow_offsets(Entity eid)
```

---

<a id="internal_m2n_text_set_shadow_color"></a>

### internal_m2n_text_set_shadow_color

```java
void internal_m2n_text_set_shadow_color(Entity eid, Color color)
```

---

<a id="internal_m2n_text_get_shadow_color"></a>

### internal_m2n_text_get_shadow_color

```java
Color internal_m2n_text_get_shadow_color(Entity eid)
```

---

<a id="internal_m2n_text_set_shadow_softener"></a>

### internal_m2n_text_set_shadow_softener

```java
void internal_m2n_text_set_shadow_softener(Entity eid, float softener)
```

---

<a id="internal_m2n_text_get_shadow_softener"></a>

### internal_m2n_text_get_shadow_softener

```java
float internal_m2n_text_get_shadow_softener(Entity eid)
```

---

<a id="internal_m2n_text_set_style_flags"></a>

### internal_m2n_text_set_style_flags

```java
void internal_m2n_text_set_style_flags(Entity eid, uint flags)
```

---

<a id="internal_m2n_text_get_style_flags"></a>

### internal_m2n_text_get_style_flags

```java
uint internal_m2n_text_get_style_flags(Entity eid)
```

