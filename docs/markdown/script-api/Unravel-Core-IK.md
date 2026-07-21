<a id="ik"></a>

# IK

Inverse-kinematics helpers for skeletal chains.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`SetIKPositionCCD`](#setikpositionccd) `static` `inline` | Solves a CCD [IK](#ik) chain so the end effector reaches *target* . *pole* is a world-space hint for which side of the (root -> end) line intermediate joints should bend toward - typically a point in front of the character at knee height for legs. Pass `[Vector3.zero](Vector3.md#zero-1)` to disable the constraint. |
| `void` | [`SetIKPositionFabrik`](#setikpositionfabrik) `static` `inline` | Solves a FABRIK [IK](#ik) chain so the end effector reaches *target* . *pole* is a world-space hint for which side of the (root -> end) line intermediate joints should bend toward. Without a pole, FABRIK may pick any geometrically valid bend direction - which on steep slopes causes knees to collapse inward. Pass `[Vector3.zero](Vector3.md#zero-1)` to disable the constraint. |
| `void` | [`SetIKPositionTwoBone`](#setikpositiontwobone) `static` `inline` | Analytical two-bone [IK](#ik) (hip-knee-foot or shoulder-elbow-hand). *pole* decides which side of the leg plane the knee (or elbow) bends to - typically a point in front of the character for legs, or off to the side for arms. |
| `void` | [`SetIKLookAtPosition`](#setiklookatposition) `static` `inline` | Orients *entity* to look toward *target* with the given blend weight. |

---

<a id="setikpositionccd"></a>

### SetIKPositionCCD

`static` `inline`

```java
static inline void SetIKPositionCCD(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations = 10, float threshold = 0.001f)
```

Solves a CCD [IK](#ik) chain so the end effector reaches *target* . *pole*  is a world-space hint for which side of the (root -> end) line intermediate joints should bend toward - typically a point in front of the character at knee height for legs. Pass `[Vector3.zero](Vector3.md#zero-1)` to disable the constraint.

---

<a id="setikpositionfabrik"></a>

### SetIKPositionFabrik

`static` `inline`

```java
static inline void SetIKPositionFabrik(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations = 10, float threshold = 0.001f)
```

Solves a FABRIK [IK](#ik) chain so the end effector reaches *target* . *pole*  is a world-space hint for which side of the (root -> end) line intermediate joints should bend toward. Without a pole, FABRIK may pick any geometrically valid bend direction - which on steep slopes causes knees to collapse inward. Pass `[Vector3.zero](Vector3.md#zero-1)` to disable the constraint.

---

<a id="setikpositiontwobone"></a>

### SetIKPositionTwoBone

`static` `inline`

```java
static inline void SetIKPositionTwoBone(Entity entity, Vector3 target, Vector3 pole, float weight = 1.0f, float soften = 1.0f)
```

Analytical two-bone [IK](#ik) (hip-knee-foot or shoulder-elbow-hand). *pole*  decides which side of the leg plane the knee (or elbow) bends to - typically a point in front of the character for legs, or off to the side for arms.

---

<a id="setiklookatposition"></a>

### SetIKLookAtPosition

`static` `inline`

```java
static inline void SetIKLookAtPosition(Entity entity, Vector3 target, float weight = 1.0f)
```

Orients *entity*  to look toward *target*  with the given blend weight.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_utils_set_ik_posiiton_ccd`](#internal_m2n_utils_set_ik_posiiton_ccd)  |  |
| `void` | [`internal_m2n_utils_set_ik_posiiton_fabrik`](#internal_m2n_utils_set_ik_posiiton_fabrik)  |  |
| `void` | [`internal_m2n_utils_set_ik_posiiton_two_bone`](#internal_m2n_utils_set_ik_posiiton_two_bone)  |  |
| `void` | [`internal_m2n_utils_set_ik_look_at_posiiton`](#internal_m2n_utils_set_ik_look_at_posiiton)  |  |

---

<a id="internal_m2n_utils_set_ik_posiiton_ccd"></a>

### internal_m2n_utils_set_ik_posiiton_ccd

```java
void internal_m2n_utils_set_ik_posiiton_ccd(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold)
```

---

<a id="internal_m2n_utils_set_ik_posiiton_fabrik"></a>

### internal_m2n_utils_set_ik_posiiton_fabrik

```java
void internal_m2n_utils_set_ik_posiiton_fabrik(Entity entity, Vector3 target, Vector3 pole, int numBonesInChain, int maxIterations, float threshold)
```

---

<a id="internal_m2n_utils_set_ik_posiiton_two_bone"></a>

### internal_m2n_utils_set_ik_posiiton_two_bone

```java
void internal_m2n_utils_set_ik_posiiton_two_bone(Entity entity, Vector3 target, Vector3 pole, float weight, float soften)
```

---

<a id="internal_m2n_utils_set_ik_look_at_posiiton"></a>

### internal_m2n_utils_set_ik_look_at_posiiton

```java
void internal_m2n_utils_set_ik_look_at_posiiton(Entity entity, Vector3 target, float weight)
```

