<a id="uielement"></a>

# UIElement

> **Extends:** [`Unravel.Core.NativeObject`](Unravel-Core-NativeObject.md#nativeobject)

Represents a wrapper around a native RmlUi element with managed lifetime. The C++ side owns the lifetime and will invalidate this wrapper when the element is destroyed.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`Owner`](#owner-2)  | Gets the entity that owns this UI element. |
| `string` | [`ElementId`](#elementid)  | Gets the element ID. |
| `string` | [`InnerRml`](#innerrml)  | Gets or sets the inner RML content of the element. |
| `bool` | [`IsVisible`](#isvisible-1)  | Gets or sets whether the element is visible. |

---

<a id="owner-2"></a>

### Owner

```java
Entity Owner
```

Gets the entity that owns this UI element.

---

<a id="elementid"></a>

### ElementId

```java
string ElementId
```

Gets the element ID.

---

<a id="innerrml"></a>

### InnerRml

```java
string InnerRml
```

Gets or sets the inner RML content of the element.

---

<a id="isvisible-1"></a>

### IsVisible

```java
bool IsVisible
```

Gets or sets whether the element is visible.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override bool` | [`IsValid`](#isvalid-5) `inline` | Gets whether this wrapper still points to a valid native element. |
| `string` | [`GetAttribute`](#getattribute) `inline` | Gets the value of an attribute. |
| `void` | [`SetAttribute`](#setattribute) `inline` | Sets the value of an attribute. |
| `void` | [`RemoveAttribute`](#removeattribute) `inline` | Removes an attribute from the element. |
| `bool` | [`HasAttribute`](#hasattribute) `inline` | Checks if the element has a specific attribute. |
| `void` | [`SetClass`](#setclass) `inline` | Sets or removes a CSS class on the element. |
| `bool` | [`IsClassSet`](#isclassset) `inline` | Checks if the element has a specific CSS class. |
| `void` | [`SyncTransformToEntity`](#synctransformtoentity) `inline` | Synchronizes this element's transform to follow the given entity. |
| `void` | [`Focus`](#focus) `inline` | Gives focus to this element. |
| `void` | [`Blur`](#blur) `inline` | Removes focus from this element. |
| `void` | [`Click`](#click) `inline` | Simulates a click on this element. |
| `void` | [`ScrollIntoView`](#scrollintoview) `inline` | Scrolls the element into view. |
| `void` | [`RegisterCallback`](#registercallback) `inline` | Adds an event listener to this element. |
| `bool` | [`UnregisterCallback`](#unregistercallback) `inline` | Removes an event listener from this element. |
| `void` | [`RegisterCallback< T >`](#registercallbackt) `inline` | Registers a typed event callback with compile-time type safety and zero runtime casting. |
| `bool` | [`UnregisterCallback< T >`](#unregistercallbackt) `inline` | Unregisters a typed event callback. |
| `bool` | [`UnsubscribeAll`](#unsubscribeall) `inline` | Unsubscribe all callbacks from this [UIElement](#uielement). This removes all event listeners (both legacy and typed) from this element. |
| `override string` | [`ToString`](#tostring-31) `inline` | Returns a string representation of this UI element wrapper. |

---

<a id="isvalid-5"></a>

### IsValid

`inline`

```java
inline override bool IsValid()
```

Gets whether this wrapper still points to a valid native element.

---

<a id="getattribute"></a>

### GetAttribute

`inline`

```java
inline string GetAttribute(string attributeName)
```

Gets the value of an attribute.

#### Returns
The attribute value, or empty string if not found.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `attributeName` | `string` | The name of the attribute. |

---

<a id="setattribute"></a>

### SetAttribute

`inline`

```java
inline void SetAttribute(string attributeName, string value)
```

Sets the value of an attribute.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `attributeName` | `string` | The name of the attribute. |
| `value` | `string` | The value to set. |

---

<a id="removeattribute"></a>

### RemoveAttribute

`inline`

```java
inline void RemoveAttribute(string attributeName)
```

Removes an attribute from the element.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `attributeName` | `string` | The name of the attribute to remove. |

---

<a id="hasattribute"></a>

### HasAttribute

`inline`

```java
inline bool HasAttribute(string attributeName)
```

Checks if the element has a specific attribute.

#### Returns
True if the attribute exists; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `attributeName` | `string` | The name of the attribute to check. |

---

<a id="setclass"></a>

### SetClass

`inline`

```java
inline void SetClass(string className, bool activate)
```

Sets or removes a CSS class on the element.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `className` | `string` | The name of the CSS class. |
| `activate` | `bool` | True to add the class, false to remove it. |

---

<a id="isclassset"></a>

### IsClassSet

`inline`

```java
inline bool IsClassSet(string className)
```

Checks if the element has a specific CSS class.

#### Returns
True if the class is set; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `className` | `string` | The name of the CSS class to check. |

---

<a id="synctransformtoentity"></a>

### SyncTransformToEntity

`inline`

```java
inline void SyncTransformToEntity(Entity transformEntity)
```

Synchronizes this element's transform to follow the given entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `transformEntity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity whose transform to sync from. |

---

<a id="focus"></a>

### Focus

`inline`

```java
inline void Focus()
```

Gives focus to this element.

---

<a id="blur"></a>

### Blur

`inline`

```java
inline void Blur()
```

Removes focus from this element.

---

<a id="click"></a>

### Click

`inline`

```java
inline void Click()
```

Simulates a click on this element.

---

<a id="scrollintoview"></a>

### ScrollIntoView

`inline`

```java
inline void ScrollIntoView(bool alignWithTop = true)
```

Scrolls the element into view.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `alignWithTop` | `bool` | If true, align with the top of the viewport; otherwise, align with the bottom. |

---

<a id="registercallback"></a>

### RegisterCallback

`inline`

```java
inline void RegisterCallback(string eventType, UIEventCallback callback)
```

Adds an event listener to this element.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `eventType` | `string` | The type of event to listen for. |
| `callback` | `[UIEventCallback](Unravel-Core.md#uieventcallback)` | The callback to invoke when the event occurs. |

---

<a id="unregistercallback"></a>

### UnregisterCallback

`inline`

```java
inline bool UnregisterCallback(string eventType, UIEventCallback callback)
```

Removes an event listener from this element.

#### Returns
True if the listener was removed successfully.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `eventType` | `string` | The type of event. |
| `callback` | `[UIEventCallback](Unravel-Core.md#uieventcallback)` | The callback to remove. |

---

<a id="registercallbackt"></a>

### RegisterCallback< T >

`inline`

```java
inline void RegisterCallback< T >(string eventType, Action< T > callback)
```

Registers a typed event callback with compile-time type safety and zero runtime casting.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `eventType` | `string` | The type of event to listen for. |
| `callback` | `Action< T >` | The callback to invoke when the event occurs. |

---

<a id="unregistercallbackt"></a>

### UnregisterCallback< T >

`inline`

```java
inline bool UnregisterCallback< T >(string eventType, Action< T > callback)
```

Unregisters a typed event callback.

#### Returns
True if the callback was found and removed.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `eventType` | `string` | The type of event. |
| `callback` | `Action< T >` | The callback to remove. |

---

<a id="unsubscribeall"></a>

### UnsubscribeAll

`inline`

```java
inline bool UnsubscribeAll()
```

Unsubscribe all callbacks from this [UIElement](#uielement). This removes all event listeners (both legacy and typed) from this element.

#### Returns
True if any subscriptions were removed

---

<a id="tostring-31"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a string representation of this UI element wrapper.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `IntPtr` | [`nativePtr`](#nativeptr-1)  |  |
| `readonly Entity` | [`ownerEntity`](#ownerentity-1)  |  |

---

<a id="nativeptr-1"></a>

### nativePtr

```java
IntPtr nativePtr = IntPtr.Zero
```

---

<a id="ownerentity-1"></a>

### ownerEntity

```java
readonly Entity ownerEntity
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_ui_element_wrapper_is_valid`](#internal_m2n_ui_element_wrapper_is_valid)  |  |
| `string` | [`internal_m2n_ui_element_wrapper_get_inner_rml`](#internal_m2n_ui_element_wrapper_get_inner_rml)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_set_inner_rml`](#internal_m2n_ui_element_wrapper_set_inner_rml)  |  |
| `bool` | [`internal_m2n_ui_element_wrapper_is_visible`](#internal_m2n_ui_element_wrapper_is_visible)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_set_visible`](#internal_m2n_ui_element_wrapper_set_visible)  |  |
| `string` | [`internal_m2n_ui_element_wrapper_get_attribute`](#internal_m2n_ui_element_wrapper_get_attribute)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_set_attribute`](#internal_m2n_ui_element_wrapper_set_attribute)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_remove_attribute`](#internal_m2n_ui_element_wrapper_remove_attribute)  |  |
| `bool` | [`internal_m2n_ui_element_wrapper_has_attribute`](#internal_m2n_ui_element_wrapper_has_attribute)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_set_class`](#internal_m2n_ui_element_wrapper_set_class)  |  |
| `bool` | [`internal_m2n_ui_element_wrapper_is_class_set`](#internal_m2n_ui_element_wrapper_is_class_set)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_sync_transform_to_entity`](#internal_m2n_ui_element_wrapper_sync_transform_to_entity)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_focus`](#internal_m2n_ui_element_wrapper_focus)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_blur`](#internal_m2n_ui_element_wrapper_blur)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_click`](#internal_m2n_ui_element_wrapper_click)  |  |
| `void` | [`internal_m2n_ui_element_wrapper_scroll_into_view`](#internal_m2n_ui_element_wrapper_scroll_into_view)  |  |
| `string` | [`internal_m2n_ui_element_wrapper_get_id`](#internal_m2n_ui_element_wrapper_get_id)  |  |

---

<a id="internal_m2n_ui_element_wrapper_is_valid"></a>

### internal_m2n_ui_element_wrapper_is_valid

```java
bool internal_m2n_ui_element_wrapper_is_valid(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_get_inner_rml"></a>

### internal_m2n_ui_element_wrapper_get_inner_rml

```java
string internal_m2n_ui_element_wrapper_get_inner_rml(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_set_inner_rml"></a>

### internal_m2n_ui_element_wrapper_set_inner_rml

```java
void internal_m2n_ui_element_wrapper_set_inner_rml(IntPtr elementPtr, Entity ownerEntity, string rml)
```

---

<a id="internal_m2n_ui_element_wrapper_is_visible"></a>

### internal_m2n_ui_element_wrapper_is_visible

```java
bool internal_m2n_ui_element_wrapper_is_visible(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_set_visible"></a>

### internal_m2n_ui_element_wrapper_set_visible

```java
void internal_m2n_ui_element_wrapper_set_visible(IntPtr elementPtr, Entity ownerEntity, bool visible)
```

---

<a id="internal_m2n_ui_element_wrapper_get_attribute"></a>

### internal_m2n_ui_element_wrapper_get_attribute

```java
string internal_m2n_ui_element_wrapper_get_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName)
```

---

<a id="internal_m2n_ui_element_wrapper_set_attribute"></a>

### internal_m2n_ui_element_wrapper_set_attribute

```java
void internal_m2n_ui_element_wrapper_set_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName, string value)
```

---

<a id="internal_m2n_ui_element_wrapper_remove_attribute"></a>

### internal_m2n_ui_element_wrapper_remove_attribute

```java
void internal_m2n_ui_element_wrapper_remove_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName)
```

---

<a id="internal_m2n_ui_element_wrapper_has_attribute"></a>

### internal_m2n_ui_element_wrapper_has_attribute

```java
bool internal_m2n_ui_element_wrapper_has_attribute(IntPtr elementPtr, Entity ownerEntity, string attributeName)
```

---

<a id="internal_m2n_ui_element_wrapper_set_class"></a>

### internal_m2n_ui_element_wrapper_set_class

```java
void internal_m2n_ui_element_wrapper_set_class(IntPtr elementPtr, Entity ownerEntity, string className, bool activate)
```

---

<a id="internal_m2n_ui_element_wrapper_is_class_set"></a>

### internal_m2n_ui_element_wrapper_is_class_set

```java
bool internal_m2n_ui_element_wrapper_is_class_set(IntPtr elementPtr, Entity ownerEntity, string className)
```

---

<a id="internal_m2n_ui_element_wrapper_sync_transform_to_entity"></a>

### internal_m2n_ui_element_wrapper_sync_transform_to_entity

```java
void internal_m2n_ui_element_wrapper_sync_transform_to_entity(IntPtr elementPtr, Entity ownerEntity, Entity transformEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_focus"></a>

### internal_m2n_ui_element_wrapper_focus

```java
void internal_m2n_ui_element_wrapper_focus(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_blur"></a>

### internal_m2n_ui_element_wrapper_blur

```java
void internal_m2n_ui_element_wrapper_blur(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_click"></a>

### internal_m2n_ui_element_wrapper_click

```java
void internal_m2n_ui_element_wrapper_click(IntPtr elementPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_element_wrapper_scroll_into_view"></a>

### internal_m2n_ui_element_wrapper_scroll_into_view

```java
void internal_m2n_ui_element_wrapper_scroll_into_view(IntPtr elementPtr, Entity ownerEntity, bool alignWithTop)
```

---

<a id="internal_m2n_ui_element_wrapper_get_id"></a>

### internal_m2n_ui_element_wrapper_get_id

```java
string internal_m2n_ui_element_wrapper_get_id(IntPtr elementPtr, Entity ownerEntity)
```

