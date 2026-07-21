<a id="profilerscope"></a>

# ProfilerScope

> **Extends:** `IDisposable`

Scoped profiler that measures execution time and automatically records it when disposed. Use with 'using' statement for automatic scope-based profiling.

using (var scope = new [ProfilerScope](#profilerscope)("MyFunction")) { // Code to profile } // [Time](Unravel-Core-Time.md#time-1) is automatically recorded here

// Or use the static helper: using ([Profiler.Scope](Unravel-Core-Profiler.md#scope)("MyFunction")) { // Code to profile }

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`ProfilerScope`](#profilerscope-1) `inline` | Creates a new [ProfilerScope](#profilerscope) and starts timing. |
| `void` | [`Dispose`](#dispose) `inline` | Stops timing and records the elapsed time to the profiler. |

---

<a id="profilerscope-1"></a>

### ProfilerScope

`inline`

```java
inline ProfilerScope(string name)
```

Creates a new [ProfilerScope](#profilerscope) and starts timing.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | Name of the performance record |

---

<a id="dispose"></a>

### Dispose

`inline`

```java
inline void Dispose()
```

Stops timing and records the elapsed time to the profiler.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly string` | [`_name`](#_name)  |  |
| `readonly Stopwatch` | [`_stopwatch`](#_stopwatch)  |  |
| `bool` | [`_disposed`](#_disposed)  |  |

---

<a id="_name"></a>

### _name

```java
readonly string _name
```

---

<a id="_stopwatch"></a>

### _stopwatch

```java
readonly Stopwatch _stopwatch
```

---

<a id="_disposed"></a>

### _disposed

```java
bool _disposed
```

