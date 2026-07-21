<a id="systemmanager"></a>

# SystemManager

Entry point for native-to-managed frame updates and script component dispatch.

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `ScriptComponentManager` | [`ScriptManager`](#scriptmanager) `static` | Global manager that invokes script component update callbacks. |

---

<a id="scriptmanager"></a>

### ScriptManager

`static`

```java
ScriptComponentManager ScriptManager = new ()
```

Global manager that invokes script component update callbacks.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_n2m_update`](#internal_n2m_update) `static` `inline` |  |
| `void` | [`internal_n2m_fixed_update`](#internal_n2m_fixed_update) `static` `inline` |  |
| `void` | [`internal_n2m_late_update`](#internal_n2m_late_update) `static` `inline` |  |

---

<a id="internal_n2m_update"></a>

### internal_n2m_update

`static` `inline`

```java
static inline void internal_n2m_update(UpdateInfo info)
```

---

<a id="internal_n2m_fixed_update"></a>

### internal_n2m_fixed_update

`static` `inline`

```java
static inline void internal_n2m_fixed_update(FixedUpdateInfo info)
```

---

<a id="internal_n2m_late_update"></a>

### internal_n2m_late_update

`static` `inline`

```java
static inline void internal_n2m_late_update()
```

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `GCMonitor` | [`gcMonitor`](#gcmonitor-2) `static` |  |

---

<a id="gcmonitor-2"></a>

### gcMonitor

`static`

```java
GCMonitor gcMonitor = new ()
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`OnStaticsCleanup`](#onstaticscleanup) `static` `inline` | Invoked by the runtime before a script domain unloads. Re-creates the manager so no script instances, Type buckets or method override caches keep the unloading domain alive, while native update callbacks keep working against a fresh, empty manager. |

---

<a id="onstaticscleanup"></a>

### OnStaticsCleanup

`static` `inline`

```java
static inline void OnStaticsCleanup()
```

Invoked by the runtime before a script domain unloads. Re-creates the manager so no script instances, Type buckets or method override caches keep the unloading domain alive, while native update callbacks keep working against a fresh, empty manager.

