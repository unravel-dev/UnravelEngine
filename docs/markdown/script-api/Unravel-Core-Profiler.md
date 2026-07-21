<a id="profiler"></a>

# Profiler

Performance profiler for measuring and recording execution times.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`AddRecord`](#addrecord) `static` `inline` | Add a performance record with a custom name and time measurement. |
| `ProfilerScope` | [`Scope`](#scope) `static` `inline` | Create a scoped profiler that automatically measures and records time when disposed. Use with 'using' statement for automatic scope-based profiling. |

---

<a id="addrecord"></a>

### AddRecord

`static` `inline`

```java
static inline void AddRecord(string name, float timeMs)
```

Add a performance record with a custom name and time measurement.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | Name of the performance record |
| `timeMs` | `float` | [Time](Unravel-Core-Time.md#time-1) in milliseconds |

---

<a id="scope"></a>

### Scope

`static` `inline`

```java
static inline ProfilerScope Scope(string name)
```

Create a scoped profiler that automatically measures and records time when disposed. Use with 'using' statement for automatic scope-based profiling.

#### Returns
[ProfilerScope](Unravel-Core-ProfilerScope.md#profilerscope) that measures time until disposed

using ([Profiler.Scope](#scope)("MyFunction")) { // Code to profile }

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | Name of the performance record |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_profiler_add_record`](#internal_m2n_profiler_add_record)  | Add a performance record with a custom name and time measurement. |

---

<a id="internal_m2n_profiler_add_record"></a>

### internal_m2n_profiler_add_record

```java
void internal_m2n_profiler_add_record(string name, float timeMs)
```

Add a performance record with a custom name and time measurement.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | Name of the performance record |
| `timeMs` | `float` | [Time](Unravel-Core-Time.md#time-1) in milliseconds |

