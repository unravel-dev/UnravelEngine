<a id="typebucket"></a>

# TypeBucket

Bucket containing all components of a single type. Maintains slots with gaps (nulls) for efficient removal and reuse.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Type` | [`type`](#type)  |  |
| `int` | [`priority`](#priority)  |  |
| `MethodOverrides` | [`overrides`](#overrides)  |  |
| `List< ScriptComponent >` | [`slots`](#slots)  |  |
| `List< int >` | [`freeSlots`](#freeslots)  |  |

---

<a id="type"></a>

### type

```java
Type type
```

---

<a id="priority"></a>

### priority

```java
int priority
```

---

<a id="overrides"></a>

### overrides

```java
MethodOverrides overrides
```

---

<a id="slots"></a>

### slots

```java
List< ScriptComponent > slots = new List<>()
```

---

<a id="freeslots"></a>

### freeSlots

```java
List< int > freeSlots = new List<int>()
```

