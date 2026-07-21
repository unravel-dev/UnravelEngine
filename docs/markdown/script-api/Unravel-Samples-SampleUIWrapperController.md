<a id="sampleuiwrappercontroller"></a>

# SampleUIWrapperController

> **Extends:** [`Unravel.Core.ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent)

Sample script demonstrating the use of UI wrapper objects for caching and direct manipulation. This approach allows you to get UI elements once and keep them for later use without repeated searches. Also demonstrates the new typed UI event system with UIPointerEvent and UIKeyEvent.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override void` | [`OnStart`](#onstart-2) `virtual` `inline` | Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled. |
| `override void` | [`OnUpdate`](#onupdate-2) `virtual` `inline` | Called on every frame update. Override this method to implement frame-based logic. |
| `override void` | [`OnDestroy`](#ondestroy-2) `virtual` `inline` | Called when the script is destroyed. Override this method to clean up resources or data. |
| `void` | [`CreateDynamicElement`](#createdynamicelement) `inline` |  |
| `void` | [`ValidateAndRecacheElements`](#validateandrecacheelements) `inline` |  |

---

<a id="onstart-2"></a>

### OnStart

`virtual` `inline`

```java
virtual inline override void OnStart()
```

Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled.

---

<a id="onupdate-2"></a>

### OnUpdate

`virtual` `inline`

```java
virtual inline override void OnUpdate()
```

Called on every frame update. Override this method to implement frame-based logic.

---

<a id="ondestroy-2"></a>

### OnDestroy

`virtual` `inline`

```java
virtual inline override void OnDestroy()
```

Called when the script is destroyed. Override this method to clean up resources or data.

---

<a id="createdynamicelement"></a>

### CreateDynamicElement

`inline`

```java
inline void CreateDynamicElement()
```

---

<a id="validateandrecacheelements"></a>

### ValidateAndRecacheElements

`inline`

```java
inline void ValidateAndRecacheElements()
```

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `UIDocument` | [`document`](#document)  |  |
| `UIElement` | [`titleElement`](#titleelement)  |  |
| `UIElement` | [`buttonElement`](#buttonelement)  |  |
| `UIElement` | [`textInputElement`](#textinputelement)  |  |
| `int` | [`clickCount`](#clickcount)  |  |

---

<a id="document"></a>

### document

```java
UIDocument document
```

---

<a id="titleelement"></a>

### titleElement

```java
UIElement titleElement
```

---

<a id="buttonelement"></a>

### buttonElement

```java
UIElement buttonElement
```

---

<a id="textinputelement"></a>

### textInputElement

```java
UIElement textInputElement
```

---

<a id="clickcount"></a>

### clickCount

```java
int clickCount = 0
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`CacheUIElements`](#cacheuielements) `inline` |  |
| `void` | [`SetupInitialUI`](#setupinitialui) `inline` |  |
| `void` | [`RegisterEventHandlers`](#registereventhandlers) `inline` |  |
| `void` | [`OnButtonClick`](#onbuttonclick) `inline` |  |
| `void` | [`OnTextInputChange`](#ontextinputchange) `inline` |  |
| `void` | [`OnTextInputKeyDown`](#ontextinputkeydown) `inline` |  |

---

<a id="cacheuielements"></a>

### CacheUIElements

`inline`

```java
inline void CacheUIElements()
```

---

<a id="setupinitialui"></a>

### SetupInitialUI

`inline`

```java
inline void SetupInitialUI()
```

---

<a id="registereventhandlers"></a>

### RegisterEventHandlers

`inline`

```java
inline void RegisterEventHandlers()
```

---

<a id="onbuttonclick"></a>

### OnButtonClick

`inline`

```java
inline void OnButtonClick(UIPointerEvent ev)
```

---

<a id="ontextinputchange"></a>

### OnTextInputChange

`inline`

```java
inline void OnTextInputChange(UIEventBase ev)
```

---

<a id="ontextinputkeydown"></a>

### OnTextInputKeyDown

`inline`

```java
inline void OnTextInputKeyDown(UIKeyEvent ev)
```

