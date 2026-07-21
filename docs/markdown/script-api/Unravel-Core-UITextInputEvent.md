<a id="uitextinputevent"></a>

# UITextInputEvent

> **Extends:** [`Unravel.Core.UIEventBase`](Unravel-Core-UIEventBase.md#uieventbase)

Represents a text input UI event for handling text entry. This is separate from key events to handle composed text input properly.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `string` | [`text`](#text-1)  | The text that was input. |
| `bool` | [`ctrlKey`](#ctrlkey-2)  | Whether the Ctrl key was held during the event. |
| `bool` | [`shiftKey`](#shiftkey-2)  | Whether the Shift key was held during the event. |
| `bool` | [`altKey`](#altkey-2)  | Whether the Alt key was held during the event. |
| `bool` | [`metaKey`](#metakey-2)  | Whether the Meta key (Windows/Cmd) was held during the event. |

---

<a id="text-1"></a>

### text

```java
string text = ""
```

The text that was input.

---

<a id="ctrlkey-2"></a>

### ctrlKey

```java
bool ctrlKey = false
```

Whether the Ctrl key was held during the event.

---

<a id="shiftkey-2"></a>

### shiftKey

```java
bool shiftKey = false
```

Whether the Shift key was held during the event.

---

<a id="altkey-2"></a>

### altKey

```java
bool altKey = false
```

Whether the Alt key was held during the event.

---

<a id="metakey-2"></a>

### metaKey

```java
bool metaKey = false
```

Whether the Meta key (Windows/Cmd) was held during the event.

