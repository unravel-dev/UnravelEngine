<a id="sampleorbitcomponent"></a>

# SampleOrbitComponent

> **Extends:** [`Unravel.Core.ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent)

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`minimumDistance`](#minimumdistance)  |  |
| `float` | [`orbitSpeed`](#orbitspeed)  |  |
| `float` | [`axisChangeSpeed`](#axischangespeed)  |  |
| `float` | [`changeInterval`](#changeinterval)  |  |
| `float` | [`positionSmoothTime`](#positionsmoothtime)  |  |
| `Entity` | [`targetEntity`](#targetentity)  |  |

---

<a id="minimumdistance"></a>

### minimumDistance

```java
float minimumDistance = 5f
```

---

<a id="orbitspeed"></a>

### orbitSpeed

```java
float orbitSpeed = 30f
```

---

<a id="axischangespeed"></a>

### axisChangeSpeed

```java
float axisChangeSpeed = 0.5f
```

---

<a id="changeinterval"></a>

### changeInterval

```java
float changeInterval = 2f
```

---

<a id="positionsmoothtime"></a>

### positionSmoothTime

```java
float positionSmoothTime = 0.5f
```

---

<a id="targetentity"></a>

### targetEntity

```java
Entity targetEntity
```

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override void` | [`OnCreate`](#oncreate-1) `virtual` `inline` | Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate. |
| `override void` | [`OnStart`](#onstart-1) `virtual` `inline` | Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled. |
| `override void` | [`OnDestroy`](#ondestroy-1) `virtual` `inline` | Called when the script is destroyed. Override this method to clean up resources or data. |
| `override void` | [`OnUpdate`](#onupdate-1) `virtual` `inline` | Called on every frame update. Override this method to implement frame-based logic. |

---

<a id="oncreate-1"></a>

### OnCreate

`virtual` `inline`

```java
virtual inline override void OnCreate()
```

Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate.

---

<a id="onstart-1"></a>

### OnStart

`virtual` `inline`

```java
virtual inline override void OnStart()
```

Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled.

---

<a id="ondestroy-1"></a>

### OnDestroy

`virtual` `inline`

```java
virtual inline override void OnDestroy()
```

Called when the script is destroyed. Override this method to clean up resources or data.

---

<a id="onupdate-1"></a>

### OnUpdate

`virtual` `inline`

```java
virtual inline override void OnUpdate()
```

Called on every frame update. Override this method to implement frame-based logic.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`currentAxis`](#currentaxis)  |  |
| `Vector3` | [`targetAxis`](#targetaxis)  |  |
| `float` | [`changeTimer`](#changetimer)  |  |
| `Vector3` | [`velocity`](#velocity-2)  |  |

---

<a id="currentaxis"></a>

### currentAxis

```java
Vector3 currentAxis
```

---

<a id="targetaxis"></a>

### targetAxis

```java
Vector3 targetAxis
```

---

<a id="changetimer"></a>

### changeTimer

```java
float changeTimer
```

---

<a id="velocity-2"></a>

### velocity

```java
Vector3 velocity = 
```

