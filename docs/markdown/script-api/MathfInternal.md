<a id="mathfinternal"></a>

# MathfInternal

Internal helpers used by [Mathf](Mathf.md#mathf).

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `volatile float` | [`FloatMinNormal`](#floatminnormal) `static` |  |
| `volatile float` | [`FloatMinDenormal`](#floatmindenormal) `static` |  |
| `bool` | [`IsFlushToZeroEnabled`](#isflushtozeroenabled) `static` |  |

---

<a id="floatminnormal"></a>

### FloatMinNormal

`static`

```java
volatile float FloatMinNormal = 1.17549435E-38f
```

---

<a id="floatmindenormal"></a>

### FloatMinDenormal

`static`

```java
volatile float FloatMinDenormal = float.Epsilon
```

---

<a id="isflushtozeroenabled"></a>

### IsFlushToZeroEnabled

`static`

```java
bool IsFlushToZeroEnabled = FloatMinDenormal == 0f
```

