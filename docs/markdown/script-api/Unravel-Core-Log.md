<a id="log-2"></a>

# Log

Writes messages to the engine log with automatic call-site information.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Trace`](#trace) `static` `inline` | Writes a trace-level log message. |
| `void` | [`Info`](#info) `static` `inline` | Writes an info-level log message. |
| `void` | [`Warning`](#warning) `static` `inline` | Writes a warning-level log message. |
| `void` | [`Error`](#error) `static` `inline` | Writes an error-level log message. |

---

<a id="trace"></a>

### Trace

`static` `inline`

```java
static inline void Trace(string message, string func = "", string file = "", int line = 0)
```

Writes a trace-level log message.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `string` | The message to log. |
| `func` | `string` | Caller member name (filled automatically). |
| `file` | `string` | Caller file path (filled automatically). |
| `line` | `int` | Caller line number (filled automatically). |

---

<a id="info"></a>

### Info

`static` `inline`

```java
static inline void Info(string message, string func = "", string file = "", int line = 0)
```

Writes an info-level log message.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `string` | The message to log. |
| `func` | `string` | Caller member name (filled automatically). |
| `file` | `string` | Caller file path (filled automatically). |
| `line` | `int` | Caller line number (filled automatically). |

---

<a id="warning"></a>

### Warning

`static` `inline`

```java
static inline void Warning(string message, string func = "", string file = "", int line = 0)
```

Writes a warning-level log message.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `string` | The message to log. |
| `func` | `string` | Caller member name (filled automatically). |
| `file` | `string` | Caller file path (filled automatically). |
| `line` | `int` | Caller line number (filled automatically). |

---

<a id="error"></a>

### Error

`static` `inline`

```java
static inline void Error(string message, string func = "", string file = "", int line = 0)
```

Writes an error-level log message.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `message` | `string` | The message to log. |
| `func` | `string` | Caller member name (filled automatically). |
| `file` | `string` | Caller file path (filled automatically). |
| `line` | `int` | Caller line number (filled automatically). |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_log_trace`](#internal_m2n_log_trace)  |  |
| `void` | [`internal_m2n_log_info`](#internal_m2n_log_info)  |  |
| `void` | [`internal_m2n_log_warning`](#internal_m2n_log_warning)  |  |
| `void` | [`internal_m2n_log_error`](#internal_m2n_log_error)  |  |

---

<a id="internal_m2n_log_trace"></a>

### internal_m2n_log_trace

```java
void internal_m2n_log_trace(string message, string func, string file, int line)
```

---

<a id="internal_m2n_log_info"></a>

### internal_m2n_log_info

```java
void internal_m2n_log_info(string message, string func, string file, int line)
```

---

<a id="internal_m2n_log_warning"></a>

### internal_m2n_log_warning

```java
void internal_m2n_log_warning(string message, string func, string file, int line)
```

---

<a id="internal_m2n_log_error"></a>

### internal_m2n_log_error

```java
void internal_m2n_log_error(string message, string func, string file, int line)
```

