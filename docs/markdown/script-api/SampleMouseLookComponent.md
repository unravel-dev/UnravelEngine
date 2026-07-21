<a id="samplemouselookcomponent"></a>

# SampleMouseLookComponent

> **Extends:** [`Unravel.Core.ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent)

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Vector2` | [`mouseAbsolute`](#mouseabsolute)  |  |
| `Vector2` | [`mouseSensitivity`](#mousesensitivity)  |  |
| `float` | [`moveSpeed`](#movespeed)  |  |

---

<a id="mouseabsolute"></a>

### mouseAbsolute

```java
Vector2 mouseAbsolute
```

---

<a id="mousesensitivity"></a>

### mouseSensitivity

```java
Vector2 mouseSensitivity = new (10, 10)
```

---

<a id="movespeed"></a>

### moveSpeed

```java
float moveSpeed = 20.0f
```

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override void` | [`OnCreate`](#oncreate-2) `virtual` `inline` | Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate. |
| `override void` | [`OnStart`](#onstart-4) `virtual` `inline` | Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled. |
| `override void` | [`OnUpdate`](#onupdate-4) `virtual` `inline` | Called on every frame update. Override this method to implement frame-based logic. |

---

<a id="oncreate-2"></a>

### OnCreate

`virtual` `inline`

```java
virtual inline override void OnCreate()
```

Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate.

---

<a id="onstart-4"></a>

### OnStart

`virtual` `inline`

```java
virtual inline override void OnStart()
```

Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled.

---

<a id="onupdate-4"></a>

### OnUpdate

`virtual` `inline`

```java
virtual inline override void OnUpdate()
```

Called on every frame update. Override this method to implement frame-based logic.

