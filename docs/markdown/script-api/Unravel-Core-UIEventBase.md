<a id="uieventbase"></a>

# UIEventBase

> **Subclassed by:** [`Unravel.Core.UIChangeEvent`](Unravel-Core-UIChangeEvent.md#uichangeevent), [`Unravel.Core.UIKeyEvent`](Unravel-Core-UIKeyEvent.md#uikeyevent), [`Unravel.Core.UIPointerEvent`](Unravel-Core-UIPointerEvent.md#uipointerevent), [`Unravel.Core.UISliderEvent`](Unravel-Core-UISliderEvent.md#uisliderevent), [`Unravel.Core.UITextInputEvent`](Unravel-Core-UITextInputEvent.md#uitextinputevent)

Base type for UI events dispatched through [UIEventManager](Unravel-Core-UIEventManager.md#uieventmanager).

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `string` | [`targetElementId`](#targetelementid)  | The ID of the element that triggered the event. |
| `IntPtr` | [`targetElementPtr`](#targetelementptr)  | Native pointer to the element that triggered the event. |
| `string` | [`currentElementId`](#currentelementid)  | The ID of the element that received the event. |
| `IntPtr` | [`currentElementPtr`](#currentelementptr)  | Native pointer to the element that received the event. |
| `EventPhase` | [`phase`](#phase)  | Current dispatch phase of the event. |
| `string` | [`eventType`](#eventtype)  | The type of event that occurred (e.g., "click", "mousedown"). |

---

<a id="targetelementid"></a>

### targetElementId

```java
string targetElementId
```

The ID of the element that triggered the event.

---

<a id="targetelementptr"></a>

### targetElementPtr

```java
IntPtr targetElementPtr = IntPtr.Zero
```

Native pointer to the element that triggered the event.

---

<a id="currentelementid"></a>

### currentElementId

```java
string currentElementId
```

The ID of the element that received the event.

---

<a id="currentelementptr"></a>

### currentElementPtr

```java
IntPtr currentElementPtr = IntPtr.Zero
```

Native pointer to the element that received the event.

---

<a id="phase"></a>

### phase

```java
EventPhase phase = EventPhase.None
```

Current dispatch phase of the event.

---

<a id="eventtype"></a>

### eventType

```java
string eventType
```

The type of event that occurred (e.g., "click", "mousedown").

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`StopPropagation`](#stoppropagation) `inline` | Stops propagation of the event if it is interruptible, but finish all listeners on the current element. |
| `void` | [`StopImmediatePropagation`](#stopimmediatepropagation) `inline` | Stops propagation of the event if it is interruptible, including to any other listeners on the current element. |

---

<a id="stoppropagation"></a>

### StopPropagation

`inline`

```java
inline void StopPropagation()
```

Stops propagation of the event if it is interruptible, but finish all listeners on the current element.

---

<a id="stopimmediatepropagation"></a>

### StopImmediatePropagation

`inline`

```java
inline void StopImmediatePropagation()
```

Stops propagation of the event if it is interruptible, including to any other listeners on the current element.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `IntPtr` | [`nativePtr`](#nativeptr-2)  |  |

---

<a id="nativeptr-2"></a>

### nativePtr

```java
IntPtr nativePtr = IntPtr.Zero
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_ui_stop_propagation`](#internal_m2n_ui_stop_propagation)  |  |
| `void` | [`internal_m2n_ui_stop_immediate_propagation`](#internal_m2n_ui_stop_immediate_propagation)  |  |

---

<a id="internal_m2n_ui_stop_propagation"></a>

### internal_m2n_ui_stop_propagation

```java
void internal_m2n_ui_stop_propagation(IntPtr nativePtr)
```

---

<a id="internal_m2n_ui_stop_immediate_propagation"></a>

### internal_m2n_ui_stop_immediate_propagation

```java
void internal_m2n_ui_stop_immediate_propagation(IntPtr nativePtr)
```

