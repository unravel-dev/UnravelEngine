<a id="uipointerevent"></a>

# UIPointerEvent

> **Extends:** [`Unravel.Core.UIEventBase`](Unravel-Core-UIEventBase.md#uieventbase)

Represents a pointer-related UI event with pointer-specific properties. Generic enough to handle mouse, touch, pen, and other pointer devices.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`x`](#x-4)  | The X coordinate of the pointer relative to the element. |
| `float` | [`y`](#y-4)  | The Y coordinate of the pointer relative to the element. |
| `int` | [`button`](#button)  | Which button was pressed (0=primary, 1=middle, 2=secondary). Only available for button events, -1 for non-button events. |
| `bool` | [`ctrlKey`](#ctrlkey-1)  | Whether the Ctrl key was held during the event. |
| `bool` | [`shiftKey`](#shiftkey-1)  | Whether the Shift key was held during the event. |
| `bool` | [`altKey`](#altkey-1)  | Whether the Alt key was held during the event. |
| `bool` | [`metaKey`](#metakey-1)  | Whether the Meta key (Windows/Cmd) was held during the event. |
| `float` | [`deltaX`](#deltax)  | The horizontal scroll delta for wheel/scroll events. Only available for scroll events. |
| `float` | [`deltaY`](#deltay)  | The vertical scroll delta for wheel/scroll events. Only available for scroll events. |

---

<a id="x-4"></a>

### x

```java
float x = 0.0f
```

The X coordinate of the pointer relative to the element.

---

<a id="y-4"></a>

### y

```java
float y = 0.0f
```

The Y coordinate of the pointer relative to the element.

---

<a id="button"></a>

### button

```java
int button = -1
```

Which button was pressed (0=primary, 1=middle, 2=secondary). Only available for button events, -1 for non-button events.

---

<a id="ctrlkey-1"></a>

### ctrlKey

```java
bool ctrlKey = false
```

Whether the Ctrl key was held during the event.

---

<a id="shiftkey-1"></a>

### shiftKey

```java
bool shiftKey = false
```

Whether the Shift key was held during the event.

---

<a id="altkey-1"></a>

### altKey

```java
bool altKey = false
```

Whether the Alt key was held during the event.

---

<a id="metakey-1"></a>

### metaKey

```java
bool metaKey = false
```

Whether the Meta key (Windows/Cmd) was held during the event.

---

<a id="deltax"></a>

### deltaX

```java
float deltaX = 0.0f
```

The horizontal scroll delta for wheel/scroll events. Only available for scroll events.

---

<a id="deltay"></a>

### deltaY

```java
float deltaY = 0.0f
```

The vertical scroll delta for wheel/scroll events. Only available for scroll events.

