<a id="transformcomponentextensions"></a>

# TransformComponentExtensions

Extension methods for [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent).

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`FindClosestBone`](#findclosestbone) `static` `inline` | Finds the [Entity](Unravel-Core-Entity.md#entity-1) under this [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent) whose Transform has a Bone component and is closest in world‐space to `point`. Returns null if no such [Entity](Unravel-Core-Entity.md#entity-1) is found. |

---

<a id="findclosestbone"></a>

### FindClosestBone

`static` `inline`

```java
static inline Entity FindClosestBone(this TransformComponent root, Vector3 point)
```

Finds the [Entity](Unravel-Core-Entity.md#entity-1) under this [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent) whose Transform has a Bone component and is closest in world‐space to `point`. Returns null if no such [Entity](Unravel-Core-Entity.md#entity-1) is found.

