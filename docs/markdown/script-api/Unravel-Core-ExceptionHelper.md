<a id="exceptionhelper"></a>

# ExceptionHelper

Helpers for throwing managed exceptions from native bridge code.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`ThrowException`](#throwexception) `static` `inline` | Throws an InvalidOperationException with the given message. |

---

<a id="throwexception"></a>

### ThrowException

`static` `inline`

```java
static inline void ThrowException(string message)
```

Throws an InvalidOperationException with the given message.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `string` | Exception message. |

