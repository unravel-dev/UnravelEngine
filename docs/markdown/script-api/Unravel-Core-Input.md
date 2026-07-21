<a id="input"></a>

# Input

Provides static methods to handle user input actions such as button presses and axis values.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Vector2` | [`mousePosition`](#mouseposition) `static` | Gets the current mouse position in screen coordinates. |

---

<a id="mouseposition"></a>

### mousePosition

`static`

```java
Vector2 mousePosition
```

Gets the current mouse position in screen coordinates.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`GetAxis`](#getaxis) `static` `inline` | Gets the analog value of an input axis. |
| `float` | [`GetAnalogValue`](#getanalogvalue) `static` `inline` | Gets the analog value associated with the specified input action. |
| `bool` | [`GetDigitalValue`](#getdigitalvalue) `static` `inline` | Gets the digital (boolean) value associated with the specified input action. |
| `bool` | [`IsPressed`](#ispressed) `static` `inline` | Checks if the specified action's button was pressed during the current frame. |
| `bool` | [`IsReleased`](#isreleased) `static` `inline` | Checks if the specified action's button was released during the current frame. |
| `bool` | [`IsDown`](#isdown) `static` `inline` | Checks if the specified action's button is currently being held down. |
| `bool` | [`IsPressed`](#ispressed-1) `static` `inline` | Checks if the specified key was pressed during the current frame. |
| `bool` | [`IsReleased`](#isreleased-1) `static` `inline` | Checks if the specified key was released during the current frame. |
| `bool` | [`IsDown`](#isdown-1) `static` `inline` | Checks if the specified key is currently being held down. |
| `bool` | [`IsPressed`](#ispressed-2) `static` `inline` | Checks if the specified mouse button was pressed during the current frame. |
| `bool` | [`IsReleased`](#isreleased-2) `static` `inline` | Checks if the specified mouse button was released during the current frame. |
| `bool` | [`IsDown`](#isdown-2) `static` `inline` | Checks if the specified mouse button is currently being held down. |

---

<a id="getaxis"></a>

### GetAxis

`static` `inline`

```java
static inline float GetAxis(string action)
```

Gets the analog value of an input axis.

#### Returns
The analog value of the specified axis.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action or axis to query. |

---

<a id="getanalogvalue"></a>

### GetAnalogValue

`static` `inline`

```java
static inline float GetAnalogValue(string action)
```

Gets the analog value associated with the specified input action.

#### Returns
The analog value associated with the action.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action to query. |

---

<a id="getdigitalvalue"></a>

### GetDigitalValue

`static` `inline`

```java
static inline bool GetDigitalValue(string action)
```

Gets the digital (boolean) value associated with the specified input action.

#### Returns
`true` if the action is active; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action to query. |

---

<a id="ispressed"></a>

### IsPressed

`static` `inline`

```java
static inline bool IsPressed(string action)
```

Checks if the specified action's button was pressed during the current frame.

#### Returns
`true` if the button was pressed during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action to query. |

---

<a id="isreleased"></a>

### IsReleased

`static` `inline`

```java
static inline bool IsReleased(string action)
```

Checks if the specified action's button was released during the current frame.

#### Returns
`true` if the button was released during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action to query. |

---

<a id="isdown"></a>

### IsDown

`static` `inline`

```java
static inline bool IsDown(string action)
```

Checks if the specified action's button is currently being held down.

#### Returns
`true` if the button is currently being held down; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `action` | `string` | The name of the action to query. |

---

<a id="ispressed-1"></a>

### IsPressed

`static` `inline`

```java
static inline bool IsPressed(KeyCode code)
```

Checks if the specified key was pressed during the current frame.

#### Returns
`true` if the key was pressed during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `code` | `[KeyCode](Unravel-Core.md#keycode)` | The key code to query. |

---

<a id="isreleased-1"></a>

### IsReleased

`static` `inline`

```java
static inline bool IsReleased(KeyCode code)
```

Checks if the specified key was released during the current frame.

#### Returns
`true` if the key was released during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `code` | `[KeyCode](Unravel-Core.md#keycode)` | The key code to query. |

---

<a id="isdown-1"></a>

### IsDown

`static` `inline`

```java
static inline bool IsDown(KeyCode code)
```

Checks if the specified key is currently being held down.

#### Returns
`true` if the key is currently being held down; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `code` | `[KeyCode](Unravel-Core.md#keycode)` | The key code to query. |

---

<a id="ispressed-2"></a>

### IsPressed

`static` `inline`

```java
static inline bool IsPressed(MouseButton button)
```

Checks if the specified mouse button was pressed during the current frame.

#### Returns
`true` if the button was pressed during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `button` | `[MouseButton](Unravel-Core.md#mousebutton)` | The mouse button to query. |

---

<a id="isreleased-2"></a>

### IsReleased

`static` `inline`

```java
static inline bool IsReleased(MouseButton button)
```

Checks if the specified mouse button was released during the current frame.

#### Returns
`true` if the button was released during this frame; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `button` | `[MouseButton](Unravel-Core.md#mousebutton)` | The mouse button to query. |

---

<a id="isdown-2"></a>

### IsDown

`static` `inline`

```java
static inline bool IsDown(MouseButton button)
```

Checks if the specified mouse button is currently being held down.

#### Returns
`true` if the button is currently being held down; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `button` | `[MouseButton](Unravel-Core.md#mousebutton)` | The mouse button to query. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`internal_m2n_input_get_analog_value`](#internal_m2n_input_get_analog_value)  |  |
| `bool` | [`internal_m2n_input_get_digital_value`](#internal_m2n_input_get_digital_value)  |  |
| `bool` | [`internal_m2n_input_is_pressed`](#internal_m2n_input_is_pressed)  |  |
| `bool` | [`internal_m2n_input_is_released`](#internal_m2n_input_is_released)  |  |
| `bool` | [`internal_m2n_input_is_down`](#internal_m2n_input_is_down)  |  |
| `bool` | [`internal_m2n_input_is_key_pressed`](#internal_m2n_input_is_key_pressed)  |  |
| `bool` | [`internal_m2n_input_is_key_released`](#internal_m2n_input_is_key_released)  |  |
| `bool` | [`internal_m2n_input_is_key_down`](#internal_m2n_input_is_key_down)  |  |
| `bool` | [`internal_m2n_input_is_mouse_button_pressed`](#internal_m2n_input_is_mouse_button_pressed)  |  |
| `bool` | [`internal_m2n_input_is_mouse_button_released`](#internal_m2n_input_is_mouse_button_released)  |  |
| `bool` | [`internal_m2n_input_is_mouse_button_down`](#internal_m2n_input_is_mouse_button_down)  |  |
| `Vector2` | [`internal_m2n_input_get_mouse_position`](#internal_m2n_input_get_mouse_position)  |  |

---

<a id="internal_m2n_input_get_analog_value"></a>

### internal_m2n_input_get_analog_value

```java
float internal_m2n_input_get_analog_value(string action)
```

---

<a id="internal_m2n_input_get_digital_value"></a>

### internal_m2n_input_get_digital_value

```java
bool internal_m2n_input_get_digital_value(string action)
```

---

<a id="internal_m2n_input_is_pressed"></a>

### internal_m2n_input_is_pressed

```java
bool internal_m2n_input_is_pressed(string action)
```

---

<a id="internal_m2n_input_is_released"></a>

### internal_m2n_input_is_released

```java
bool internal_m2n_input_is_released(string action)
```

---

<a id="internal_m2n_input_is_down"></a>

### internal_m2n_input_is_down

```java
bool internal_m2n_input_is_down(string action)
```

---

<a id="internal_m2n_input_is_key_pressed"></a>

### internal_m2n_input_is_key_pressed

```java
bool internal_m2n_input_is_key_pressed(KeyCode code)
```

---

<a id="internal_m2n_input_is_key_released"></a>

### internal_m2n_input_is_key_released

```java
bool internal_m2n_input_is_key_released(KeyCode code)
```

---

<a id="internal_m2n_input_is_key_down"></a>

### internal_m2n_input_is_key_down

```java
bool internal_m2n_input_is_key_down(KeyCode code)
```

---

<a id="internal_m2n_input_is_mouse_button_pressed"></a>

### internal_m2n_input_is_mouse_button_pressed

```java
bool internal_m2n_input_is_mouse_button_pressed(MouseButton button)
```

---

<a id="internal_m2n_input_is_mouse_button_released"></a>

### internal_m2n_input_is_mouse_button_released

```java
bool internal_m2n_input_is_mouse_button_released(MouseButton button)
```

---

<a id="internal_m2n_input_is_mouse_button_down"></a>

### internal_m2n_input_is_mouse_button_down

```java
bool internal_m2n_input_is_mouse_button_down(MouseButton button)
```

---

<a id="internal_m2n_input_get_mouse_position"></a>

### internal_m2n_input_get_mouse_position

```java
Vector2 internal_m2n_input_get_mouse_position()
```

