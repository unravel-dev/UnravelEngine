<a id="uidocument"></a>

# UIDocument

> **Extends:** [`Unravel.Core.NativeObject`](Unravel-Core-NativeObject.md#nativeobject)

Represents a wrapper around a native RmlUi document with managed lifetime. The C++ side owns the lifetime and will invalidate this wrapper when the document is destroyed.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`Owner`](#owner-1)  | Gets the entity that owns this UI document. |
| `string` | [`Title`](#title)  | Gets or sets the title of the document. |
| `bool` | [`IsVisible`](#isvisible)  | Gets whether the document is currently visible. |

---

<a id="owner-1"></a>

### Owner

```java
Entity Owner
```

Gets the entity that owns this UI document.

---

<a id="title"></a>

### Title

```java
string Title
```

Gets or sets the title of the document.

---

<a id="isvisible"></a>

### IsVisible

```java
bool IsVisible
```

Gets whether the document is currently visible.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override bool` | [`IsValid`](#isvalid-4) `inline` | Gets whether this wrapper still points to a valid native document. |
| `void` | [`Show`](#show) `inline` | Shows the document. |
| `void` | [`Hide`](#hide) `inline` | Hides the document. |
| `void` | [`Close`](#close) `inline` | Closes the document and removes it from the context. After calling this, the wrapper will become invalid. |
| `UIElement` | [`GetElementById`](#getelementbyid) `inline` | Gets an element wrapper by its ID. |
| `UIElement` | [`QuerySelector`](#queryselector) `inline` | Gets the first element that matches the specified CSS selector. |
| `override string` | [`ToString`](#tostring-30) `inline` | Returns a string representation of this UI document wrapper. |

---

<a id="isvalid-4"></a>

### IsValid

`inline`

```java
inline override bool IsValid()
```

Gets whether this wrapper still points to a valid native document.

---

<a id="show"></a>

### Show

`inline`

```java
inline void Show()
```

Shows the document.

---

<a id="hide"></a>

### Hide

`inline`

```java
inline void Hide()
```

Hides the document.

---

<a id="close"></a>

### Close

`inline`

```java
inline void Close()
```

Closes the document and removes it from the context. After calling this, the wrapper will become invalid.

---

<a id="getelementbyid"></a>

### GetElementById

`inline`

```java
inline UIElement GetElementById(string elementId)
```

Gets an element wrapper by its ID.

#### Returns
A [UIElement](Unravel-Core-UIElement.md#uielement) if found; otherwise, null.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `elementId` | `string` | The ID of the element to find. |

---

<a id="queryselector"></a>

### QuerySelector

`inline`

```java
inline UIElement QuerySelector(string selector)
```

Gets the first element that matches the specified CSS selector.

#### Returns
A [UIElement](Unravel-Core-UIElement.md#uielement) if found; otherwise, null.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `selector` | `string` | The CSS selector to match against. |

---

<a id="tostring-30"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a string representation of this UI document wrapper.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `IntPtr` | [`nativePtr`](#nativeptr)  |  |
| `readonly Entity` | [`ownerEntity`](#ownerentity)  |  |

---

<a id="nativeptr"></a>

### nativePtr

```java
IntPtr nativePtr = IntPtr.Zero
```

---

<a id="ownerentity"></a>

### ownerEntity

```java
readonly Entity ownerEntity
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_ui_document_wrapper_is_valid`](#internal_m2n_ui_document_wrapper_is_valid)  |  |
| `string` | [`internal_m2n_ui_document_wrapper_get_title`](#internal_m2n_ui_document_wrapper_get_title)  |  |
| `void` | [`internal_m2n_ui_document_wrapper_set_title`](#internal_m2n_ui_document_wrapper_set_title)  |  |
| `bool` | [`internal_m2n_ui_document_wrapper_is_visible`](#internal_m2n_ui_document_wrapper_is_visible)  |  |
| `void` | [`internal_m2n_ui_document_wrapper_show`](#internal_m2n_ui_document_wrapper_show)  |  |
| `void` | [`internal_m2n_ui_document_wrapper_hide`](#internal_m2n_ui_document_wrapper_hide)  |  |
| `void` | [`internal_m2n_ui_document_wrapper_close`](#internal_m2n_ui_document_wrapper_close)  |  |
| `IntPtr` | [`internal_m2n_ui_document_wrapper_get_element_by_id`](#internal_m2n_ui_document_wrapper_get_element_by_id)  |  |
| `IntPtr` | [`internal_m2n_ui_document_wrapper_query_selector`](#internal_m2n_ui_document_wrapper_query_selector)  |  |

---

<a id="internal_m2n_ui_document_wrapper_is_valid"></a>

### internal_m2n_ui_document_wrapper_is_valid

```java
bool internal_m2n_ui_document_wrapper_is_valid(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_get_title"></a>

### internal_m2n_ui_document_wrapper_get_title

```java
string internal_m2n_ui_document_wrapper_get_title(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_set_title"></a>

### internal_m2n_ui_document_wrapper_set_title

```java
void internal_m2n_ui_document_wrapper_set_title(IntPtr documentPtr, Entity ownerEntity, string title)
```

---

<a id="internal_m2n_ui_document_wrapper_is_visible"></a>

### internal_m2n_ui_document_wrapper_is_visible

```java
bool internal_m2n_ui_document_wrapper_is_visible(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_show"></a>

### internal_m2n_ui_document_wrapper_show

```java
void internal_m2n_ui_document_wrapper_show(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_hide"></a>

### internal_m2n_ui_document_wrapper_hide

```java
void internal_m2n_ui_document_wrapper_hide(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_close"></a>

### internal_m2n_ui_document_wrapper_close

```java
void internal_m2n_ui_document_wrapper_close(IntPtr documentPtr, Entity ownerEntity)
```

---

<a id="internal_m2n_ui_document_wrapper_get_element_by_id"></a>

### internal_m2n_ui_document_wrapper_get_element_by_id

```java
IntPtr internal_m2n_ui_document_wrapper_get_element_by_id(IntPtr documentPtr, Entity ownerEntity, string elementId)
```

---

<a id="internal_m2n_ui_document_wrapper_query_selector"></a>

### internal_m2n_ui_document_wrapper_query_selector

```java
IntPtr internal_m2n_ui_document_wrapper_query_selector(IntPtr documentPtr, Entity ownerEntity, string selector)
```

