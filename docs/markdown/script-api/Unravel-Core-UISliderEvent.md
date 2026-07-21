<a id="uisliderevent"></a>

# UISliderEvent

> **Extends:** [`Unravel.Core.UIEventBase`](Unravel-Core-UIEventBase.md#uieventbase)

Represents a slider UI event for handling slider value changes. This is separate from key events to handle composed slider value changes properly.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`value`](#value-2)  | The value that was input. |
| `float` | [`minValue`](#minvalue)  | Whether the Min value was held during the event. |
| `float` | [`maxValue`](#maxvalue)  | Whether the Max value was held during the event. |
| `float` | [`step`](#step-1)  | Whether the Step value was held during the event. |

---

<a id="value-2"></a>

### value

```java
float value = 0
```

The value that was input.

---

<a id="minvalue"></a>

### minValue

```java
float minValue = 0
```

Whether the Min value was held during the event.

---

<a id="maxvalue"></a>

### maxValue

```java
float maxValue = 0
```

Whether the Max value was held during the event.

---

<a id="step-1"></a>

### step

```java
float step = 0
```

Whether the Step value was held during the event.

