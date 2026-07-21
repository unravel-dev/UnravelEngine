<a id="uieventmanager"></a>

# UIEventManager

Global UI event manager that handles all UI event dispatching. Similar to [ScriptComponentManager](Unravel-Core-ScriptComponentManager.md#scriptcomponentmanager) but for UI events.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`DispatchEvent`](#dispatchevent)  | Dispatch a UI event. This is called from C++ when an event occurs. |

---

<a id="dispatchevent"></a>

### DispatchEvent

```java
void DispatchEvent(UIEventBase ev)
```

Dispatch a UI event. This is called from C++ when an event occurs.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Subscribe`](#subscribe) `static` `inline` | Subscribe to a UI event. This is called by UIElement.AddEventListener. |
| `bool` | [`Unsubscribe`](#unsubscribe) `static` `inline` | Unsubscribe from a UI event. |
| `bool` | [`UnsubscribeAll`](#unsubscribeall-1) `static` `inline` | Unsubscribe all callbacks from a specific [UIElement](Unravel-Core-UIElement.md#uielement). This removes the element from both base and typed subscription dictionaries. |
| `int` | [`GetSubscriptionCount`](#getsubscriptioncount) `static` `inline` | Get the number of active subscriptions (for debugging/monitoring). |
| `void` | [`Subscribe< T >`](#subscribet) `static` `inline` | Subscribe to a typed UI event with compile-time type safety and zero runtime casting. |
| `bool` | [`Unsubscribe< T >`](#unsubscribet) `static` `inline` | Unsubscribe from a typed UI event. |
| `string` | [`GetSubscriptionInfo`](#getsubscriptioninfo) `static` `inline` | Get subscription info for debugging. |

---

<a id="subscribe"></a>

### Subscribe

`static` `inline`

```java
static inline void Subscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
```

Subscribe to a UI event. This is called by UIElement.AddEventListener.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `elementWrapper` | `[UIElement](Unravel-Core-UIElement.md#uielement)` | The [UIElement](Unravel-Core-UIElement.md#uielement) instance |
| `eventType` | `string` | The type of event (e.g., "click") |
| `callback` | `[UIEventCallback](Unravel-Core.md#uieventcallback)` | The callback to invoke |

---

<a id="unsubscribe"></a>

### Unsubscribe

`static` `inline`

```java
static inline bool Unsubscribe(UIElement elementWrapper, string eventType, UIEventCallback callback)
```

Unsubscribe from a UI event.

#### Returns
True if the callback was found and removed

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `elementWrapper` | `[UIElement](Unravel-Core-UIElement.md#uielement)` | The [UIElement](Unravel-Core-UIElement.md#uielement) instance |
| `eventType` | `string` | The type of event |
| `callback` | `[UIEventCallback](Unravel-Core.md#uieventcallback)` | The callback to remove |

---

<a id="unsubscribeall-1"></a>

### UnsubscribeAll

`static` `inline`

```java
static inline bool UnsubscribeAll(UIElement elementWrapper)
```

Unsubscribe all callbacks from a specific [UIElement](Unravel-Core-UIElement.md#uielement). This removes the element from both base and typed subscription dictionaries.

#### Returns
True if any subscriptions were removed (or will be removed if deferred)

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `elementWrapper` | `[UIElement](Unravel-Core-UIElement.md#uielement)` | The [UIElement](Unravel-Core-UIElement.md#uielement) to unsubscribe from |

---

<a id="getsubscriptioncount"></a>

### GetSubscriptionCount

`static` `inline`

```java
static inline int GetSubscriptionCount()
```

Get the number of active subscriptions (for debugging/monitoring).

---

<a id="subscribet"></a>

### Subscribe< T >

`static` `inline`

```java
static inline void Subscribe< T >(UIElement elementWrapper, string eventType, Action< T > callback)
```

Subscribe to a typed UI event with compile-time type safety and zero runtime casting.

---

<a id="unsubscribet"></a>

### Unsubscribe< T >

`static` `inline`

```java
static inline bool Unsubscribe< T >(UIElement elementWrapper, string eventType, Action< T > callback)
```

Unsubscribe from a typed UI event.

---

<a id="getsubscriptioninfo"></a>

### GetSubscriptionInfo

`static` `inline`

```java
static inline string GetSubscriptionInfo()
```

Get subscription info for debugging.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_ui_ensure_native_event_listener`](#internal_m2n_ui_ensure_native_event_listener)  |  |

---

<a id="internal_m2n_ui_ensure_native_event_listener"></a>

### internal_m2n_ui_ensure_native_event_listener

```java
void internal_m2n_ui_ensure_native_event_listener(IntPtr elementPtr, string eventType)
```

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Dictionary< UIElement, Dictionary< string, List< UIEventCallback > > >` | [`baseSubscriptions`](#basesubscriptions) `static` |  |
| `readonly Dictionary< UIElement, Dictionary< string, Dictionary< Type, List< Delegate > > > >` | [`typedSubscriptions`](#typedsubscriptions) `static` |  |
| `bool` | [`isDispatching`](#isdispatching) `static` |  |
| `readonly List< PendingOperation >` | [`pendingOperations`](#pendingoperations) `static` |  |

---

<a id="basesubscriptions"></a>

### baseSubscriptions

`static`

```java
readonly Dictionary< UIElement, Dictionary< string, List< UIEventCallback > > > baseSubscriptions = new Dictionary<, Dictionary<string, List<>>>()
```

---

<a id="typedsubscriptions"></a>

### typedSubscriptions

`static`

```java
readonly Dictionary< UIElement, Dictionary< string, Dictionary< Type, List< Delegate > > > > typedSubscriptions = new Dictionary<, Dictionary<string, Dictionary<Type, List<Delegate>>>>()
```

---

<a id="isdispatching"></a>

### isDispatching

`static`

```java
bool isDispatching = false
```

---

<a id="pendingoperations"></a>

### pendingOperations

`static`

```java
readonly List< PendingOperation > pendingOperations = new List<PendingOperation>()
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`OnStaticsCleanup`](#onstaticscleanup-1) `static` `inline` | Invoked by the runtime before a script domain unloads. Subscribed callbacks usually target script instances, which would otherwise keep the unloading domain alive. |
| `UIElement` | [`FindTargetWrapper`](#findtargetwrapper) `static` `inline` |  |
| `void` | [`DispatchTypedCallbacks`](#dispatchtypedcallbacks) `static` `inline` |  |
| `void` | [`DispatchBaseCallbacks`](#dispatchbasecallbacks) `static` `inline` |  |
| `void` | [`ProcessPendingOperations`](#processpendingoperations) `static` `inline` |  |
| `void` | [`EnsureNativeEventListener`](#ensurenativeeventlistener) `static` `inline` | Ensure that the C++ side has a native event listener for this event type. |
| `void` | [`SubscribeTypedInternal`](#subscribetypedinternal) `static` `inline` |  |
| `bool` | [`UnsubscribeTypedInternal`](#unsubscribetypedinternal) `static` `inline` |  |

---

<a id="onstaticscleanup-1"></a>

### OnStaticsCleanup

`static` `inline`

```java
static inline void OnStaticsCleanup()
```

Invoked by the runtime before a script domain unloads. Subscribed callbacks usually target script instances, which would otherwise keep the unloading domain alive.

---

<a id="findtargetwrapper"></a>

### FindTargetWrapper

`static` `inline`

```java
static inline UIElement FindTargetWrapper(UIEventBase ev)
```

---

<a id="dispatchtypedcallbacks"></a>

### DispatchTypedCallbacks

`static` `inline`

```java
static inline void DispatchTypedCallbacks(UIElement targetWrapper, UIEventBase ev)
```

---

<a id="dispatchbasecallbacks"></a>

### DispatchBaseCallbacks

`static` `inline`

```java
static inline void DispatchBaseCallbacks(UIElement targetWrapper, UIEventBase ev)
```

---

<a id="processpendingoperations"></a>

### ProcessPendingOperations

`static` `inline`

```java
static inline void ProcessPendingOperations()
```

---

<a id="ensurenativeeventlistener"></a>

### EnsureNativeEventListener

`static` `inline`

```java
static inline void EnsureNativeEventListener(UIElement elementWrapper, string eventType)
```

Ensure that the C++ side has a native event listener for this event type.

---

<a id="subscribetypedinternal"></a>

### SubscribeTypedInternal

`static` `inline`

```java
static inline void SubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
```

---

<a id="unsubscribetypedinternal"></a>

### UnsubscribeTypedInternal

`static` `inline`

```java
static inline bool UnsubscribeTypedInternal(UIElement elementWrapper, string eventType, Type callbackType, Delegate callback)
```

