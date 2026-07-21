<a id="uikeyevent"></a>

# UIKeyEvent

> **Extends:** [`Unravel.Core.UIEventBase`](Unravel-Core-UIEventBase.md#uieventbase)

Represents a keyboard-related UI event with key-specific properties. Simplified to only contain key code and modifier keys.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `KeyCode` | [`Key`](#key)  | Convenience property for the key. |
| `bool` | [`HasModifiers`](#hasmodifiers)  | Whether any modifier keys were held during the event. |

---

<a id="key"></a>

### Key

```java
KeyCode Key
```

Convenience property for the key.

---

<a id="hasmodifiers"></a>

### HasModifiers

```java
bool HasModifiers
```

Whether any modifier keys were held during the event.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `KeyCode` | [`keyCode`](#keycode-1)  | The key that was pressed or released. |
| `bool` | [`ctrlKey`](#ctrlkey)  | Whether the Ctrl key was held. |
| `bool` | [`shiftKey`](#shiftkey)  | Whether the Shift key was held. |
| `bool` | [`altKey`](#altkey)  | Whether the Alt key was held. |
| `bool` | [`metaKey`](#metakey)  | Whether the Meta key (Windows/Cmd) was held. |

---

<a id="keycode-1"></a>

### keyCode

```java
KeyCode keyCode = KeyCode.Unknown
```

The key that was pressed or released.

---

<a id="ctrlkey"></a>

### ctrlKey

```java
bool ctrlKey = false
```

Whether the Ctrl key was held.

---

<a id="shiftkey"></a>

### shiftKey

```java
bool shiftKey = false
```

Whether the Shift key was held.

---

<a id="altkey"></a>

### altKey

```java
bool altKey = false
```

Whether the Alt key was held.

---

<a id="metakey"></a>

### metaKey

```java
bool metaKey = false
```

Whether the Meta key (Windows/Cmd) was held.

