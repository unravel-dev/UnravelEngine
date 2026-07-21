<a id="time-1"></a>

# Time

Provides global timing information for gameplay scripts.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`timeScale`](#timescale) `static` | Gets or sets the time scale for the application. |

---

<a id="timescale"></a>

### timeScale

`static`

```java
float timeScale
```

Gets or sets the time scale for the application.

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`time`](#time-2) `static` | Elapsed time in seconds since play began. |
| `float` | [`deltaTime`](#deltatime-1) `static` | Seconds since the previous update frame (scaled by [timeScale](#timescale)). |
| `float` | [`fixedDeltaTime`](#fixeddeltatime) `static` | Seconds for the current fixed update step. |
| `long` | [`frameCount`](#framecount) `static` | Number of update frames since play began. |

---

<a id="time-2"></a>

### time

`static`

```java
float time
```

Elapsed time in seconds since play began.

---

<a id="deltatime-1"></a>

### deltaTime

`static`

```java
float deltaTime
```

Seconds since the previous update frame (scaled by [timeScale](#timescale)).

---

<a id="fixeddeltatime"></a>

### fixedDeltaTime

`static`

```java
float fixedDeltaTime
```

Seconds for the current fixed update step.

---

<a id="framecount"></a>

### frameCount

`static`

```java
long frameCount
```

Number of update frames since play began.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_set_time_scale`](#internal_m2n_set_time_scale)  |  |

---

<a id="internal_m2n_set_time_scale"></a>

### internal_m2n_set_time_scale

```java
void internal_m2n_set_time_scale(float scale)
```

