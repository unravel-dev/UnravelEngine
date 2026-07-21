<a id="gcmonitor"></a>

# GCMonitor

Tracks managed GC activity and optionally logs collection/memory deltas.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`GCMonitor`](#gcmonitor-1) `inline` | Creates a monitor and captures the current GC baseline. |
| `void` | [`Reset`](#reset-1) `inline` | Resets the baseline collection counts and managed memory size. |
| `void` | [`CheckAndLog`](#checkandlog) `inline` | Compares current GC stats to the baseline and logs notable changes. |

---

<a id="gcmonitor-1"></a>

### GCMonitor

`inline`

```java
inline GCMonitor()
```

Creates a monitor and captures the current GC baseline.

---

<a id="reset-1"></a>

### Reset

`inline`

```java
inline void Reset()
```

Resets the baseline collection counts and managed memory size.

---

<a id="checkandlog"></a>

### CheckAndLog

`inline`

```java
inline void CheckAndLog(string context = "")
```

Compares current GC stats to the baseline and logs notable changes.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `string` | Optional label included in the log message. |

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `int` | [`lastGen0Count`](#lastgen0count)  |  |
| `int` | [`lastGen1Count`](#lastgen1count)  |  |
| `int` | [`lastGen2Count`](#lastgen2count)  |  |
| `long` | [`lastMemory`](#lastmemory)  |  |

---

<a id="lastgen0count"></a>

### lastGen0Count

```java
int lastGen0Count
```

---

<a id="lastgen1count"></a>

### lastGen1Count

```java
int lastGen1Count
```

---

<a id="lastgen2count"></a>

### lastGen2Count

```java
int lastGen2Count
```

---

<a id="lastmemory"></a>

### lastMemory

```java
long lastMemory
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `long` | [`internal_m2n_get_dotnet_heap_size`](#internal_m2n_get_dotnet_heap_size)  |  |
| `long` | [`internal_m2n_get_dotnet_used_size`](#internal_m2n_get_dotnet_used_size)  |  |

---

<a id="internal_m2n_get_dotnet_heap_size"></a>

### internal_m2n_get_dotnet_heap_size

```java
long internal_m2n_get_dotnet_heap_size()
```

---

<a id="internal_m2n_get_dotnet_used_size"></a>

### internal_m2n_get_dotnet_used_size

```java
long internal_m2n_get_dotnet_used_size()
```

