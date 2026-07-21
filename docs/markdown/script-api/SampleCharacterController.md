<a id="samplecharactercontroller"></a>

# SampleCharacterController

> **Extends:** [`Unravel.Core.ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent)

Production-grade third-person character controller.

Uses the built-in CharacterControllerComponent for capsule-based sweep movement, gravity and collisions (no manual rigidbody integration), and layers on top:

* Acceleration / deceleration toward target speed (walk / run / sprint)

* Coyote time + jump buffering for forgiving jump feel

* Rotation toward movement direction with in-place turning

* Turn damping (speed scaling when changing direction sharply)

* Foot IK with hips dipping to match uneven ground

* Spine look-at IK + two-handed aim IK driven by the cursor

Shooting/ability logic lives in the separate `CharacterShooter` component so movement can be reused across non-combat characters (NPCs, cutscene actors).

The script auto-resolves common bone names (Mixamo rig by default). Drop this onto any humanoid rig with a CharacterControllerComponent and it will work out of the box; override the bone slots in the inspector for non-Mixamo rigs.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`CurrentSpeed`](#currentspeed)  |  |
| `float` | [`CurrentRotateSpeed`](#currentrotatespeed)  |  |
| `bool` | [`IsGrounded`](#isgrounded-1)  |  |
| `bool` | [`IsMoving`](#ismoving)  |  |
| `bool` | [`IsBackwards`](#isbackwards)  |  |
| `bool` | [`IsRunning`](#isrunning)  |  |
| `bool` | [`IsRotatingInPlace`](#isrotatinginplace)  |  |
| `bool` | [`IsAiming`](#isaiming)  |  |
| `float` | [`TurningInPlaceDirection`](#turninginplacedirection)  |  |

---

<a id="currentspeed"></a>

### CurrentSpeed

```java
float CurrentSpeed
```

---

<a id="currentrotatespeed"></a>

### CurrentRotateSpeed

```java
float CurrentRotateSpeed
```

---

<a id="isgrounded-1"></a>

### IsGrounded

```java
bool IsGrounded
```

---

<a id="ismoving"></a>

### IsMoving

```java
bool IsMoving
```

---

<a id="isbackwards"></a>

### IsBackwards

```java
bool IsBackwards
```

---

<a id="isrunning"></a>

### IsRunning

```java
bool IsRunning
```

---

<a id="isrotatinginplace"></a>

### IsRotatingInPlace

```java
bool IsRotatingInPlace
```

---

<a id="isaiming"></a>

### IsAiming

```java
bool IsAiming
```

---

<a id="turninginplacedirection"></a>

### TurningInPlaceDirection

```java
float TurningInPlaceDirection
```

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`WalkSpeed`](#walkspeed)  |  |
| `float` | [`RunSpeed`](#runspeed)  |  |
| `float` | [`SprintSpeed`](#sprintspeed)  |  |
| `float` | [`Acceleration`](#acceleration)  |  |
| `float` | [`Deceleration`](#deceleration)  |  |
| `float` | [`AirControl`](#aircontrol)  |  |
| `bool` | [`PreserveAirMomentum`](#preserveairmomentum)  |  |
| `float` | [`RunSmoothing`](#runsmoothing)  |  |
| `float` | [`RotateSpeed`](#rotatespeed)  |  |
| `float` | [`RunRotateSpeed`](#runrotatespeed)  |  |
| `float` | [`InPlaceTurnSpeed`](#inplaceturnspeed)  |  |
| `float` | [`InPlaceTurnThreshold`](#inplaceturnthreshold)  |  |
| `bool` | [`EnableTurnDamping`](#enableturndamping)  |  |
| `float` | [`TurnDampStartAngle`](#turndampstartangle)  |  |
| `float` | [`TurnDampEndAngle`](#turndampendangle)  |  |
| `float` | [`MinTurnSpeedScale`](#minturnspeedscale)  |  |
| `float` | [`JumpSpeed`](#jumpspeed)  |  |
| `float` | [`CoyoteTime`](#coyotetime)  |  |
| `float` | [`JumpBuffer`](#jumpbuffer)  |  |
| `bool` | [`ApplyFootIK`](#applyfootik)  |  |
| `int` | [`FootChainLength`](#footchainlength)  |  |
| `float` | [`FootHeightMin`](#footheightmin)  |  |
| `float` | [`FootHeightMax`](#footheightmax)  |  |
| `float` | [`FootRaycastOffset`](#footraycastoffset)  |  |
| `float` | [`FootRaycastDistance`](#footraycastdistance)  |  |
| `float` | [`FootTargetYOffset`](#foottargetyoffset)  |  |
| `float` | [`HipsSmoothing`](#hipssmoothing)  |  |
| `bool` | [`ApplyAimIK`](#applyaimik)  |  |
| `bool` | [`AimWithRightMouse`](#aimwithrightmouse)  |  |
| `float` | [`AimMaxDistance`](#aimmaxdistance)  |  |
| `float` | [`SpineIKSpeed`](#spineikspeed)  |  |
| `float` | [`HandIKSpeed`](#handikspeed)  |  |
| `float` | [`HandSwitchStartAngle`](#handswitchstartangle)  |  |
| `float` | [`HandSwitchEndAngle`](#handswitchendangle)  |  |
| `float` | [`MaxAimForwardAngle`](#maxaimforwardangle)  |  |
| `int` | [`HandChainLength`](#handchainlength)  |  |
| `Entity` | [`Hips`](#hips)  |  |
| `Entity` | [`Spine`](#spine)  |  |
| `Entity` | [`LeftFoot`](#leftfoot)  |  |
| `Entity` | [`RightFoot`](#rightfoot)  |  |
| `Entity` | [`LeftHand`](#lefthand)  |  |
| `Entity` | [`RightHand`](#righthand)  |  |

---

<a id="walkspeed"></a>

### WalkSpeed

```java
float WalkSpeed = 2.0f
```

---

<a id="runspeed"></a>

### RunSpeed

```java
float RunSpeed = 5.0f
```

---

<a id="sprintspeed"></a>

### SprintSpeed

```java
float SprintSpeed = 7.5f
```

---

<a id="acceleration"></a>

### Acceleration

```java
float Acceleration = 16.0f
```

---

<a id="deceleration"></a>

### Deceleration

```java
float Deceleration = 20.0f
```

---

<a id="aircontrol"></a>

### AirControl

```java
float AirControl = 0.35f
```

---

<a id="preserveairmomentum"></a>

### PreserveAirMomentum

```java
bool PreserveAirMomentum = true
```

---

<a id="runsmoothing"></a>

### RunSmoothing

```java
float RunSmoothing = 2.5f
```

---

<a id="rotatespeed"></a>

### RotateSpeed

```java
float RotateSpeed = 8.0f
```

---

<a id="runrotatespeed"></a>

### RunRotateSpeed

```java
float RunRotateSpeed = 12.0f
```

---

<a id="inplaceturnspeed"></a>

### InPlaceTurnSpeed

```java
float InPlaceTurnSpeed = 120.0f
```

---

<a id="inplaceturnthreshold"></a>

### InPlaceTurnThreshold

```java
float InPlaceTurnThreshold = 0.2f
```

---

<a id="enableturndamping"></a>

### EnableTurnDamping

```java
bool EnableTurnDamping = true
```

---

<a id="turndampstartangle"></a>

### TurnDampStartAngle

```java
float TurnDampStartAngle = 30.0f
```

---

<a id="turndampendangle"></a>

### TurnDampEndAngle

```java
float TurnDampEndAngle = 150.0f
```

---

<a id="minturnspeedscale"></a>

### MinTurnSpeedScale

```java
float MinTurnSpeedScale = 0.4f
```

---

<a id="jumpspeed"></a>

### JumpSpeed

```java
float JumpSpeed = 2.0f
```

---

<a id="coyotetime"></a>

### CoyoteTime

```java
float CoyoteTime = 0.15f
```

---

<a id="jumpbuffer"></a>

### JumpBuffer

```java
float JumpBuffer = 0.10f
```

---

<a id="applyfootik"></a>

### ApplyFootIK

```java
bool ApplyFootIK = true
```

---

<a id="footchainlength"></a>

### FootChainLength

```java
int FootChainLength = 2
```

---

<a id="footheightmin"></a>

### FootHeightMin

```java
float FootHeightMin = 0.14f
```

---

<a id="footheightmax"></a>

### FootHeightMax

```java
float FootHeightMax = 0.22f
```

---

<a id="footraycastoffset"></a>

### FootRaycastOffset

```java
float FootRaycastOffset = 0.5f
```

---

<a id="footraycastdistance"></a>

### FootRaycastDistance

```java
float FootRaycastDistance = 1.0f
```

---

<a id="foottargetyoffset"></a>

### FootTargetYOffset

```java
float FootTargetYOffset = 0.1f
```

---

<a id="hipssmoothing"></a>

### HipsSmoothing

```java
float HipsSmoothing = 0.15f
```

---

<a id="applyaimik"></a>

### ApplyAimIK

```java
bool ApplyAimIK = true
```

---

<a id="aimwithrightmouse"></a>

### AimWithRightMouse

```java
bool AimWithRightMouse = true
```

---

<a id="aimmaxdistance"></a>

### AimMaxDistance

```java
float AimMaxDistance = 500.0f
```

---

<a id="spineikspeed"></a>

### SpineIKSpeed

```java
float SpineIKSpeed = 5.0f
```

---

<a id="handikspeed"></a>

### HandIKSpeed

```java
float HandIKSpeed = 5.0f
```

---

<a id="handswitchstartangle"></a>

### HandSwitchStartAngle

```java
float HandSwitchStartAngle = 20.0f
```

---

<a id="handswitchendangle"></a>

### HandSwitchEndAngle

```java
float HandSwitchEndAngle = 30.0f
```

---

<a id="maxaimforwardangle"></a>

### MaxAimForwardAngle

```java
float MaxAimForwardAngle = 110.0f
```

---

<a id="handchainlength"></a>

### HandChainLength

```java
int HandChainLength = 2
```

---

<a id="hips"></a>

### Hips

```java
Entity Hips
```

---

<a id="spine"></a>

### Spine

```java
Entity Spine
```

---

<a id="leftfoot"></a>

### LeftFoot

```java
Entity LeftFoot
```

---

<a id="rightfoot"></a>

### RightFoot

```java
Entity RightFoot
```

---

<a id="lefthand"></a>

### LeftHand

```java
Entity LeftHand
```

---

<a id="righthand"></a>

### RightHand

```java
Entity RightHand
```

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override void` | [`OnStart`](#onstart-3) `virtual` `inline` | Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled. |
| `override void` | [`OnUpdate`](#onupdate-3) `virtual` `inline` | Called on every frame update. Override this method to implement frame-based logic. |

---

<a id="onstart-3"></a>

### OnStart

`virtual` `inline`

```java
virtual inline override void OnStart()
```

Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled.

---

<a id="onupdate-3"></a>

### OnUpdate

`virtual` `inline`

```java
virtual inline override void OnUpdate()
```

Called on every frame update. Override this method to implement frame-based logic.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `CharacterControllerComponent` | [`cc`](#cc)  |  |
| `CameraComponent` | [`mainCamera`](#maincamera)  |  |
| `Vector3` | [`horizontalVelocity`](#horizontalvelocity)  |  |
| `float` | [`smoothedRunInput`](#smoothedruninput)  |  |
| `float` | [`timeSinceUngrounded`](#timesinceungrounded)  |  |
| `float` | [`timeSinceJumpPressed`](#timesincejumppressed)  |  |
| `float` | [`leftFootBlend`](#leftfootblend)  |  |
| `float` | [`rightFootBlend`](#rightfootblend)  |  |
| `float` | [`hipsOffset`](#hipsoffset)  |  |
| `float` | [`spineIKWeight`](#spineikweight)  |  |
| `float` | [`handIKWeight`](#handikweight)  |  |
| `float` | [`leftHandWeight`](#lefthandweight)  |  |
| `float` | [`rightHandWeight`](#righthandweight)  |  |

---

<a id="cc"></a>

### cc

```java
CharacterControllerComponent cc
```

---

<a id="maincamera"></a>

### mainCamera

```java
CameraComponent mainCamera
```

---

<a id="horizontalvelocity"></a>

### horizontalVelocity

```java
Vector3 horizontalVelocity = 
```

---

<a id="smoothedruninput"></a>

### smoothedRunInput

```java
float smoothedRunInput = 0.0f
```

---

<a id="timesinceungrounded"></a>

### timeSinceUngrounded

```java
float timeSinceUngrounded = 0.0f
```

---

<a id="timesincejumppressed"></a>

### timeSinceJumpPressed

```java
float timeSinceJumpPressed = float.MaxValue
```

---

<a id="leftfootblend"></a>

### leftFootBlend

```java
float leftFootBlend = 1.0f
```

---

<a id="rightfootblend"></a>

### rightFootBlend

```java
float rightFootBlend = 1.0f
```

---

<a id="hipsoffset"></a>

### hipsOffset

```java
float hipsOffset = 0.0f
```

---

<a id="spineikweight"></a>

### spineIKWeight

```java
float spineIKWeight = 0.0f
```

---

<a id="handikweight"></a>

### handIKWeight

```java
float handIKWeight = 0.0f
```

---

<a id="lefthandweight"></a>

### leftHandWeight

```java
float leftHandWeight = 0.0f
```

---

<a id="righthandweight"></a>

### rightHandWeight

```java
float rightHandWeight = 0.0f
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `Vector2` | [`ReadMoveInput`](#readmoveinput) `inline` |  |
| `void` | [`UpdateTimers`](#updatetimers) `inline` |  |
| `void` | [`UpdateMovement`](#updatemovement) `inline` |  |
| `void` | [`UpdateRotation`](#updaterotation) `inline` |  |
| `void` | [`TryJump`](#tryjump) `inline` |  |
| `void` | [`UpdateFootIK`](#updatefootik) `inline` |  |
| `bool` | [`ProcessFoot`](#processfoot) `inline` |  |
| `void` | [`UpdateAimIK`](#updateaimik) `inline` |  |
| `void` | [`ResolveBones`](#resolvebones) `inline` |  |
| `Entity` | [`FindBone`](#findbone) `inline` |  |
| `void` | [`ResolveCamera`](#resolvecamera) `inline` |  |

---

<a id="readmoveinput"></a>

### ReadMoveInput

`inline`

```java
inline Vector2 ReadMoveInput()
```

---

<a id="updatetimers"></a>

### UpdateTimers

`inline`

```java
inline void UpdateTimers(float dt)
```

---

<a id="updatemovement"></a>

### UpdateMovement

`inline`

```java
inline void UpdateMovement(Vector2 moveInput, float runInput, float dt)
```

---

<a id="updaterotation"></a>

### UpdateRotation

`inline`

```java
inline void UpdateRotation(Vector2 moveInput, float dt)
```

---

<a id="tryjump"></a>

### TryJump

`inline`

```java
inline void TryJump()
```

---

<a id="updatefootik"></a>

### UpdateFootIK

`inline`

```java
inline void UpdateFootIK(float dt)
```

---

<a id="processfoot"></a>

### ProcessFoot

`inline`

```java
inline bool ProcessFoot(Entity foot, ref float blend, float dt, out Vector3 target, out float verticalOffset)
```

---

<a id="updateaimik"></a>

### UpdateAimIK

`inline`

```java
inline void UpdateAimIK(float dt)
```

---

<a id="resolvebones"></a>

### ResolveBones

`inline`

```java
inline void ResolveBones()
```

---

<a id="findbone"></a>

### FindBone

`inline`

```java
inline Entity FindBone(params string[] names)
```

---

<a id="resolvecamera"></a>

### ResolveCamera

`inline`

```java
inline void ResolveCamera()
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`SteerAirVelocity`](#steerairvelocity) `static` `inline` | Blends the current horizontal velocity toward the input target while preserving the larger magnitude of the two. Allows steering in the air without letting the player brake mid-jump by releasing the stick. |

---

<a id="steerairvelocity"></a>

### SteerAirVelocity

`static` `inline`

```java
static inline Vector3 SteerAirVelocity(Vector3 current, Vector3 target, float maxDelta)
```

Blends the current horizontal velocity toward the input target while preserving the larger magnitude of the two. Allows steering in the air without letting the player brake mid-jump by releasing the stick.

