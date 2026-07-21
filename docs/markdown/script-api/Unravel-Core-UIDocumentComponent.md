<a id="uidocumentcomponent"></a>

# UIDocumentComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

[Component](Unravel-Core-Component.md#component) that manages an RmlUi document for rendering HTML/CSS-based user interfaces. Each component instance holds its own document while sharing the global UI context.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `VisualTree` | [`asset`](#asset-1)  | Gets or sets the visual tree asset (HTML/RML document) for this UI component. |
| `bool` | [`loaded`](#loaded)  | Gets a value indicating whether the UI document is currently loaded and ready for use. |
| `bool` | [`enabled`](#enabled-2)  | Gets a value indicating whether the UI document is currently enabled. |
| `string` | [`title`](#title-1)  | Gets or sets the title of the UI document. |

---

<a id="asset-1"></a>

### asset

```java
VisualTree asset
```

Gets or sets the visual tree asset (HTML/RML document) for this UI component.

The visual tree asset that defines the UI document structure and content.

---

<a id="loaded"></a>

### loaded

```java
bool loaded
```

Gets a value indicating whether the UI document is currently loaded and ready for use.

True if the document is loaded; otherwise, false.

---

<a id="enabled-2"></a>

### enabled

```java
bool enabled
```

Gets a value indicating whether the UI document is currently enabled.

True if the document is enabled; otherwise, false.

---

<a id="title-1"></a>

### title

```java
string title
```

Gets or sets the title of the UI document.

The document title as a string.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Close`](#close-1) `inline` | Closes the UI document, unloading it from memory. The document will need to be reloaded before it can be used again. |
| `UIDocument` | [`GetDocument`](#getdocument) `inline` | Gets a wrapper object for this UI document that can be cached and used for direct access. The wrapper will be automatically invalidated when the document is destroyed. |

---

<a id="close-1"></a>

### Close

`inline`

```java
inline void Close()
```

Closes the UI document, unloading it from memory. The document will need to be reloaded before it can be used again.

---

<a id="getdocument"></a>

### GetDocument

`inline`

```java
inline UIDocument GetDocument()
```

Gets a wrapper object for this UI document that can be cached and used for direct access. The wrapper will be automatically invalidated when the document is destroyed.

#### Returns
A [UIDocument](Unravel-Core-UIDocument.md#uidocument) if the document is loaded; otherwise, null.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `VisualTree` | [`asset_`](#asset_)  |  |

---

<a id="asset_"></a>

### asset_

```java
VisualTree asset_
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `Guid` | [`internal_m2n_ui_document_get_asset`](#internal_m2n_ui_document_get_asset)  |  |
| `void` | [`internal_m2n_ui_document_set_asset`](#internal_m2n_ui_document_set_asset)  |  |
| `bool` | [`internal_m2n_ui_document_is_loaded`](#internal_m2n_ui_document_is_loaded)  |  |
| `bool` | [`internal_m2n_ui_document_is_enabled`](#internal_m2n_ui_document_is_enabled)  |  |
| `void` | [`internal_m2n_ui_document_set_enabled`](#internal_m2n_ui_document_set_enabled)  |  |
| `void` | [`internal_m2n_ui_document_close`](#internal_m2n_ui_document_close)  |  |
| `string` | [`internal_m2n_ui_document_get_title`](#internal_m2n_ui_document_get_title)  |  |
| `void` | [`internal_m2n_ui_document_set_title`](#internal_m2n_ui_document_set_title)  |  |
| `IntPtr` | [`internal_m2n_ui_document_get_wrapper`](#internal_m2n_ui_document_get_wrapper)  |  |

---

<a id="internal_m2n_ui_document_get_asset"></a>

### internal_m2n_ui_document_get_asset

```java
Guid internal_m2n_ui_document_get_asset(Entity eid)
```

---

<a id="internal_m2n_ui_document_set_asset"></a>

### internal_m2n_ui_document_set_asset

```java
void internal_m2n_ui_document_set_asset(Entity eid, Guid uid)
```

---

<a id="internal_m2n_ui_document_is_loaded"></a>

### internal_m2n_ui_document_is_loaded

```java
bool internal_m2n_ui_document_is_loaded(Entity eid)
```

---

<a id="internal_m2n_ui_document_is_enabled"></a>

### internal_m2n_ui_document_is_enabled

```java
bool internal_m2n_ui_document_is_enabled(Entity eid)
```

---

<a id="internal_m2n_ui_document_set_enabled"></a>

### internal_m2n_ui_document_set_enabled

```java
void internal_m2n_ui_document_set_enabled(Entity eid, bool enabled)
```

---

<a id="internal_m2n_ui_document_close"></a>

### internal_m2n_ui_document_close

```java
void internal_m2n_ui_document_close(Entity eid)
```

---

<a id="internal_m2n_ui_document_get_title"></a>

### internal_m2n_ui_document_get_title

```java
string internal_m2n_ui_document_get_title(Entity eid)
```

---

<a id="internal_m2n_ui_document_set_title"></a>

### internal_m2n_ui_document_set_title

```java
void internal_m2n_ui_document_set_title(Entity eid, string title)
```

---

<a id="internal_m2n_ui_document_get_wrapper"></a>

### internal_m2n_ui_document_get_wrapper

```java
IntPtr internal_m2n_ui_document_get_wrapper(Entity eid)
```

