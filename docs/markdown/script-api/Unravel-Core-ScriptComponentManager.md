<a id="scriptcomponentmanager"></a>

# ScriptComponentManager

Optimized manager for ScriptComponents with type-based priority.

* Per-type buckets: Components of same type never sorted against each other

* Slot reuse: Removed components leave gaps that are reused (no shifting)

* O(1) removal: Dictionary lookup instead of linear search

* Minimal allocations: Reuses collections across frames

* Deferred operations during invocation for thread safety

* Method override detection: Only adds components that actually override Update/FixedUpdate/LateUpdate

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`ScriptComponentManager`](#scriptcomponentmanager-1) `inline` |  |
| `void` | [`Add`](#add) `inline` | Add a [ScriptComponent](Unravel-Core-ScriptComponent.md#scriptcomponent). If we're currently invoking, we defer it into 'pendingOps'. Otherwise, we insert it directly into the appropriate type bucket. |
| `void` | [`Remove`](#remove-2) `inline` | Remove a [ScriptComponent](Unravel-Core-ScriptComponent.md#scriptcomponent). Uses O(1) dictionary lookup instead of linear search. If we are invoking, defer the operation and collapse with existing operations. |
| `void` | [`Clear`](#clear-1) `inline` | Clears everything. |
| `void` | [`InvokeUpdate`](#invokeupdate)  |  |
| `void` | [`InvokeFixedUpdate`](#invokefixedupdate)  |  |
| `void` | [`InvokeLateUpdate`](#invokelateupdate)  |  |
| `void` | [`SetTypePriority`](#settypepriority) `inline` | Sets or updates the priority for a given type. If the type already has components, the bucket priority is updated and buckets are re-sorted. |

---

<a id="scriptcomponentmanager-1"></a>

### ScriptComponentManager

`inline`

```java
inline ScriptComponentManager(Dictionary< Type, int > typePriorityMap = null)
```

---

<a id="add"></a>

### Add

`inline`

```java
inline void Add(ScriptComponent comp)
```

Add a [ScriptComponent](Unravel-Core-ScriptComponent.md#scriptcomponent). If we're currently invoking, we defer it into 'pendingOps'. Otherwise, we insert it directly into the appropriate type bucket.

---

<a id="remove-2"></a>

### Remove

`inline`

```java
inline void Remove(ScriptComponent comp)
```

Remove a [ScriptComponent](Unravel-Core-ScriptComponent.md#scriptcomponent). Uses O(1) dictionary lookup instead of linear search. If we are invoking, defer the operation and collapse with existing operations.

---

<a id="clear-1"></a>

### Clear

`inline`

```java
inline void Clear()
```

Clears everything.

---

<a id="invokeupdate"></a>

### InvokeUpdate

```java
void InvokeUpdate()
```

---

<a id="invokefixedupdate"></a>

### InvokeFixedUpdate

```java
void InvokeFixedUpdate()
```

---

<a id="invokelateupdate"></a>

### InvokeLateUpdate

```java
void InvokeLateUpdate()
```

---

<a id="settypepriority"></a>

### SetTypePriority

`inline`

```java
inline void SetTypePriority(Type t, int priority)
```

Sets or updates the priority for a given type. If the type already has components, the bucket priority is updated and buckets are re-sorted.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`isInvoking`](#isinvoking)  |  |
| `readonly Dictionary< Type, TypeBucket >` | [`bucketsByType`](#bucketsbytype)  |  |
| `readonly List< TypeBucket >` | [`sortedBuckets`](#sortedbuckets)  |  |
| `readonly Dictionary< ScriptComponent, ComponentLocation >` | [`locationByComponent`](#locationbycomponent)  |  |
| `readonly List< PendingOp >` | [`pendingOps`](#pendingops)  |  |
| `readonly Dictionary< ScriptComponent, int >` | [`pendingOpIndexByComponent`](#pendingopindexbycomponent)  |  |
| `readonly HashSet< ScriptComponent >` | [`removedDuringInvoke`](#removedduringinvoke)  |  |
| `readonly Dictionary< Type, int >` | [`typePriorities`](#typepriorities)  |  |
| `readonly Dictionary< Type, MethodOverrides >` | [`methodOverrideCache`](#methodoverridecache)  |  |

---

<a id="isinvoking"></a>

### isInvoking

```java
bool isInvoking = false
```

---

<a id="bucketsbytype"></a>

### bucketsByType

```java
readonly Dictionary< Type, TypeBucket > bucketsByType = new Dictionary<Type, TypeBucket>()
```

---

<a id="sortedbuckets"></a>

### sortedBuckets

```java
readonly List< TypeBucket > sortedBuckets = new List<TypeBucket>()
```

---

<a id="locationbycomponent"></a>

### locationByComponent

```java
readonly Dictionary< ScriptComponent, ComponentLocation > locationByComponent = new Dictionary<, ComponentLocation>(ReferenceEqualityComparer.Instance)
```

---

<a id="pendingops"></a>

### pendingOps

```java
readonly List< PendingOp > pendingOps = new List<PendingOp>()
```

---

<a id="pendingopindexbycomponent"></a>

### pendingOpIndexByComponent

```java
readonly Dictionary< ScriptComponent, int > pendingOpIndexByComponent = new Dictionary<, int>(ReferenceEqualityComparer.Instance)
```

---

<a id="removedduringinvoke"></a>

### removedDuringInvoke

```java
readonly HashSet< ScriptComponent > removedDuringInvoke = new HashSet<>(ReferenceEqualityComparer.Instance)
```

---

<a id="typepriorities"></a>

### typePriorities

```java
readonly Dictionary< Type, int > typePriorities = new Dictionary<Type, int>()
```

---

<a id="methodoverridecache"></a>

### methodOverrideCache

```java
readonly Dictionary< Type, MethodOverrides > methodOverrideCache = new Dictionary<Type, MethodOverrides>()
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`InvokeInternal`](#invokeinternal) `inline` |  |
| `void` | [`InsertComponent`](#insertcomponent) `inline` |  |
| `MethodOverrides` | [`GetMethodOverrides`](#getmethodoverrides) `inline` |  |
| `void` | [`RemoveComponent`](#removecomponent-2) `inline` |  |
| `void` | [`CompactBucket`](#compactbucket) `inline` |  |
| `void` | [`InsertBucketSorted`](#insertbucketsorted) `inline` |  |
| `void` | [`ResortBuckets`](#resortbuckets) `inline` |  |
| `int` | [`GetPriorityFor`](#getpriorityfor) `inline` |  |
| `void` | [`CollapseOperations`](#collapseoperations) `inline` | Collapses operations for a component to avoid redundant add/remove chains. Uses O(1) dictionary lookup instead of O(n) linear search. |

---

<a id="invokeinternal"></a>

### InvokeInternal

`inline`

```java
inline void InvokeInternal(Action< ScriptComponent > action)
```

---

<a id="insertcomponent"></a>

### InsertComponent

`inline`

```java
inline void InsertComponent(ScriptComponent comp)
```

---

<a id="getmethodoverrides"></a>

### GetMethodOverrides

`inline`

```java
inline MethodOverrides GetMethodOverrides(Type t)
```

---

<a id="removecomponent-2"></a>

### RemoveComponent

`inline`

```java
inline void RemoveComponent(ScriptComponent comp)
```

---

<a id="compactbucket"></a>

### CompactBucket

`inline`

```java
inline void CompactBucket(TypeBucket bucket)
```

---

<a id="insertbucketsorted"></a>

### InsertBucketSorted

`inline`

```java
inline void InsertBucketSorted(TypeBucket bucket)
```

---

<a id="resortbuckets"></a>

### ResortBuckets

`inline`

```java
inline void ResortBuckets()
```

---

<a id="getpriorityfor"></a>

### GetPriorityFor

`inline`

```java
inline int GetPriorityFor(Type t)
```

---

<a id="collapseoperations"></a>

### CollapseOperations

`inline`

```java
inline void CollapseOperations(ScriptComponent comp, bool isAdd)
```

Collapses operations for a component to avoid redundant add/remove chains. Uses O(1) dictionary lookup instead of O(n) linear search.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `comp` | `[ScriptComponent](Unravel-Core-ScriptComponent.md#scriptcomponent)` | The component to process operations for. |
| `isAdd` | `bool` | True if this is an add operation, false if remove. |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Action< ScriptComponent >` | [`updateAction`](#updateaction) `static` |  |
| `readonly Action< ScriptComponent >` | [`fixedUpdateAction`](#fixedupdateaction) `static` |  |
| `readonly Action< ScriptComponent >` | [`lateUpdateAction`](#lateupdateaction) `static` |  |
| `readonly Type` | [`scriptComponentBaseType`](#scriptcomponentbasetype) `static` |  |

---

<a id="updateaction"></a>

### updateAction

`static`

```java
readonly Action< ScriptComponent > updateAction = c => c.OnUpdate()
```

---

<a id="fixedupdateaction"></a>

### fixedUpdateAction

`static`

```java
readonly Action< ScriptComponent > fixedUpdateAction = c => c.OnFixedUpdate()
```

---

<a id="lateupdateaction"></a>

### lateUpdateAction

`static`

```java
readonly Action< ScriptComponent > lateUpdateAction = c => c.OnLateUpdate()
```

---

<a id="scriptcomponentbasetype"></a>

### scriptComponentBaseType

`static`

```java
readonly Type scriptComponentBaseType = typeof()
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`IsMethodOverridden`](#ismethodoverridden) `static` `inline` |  |

---

<a id="ismethodoverridden"></a>

### IsMethodOverridden

`static` `inline`

```java
static inline bool IsMethodOverridden(Type derivedType, string methodName)
```

