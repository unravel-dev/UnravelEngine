<a id="core"></a>

# Core

## Classes

| Name | Description |
|------|-------------|
| [`AnimationClip`](Unravel-Core-AnimationClip.md#animationclip) | Animation clip asset handle. |
| [`AnimationComponent`](Unravel-Core-AnimationComponent.md#animationcomponent) | Provides functionality to manage and blend animation clips on an entity. |
| [`Application`](Unravel-Core-Application.md#application) | Provides application-level functionality and control. |
| [`Asset`](Unravel-Core-Asset.md#asset) | Represents a generic asset with a unique identifier (UID). |
| [`Assets`](Unravel-Core-Assets.md#assets) | Loads and resolves assets by key or unique identifier. |
| [`AudioClip`](Unravel-Core-AudioClip.md#audioclip) | Audio clip asset handle. |
| [`AudioListenerComponent`](Unravel-Core-AudioListenerComponent.md#audiolistenercomponent) | Marks an entity as the audio listener used for spatialized sound. |
| [`AudioSourceComponent`](Unravel-Core-AudioSourceComponent.md#audiosourcecomponent) | Represents an audio source component that can play, pause, and control audio playback in a scene. |
| [`AutoStaticsCleanupAttribute`](Unravel-Core-AutoStaticsCleanupAttribute.md#autostaticscleanupattribute) | Marks a class whose static state must be reset when a script domain is unloaded (CoreCLR backend). |
| [`BoneComponent`](Unravel-Core-BoneComponent.md#bonecomponent) | Represents a component identifying a bone. |
| [`CameraComponent`](Unravel-Core-CameraComponent.md#cameracomponent) | Represents a camera component that allows interaction with the camera, such as converting screen space positions to rays in 3D space. |
| [`CharacterControllerComponent`](Unravel-Core-CharacterControllerComponent.md#charactercontrollercomponent) | Provides character controller physics functionality for an entity. Uses a capsule shape internally with sweep-based movement. |
| [`Collision`](Unravel-Core-Collision.md#collision) | Represents a collision that occurs between two entities. |
| [`Component`](Unravel-Core-Component.md#component) | Represents a base class for all components in the entity-component system. |
| [`ExceptionHelper`](Unravel-Core-ExceptionHelper.md#exceptionhelper) | Helpers for throwing managed exceptions from native bridge code. |
| [`Font`](Unravel-Core-Font.md#font) | [Font](Unravel-Core-Font.md#font) asset handle. |
| [`GCMonitor`](Unravel-Core-GCMonitor.md#gcmonitor) | Tracks managed GC activity and optionally logs collection/memory deltas. |
| [`Gizmos`](Unravel-Core-Gizmos.md#gizmos) | Draws debug primitives in the scene for visualization during development. |
| [`HeaderAttribute`](Unravel-Core-HeaderAttribute.md#headerattribute) | Adds a header to a field, providing a brief description or hint. |
| [`HideAttribute`](Unravel-Core-HideAttribute.md#hideattribute) | Hides a field from the inspector, preventing it from being displayed or edited. |
| [`IdComponent`](Unravel-Core-IdComponent.md#idcomponent) | Holds the entity's unique identifier used by the runtime and serialization. |
| [`IK`](Unravel-Core-IK.md#ik) | Inverse-kinematics helpers for skeletal chains. |
| [`Input`](Unravel-Core-Input.md#input) | Provides static methods to handle user input actions such as button presses and axis values. |
| [`LightComponent`](Unravel-Core-LightComponent.md#lightcomponent) | Light source component attached to an entity. |
| [`Log`](Unravel-Core-Log.md#log-2) | Writes messages to the engine log with automatic call-site information. |
| [`Material`](Unravel-Core-Material.md#material) | [Material](Unravel-Core-Material.md#material) asset with editable shading properties. |
| [`MaxAttribute`](Unravel-Core-MaxAttribute.md#maxattribute) | Specifies the maximum allowable value for a numeric field. |
| [`Mesh`](Unravel-Core-Mesh.md#mesh) | [Mesh](Unravel-Core-Mesh.md#mesh) asset handle. |
| [`MinAttribute`](Unravel-Core-MinAttribute.md#minattribute) | Specifies the minimum allowable value for a numeric field. |
| [`ModelComponent`](Unravel-Core-ModelComponent.md#modelcomponent) | Represents a component that provides model rendering capabilities for an entity. |
| [`NativeObject`](Unravel-Core-NativeObject.md#nativeobject) | Base class for managed wrappers around native engine objects. Equality accounts for validity; invalid instances are never equal. |
| [`ParticleEmitterComponent`](Unravel-Core-ParticleEmitterComponent.md#particleemittercomponent) | Represents a component that provides particle emission capabilities for an entity. |
| [`Physics`](Unravel-Core-Physics.md#physics) | Provides static methods for performing physics-related operations, such as raycasting. |
| [`PhysicsComponent`](Unravel-Core-PhysicsComponent.md#physicscomponent) | Provides physics functionality for an entity. |
| [`PhysicsMaterial`](Unravel-Core-PhysicsMaterial.md#physicsmaterial) | [Physics](Unravel-Core-Physics.md#physics) material asset handle. |
| [`Prefab`](Unravel-Core-Prefab.md#prefab) | [Prefab](Unravel-Core-Prefab.md#prefab) asset that can be instantiated into a scene as an entity hierarchy. |
| [`Profiler`](Unravel-Core-Profiler.md#profiler) | Performance profiler for measuring and recording execution times. |
| [`RangeAttribute`](Unravel-Core-RangeAttribute.md#rangeattribute) | Specifies a range for a numeric field in a class or struct. |
| [`ReferenceEqualityComparer`](Unravel-Core-ReferenceEqualityComparer.md#referenceequalitycomparer) | Comparer that uses reference equality instead of overridden Equals/GetHashCode. This prevents issues when [NativeObject](Unravel-Core-NativeObject.md#nativeobject)'s IsValid() state changes after adding to collections. |
| [`ReflectionProbeComponent`](Unravel-Core-ReflectionProbeComponent.md#reflectionprobecomponent) | Captures and provides reflection probe data for an entity. |
| [`Scene`](Unravel-Core-Scene.md#scene) | Represents a scene in the application, providing methods to manage entities and load or destroy scenes. |
| [`ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent) | Represents a script component that provides lifecycle hooks and event handling for an entity. |
| [`ScriptComponentManager`](Unravel-Core-ScriptComponentManager.md#scriptcomponentmanager) | Optimized manager for ScriptComponents with type-based priority. |
| [`ScriptSourceFileAttribute`](Unravel-Core-ScriptSourceFileAttribute.md#scriptsourcefileattribute) | Records the source file path of a script class for editor and tooling use. |
| [`StepAttribute`](Unravel-Core-StepAttribute.md#stepattribute) | Specifies a step increment for a numeric field in a class or struct. |
| [`StyleSheet`](Unravel-Core-StyleSheet.md#stylesheet) | UI stylesheet asset handle. |
| [`SystemManager`](Unravel-Core-SystemManager.md#systemmanager) | Entry point for native-to-managed frame updates and script component dispatch. |
| [`Tests`](Unravel-Core-Tests.md#tests) |  |
| [`TextComponent`](Unravel-Core-TextComponent.md#textcomponent) | Mirrors ace::text_component, letting scripts manage text rendering via properties. |
| [`Texture`](Unravel-Core-Texture.md#texture-1) | [Texture](Unravel-Core-Texture.md#texture-1) asset handle. |
| [`Time`](Unravel-Core-Time.md#time-1) | Provides global timing information for gameplay scripts. |
| [`TooltipAttribute`](Unravel-Core-TooltipAttribute.md#tooltipattribute) | Adds a tooltip to a field, providing a brief description or hint. |
| [`TransformComponent`](Unravel-Core-TransformComponent.md#transformcomponent) | Represents a component that defines the position, rotation, scale, and other transformations of an entity in 3D space. |
| [`TransformComponentExtensions`](Unravel-Core-TransformComponentExtensions.md#transformcomponentextensions) | Extension methods for [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent). |
| [`UIChangeEvent`](Unravel-Core-UIChangeEvent.md#uichangeevent) | Represents a UI change event for handling string value changes. This is for text inputs, dropdowns, and other elements that provide string values. |
| [`UIDocument`](Unravel-Core-UIDocument.md#uidocument) | Represents a wrapper around a native RmlUi document with managed lifetime. The C++ side owns the lifetime and will invalidate this wrapper when the document is destroyed. |
| [`UIDocumentComponent`](Unravel-Core-UIDocumentComponent.md#uidocumentcomponent) | [Component](Unravel-Core-Component.md#component) that manages an RmlUi document for rendering HTML/CSS-based user interfaces. Each component instance holds its own document while sharing the global UI context. |
| [`UIElement`](Unravel-Core-UIElement.md#uielement) | Represents a wrapper around a native RmlUi element with managed lifetime. The C++ side owns the lifetime and will invalidate this wrapper when the element is destroyed. |
| [`UIEventBase`](Unravel-Core-UIEventBase.md#uieventbase) | Base type for UI events dispatched through [UIEventManager](Unravel-Core-UIEventManager.md#uieventmanager). |
| [`UIEventManager`](Unravel-Core-UIEventManager.md#uieventmanager) | Global UI event manager that handles all UI event dispatching. Similar to [ScriptComponentManager](Unravel-Core-ScriptComponentManager.md#scriptcomponentmanager) but for UI events. |
| [`UIKeyEvent`](Unravel-Core-UIKeyEvent.md#uikeyevent) | Represents a keyboard-related UI event with key-specific properties. Simplified to only contain key code and modifier keys. |
| [`UIPointerEvent`](Unravel-Core-UIPointerEvent.md#uipointerevent) | Represents a pointer-related UI event with pointer-specific properties. Generic enough to handle mouse, touch, pen, and other pointer devices. |
| [`UISliderEvent`](Unravel-Core-UISliderEvent.md#uisliderevent) | Represents a slider UI event for handling slider value changes. This is separate from key events to handle composed slider value changes properly. |
| [`UITextInputEvent`](Unravel-Core-UITextInputEvent.md#uitextinputevent) | Represents a text input UI event for handling text entry. This is separate from key events to handle composed text input properly. |
| [`VisualTree`](Unravel-Core-VisualTree.md#visualtree) | UI visual-tree (RML/layout) asset handle. |
| [`ContactPoint`](Unravel-Core-ContactPoint.md#contactpoint) | Represents a contact point where a collision occurs. |
| [`Entity`](Unravel-Core-Entity.md#entity-1) | Represents an entity within a scene. Provides methods to manage components and query entity properties. |
| [`FixedUpdateInfo`](Unravel-Core-FixedUpdateInfo.md#fixedupdateinfo) | Fixed-step timing values pushed from native into managed code. |
| [`LayerMask`](Unravel-Core-LayerMask.md#layermask) | Represents a layer mask that can be used to include or exclude layers. |
| [`MaterialProperties`](Unravel-Core-MaterialProperties.md#materialproperties) | Blittable material property block exchanged with native code. |
| [`ProfilerScope`](Unravel-Core-ProfilerScope.md#profilerscope) | Scoped profiler that measures execution time and automatically records it when disposed. Use with 'using' statement for automatic scope-based profiling. |
| [`Ray`](Unravel-Core-Ray.md#ray) | Represents a ray with an origin and a direction in 3D space. |
| [`RaycastHit`](Unravel-Core-RaycastHit.md#raycasthit) | Represents information about a single hit during a raycasting operation. |
| [`UpdateInfo`](Unravel-Core-UpdateInfo.md#updateinfo) | Per-frame timing values pushed from native into managed code. |

## Enumerations

| Name | Description |
|------|-------------|
| [`MouseButton`](#mousebutton)  | Identifies a mouse button for [Input](Unravel-Core-Input.md#input) queries. |
| [`KeyCode`](#keycode)  | Identifies a keyboard key for [Input](Unravel-Core-Input.md#input) queries. |
| [`EmitterShape`](#emittershape)  | Emitter shape enumeration that matches the C++ EmitterShape::Enum. |
| [`EmitterDirection`](#emitterdirection)  | Emitter direction enumeration that matches the C++ EmitterDirection::Enum. |
| [`ParticleSimBackend`](#particlesimbackend)  | Particle simulation/pack backend. Matches C++ ps_soa::particle_sim_backend. |
| [`Alignment`](#alignment)  | Flags for horizontal and vertical text alignment, matching ace::align. |
| [`TextStyleFlags`](#textstyleflags)  | Text style flags for decorations and effects, matching gfx::text_style_flags. |
| [`ForceMode`](#forcemode)  | Specifies how forces are applied to physics components. |
| [`EventPhase`](#eventphase)  | Phase of UI event dispatch in the capture/target/bubble model. |

---

<a id="mousebutton"></a>

### MouseButton

```java
enum MouseButton
```

Identifies a mouse button for [Input](Unravel-Core-Input.md#input) queries.

| Value | Description |
|-------|-------------|
| `Left` |  |
| `Right` |  |
| `Middle` |  |
| `Button4` |  |
| `Button5` |  |
| `Button6` |  |
| `Button7` |  |
| `Button8` |  |
| `Button9` |  |
| `Button10` |  |
| `Button11` |  |
| `Button12` |  |
| `Button13` |  |
| `Button14` |  |
| `Button15` |  |
| `Button16` |  |

---

<a id="keycode"></a>

### KeyCode

```java
enum KeyCode
```

Identifies a keyboard key for [Input](Unravel-Core-Input.md#input) queries.

| Value | Description |
|-------|-------------|
| `Unknown` |  |
| `A` |  |
| `B` |  |
| `C` |  |
| `D` |  |
| `E` |  |
| `F` |  |
| `G` |  |
| `H` |  |
| `I` |  |
| `J` |  |
| `K` |  |
| `L` |  |
| `M` |  |
| `N` |  |
| `O` |  |
| `P` |  |
| `Q` |  |
| `R` |  |
| `S` |  |
| `T` |  |
| `U` |  |
| `V` |  |
| `W` |  |
| `X` |  |
| `Y` |  |
| `Z` |  |
| `Digit1` |  |
| `Digit2` |  |
| `Digit3` |  |
| `Digit4` |  |
| `Digit5` |  |
| `Digit6` |  |
| `Digit7` |  |
| `Digit8` |  |
| `Digit9` |  |
| `Digit0` |  |
| `Enter` |  |
| `Escape` |  |
| `Backspace` |  |
| `Tab` |  |
| `Space` |  |
| `Minus` |  |
| `Equals` |  |
| `LeftBracket` |  |
| `RightBracket` |  |
| `Backslash` |  |
| `NonUsHash` |  |
| `Semicolon` |  |
| `Apostrophe` |  |
| `Grave` |  |
| `Comma` |  |
| `Period` |  |
| `Slash` |  |
| `Capslock` |  |
| `F1` |  |
| `F2` |  |
| `F3` |  |
| `F4` |  |
| `F5` |  |
| `F6` |  |
| `F7` |  |
| `F8` |  |
| `F9` |  |
| `F10` |  |
| `F11` |  |
| `F12` |  |
| `PrintScreen` |  |
| `ScrollLock` |  |
| `Pause` |  |
| `Insert` |  |
| `Home` |  |
| `PageUp` |  |
| `Del` |  |
| `End` |  |
| `PageDown` |  |
| `Right` |  |
| `Left` |  |
| `Down` |  |
| `Up` |  |
| `NumLockClear` |  |
| `KpDivide` |  |
| `KpMultiply` |  |
| `KpMinus` |  |
| `KpPlus` |  |
| `KpEnter` |  |
| `KpDigit1` |  |
| `KpDigit2` |  |
| `KpDigit3` |  |
| `KpDigit4` |  |
| `KpDigit5` |  |
| `KpDigit6` |  |
| `KpDigit7` |  |
| `KpDigit8` |  |
| `KpDigit9` |  |
| `KpDigit0` |  |
| `KpPeriod` |  |
| `NonUsBackslash` |  |
| `Application` |  |
| `Power` |  |
| `KpEquals` |  |
| `F13` |  |
| `F14` |  |
| `F15` |  |
| `F16` |  |
| `F17` |  |
| `F18` |  |
| `F19` |  |
| `F20` |  |
| `F21` |  |
| `F22` |  |
| `F23` |  |
| `F24` |  |
| `Execute` |  |
| `Help` |  |
| `Menu` |  |
| `Select` |  |
| `Stop` |  |
| `Again` |  |
| `Undo` |  |
| `Cut` |  |
| `Copy` |  |
| `Paste` |  |
| `Find` |  |
| `Mute` |  |
| `VolumeUp` |  |
| `VolumeDown` |  |
| `KpComma` |  |
| `KpEqualsAs400` |  |
| `International1` |  |
| `International2` |  |
| `International3` |  |
| `International4` |  |
| `International5` |  |
| `International6` |  |
| `International7` |  |
| `International8` |  |
| `International9` |  |
| `Lang1` |  |
| `Lang2` |  |
| `Lang3` |  |
| `Lang4` |  |
| `Lang5` |  |
| `Lang6` |  |
| `Lang7` |  |
| `Lang8` |  |
| `Lang9` |  |
| `AltErase` |  |
| `SysReq` |  |
| `Cancel` |  |
| `Clear` |  |
| `Prior` |  |
| `Return2` |  |
| `Separator` |  |
| `Out` |  |
| `Oper` |  |
| `ClearAgain` |  |
| `CrSel` |  |
| `ExSel` |  |
| `KpDigit00` |  |
| `KpDigit000` |  |
| `ThousandsSeparator` |  |
| `DecimalSeparator` |  |
| `CurrencyUnit` |  |
| `CurrencySubUnit` |  |
| `KpLeftParen` |  |
| `KpRightParen` |  |
| `KpLeftBrace` |  |
| `KpRightBrace` |  |
| `KpTab` |  |
| `KpBackspace` |  |
| `KpA` |  |
| `KpB` |  |
| `KpC` |  |
| `KpD` |  |
| `KpE` |  |
| `KpF` |  |
| `KpXor` |  |
| `KpPower` |  |
| `KpPercent` |  |
| `KpLess` |  |
| `KpGreater` |  |
| `KpAmpersand` |  |
| `KpDblAmpersand` |  |
| `KpVerticalBar` |  |
| `KpDblVerticalBar` |  |
| `KpColon` |  |
| `KpHash` |  |
| `KpSpace` |  |
| `KpAt` |  |
| `KpExclam` |  |
| `KpMemStore` |  |
| `KpMemRecall` |  |
| `KpMemClear` |  |
| `KpMemAdd` |  |
| `KpMemSubtract` |  |
| `KpMemMultiply` |  |
| `KpMemDivide` |  |
| `KpPlusMinus` |  |
| `KpClear` |  |
| `KpClearEntry` |  |
| `KpBinary` |  |
| `KpOctal` |  |
| `KpDecimal` |  |
| `KpHexadecimal` |  |
| `LCtrl` |  |
| `LShift` |  |
| `LAlt` |  |
| `LGui` |  |
| `RCtrl` |  |
| `RShift` |  |
| `RAlt` |  |
| `RGui` |  |
| `Mode` |  |
| `MediaPlay` |  |
| `MediaPause` |  |
| `MediaRecord` |  |
| `MediaFastForward` |  |
| `MediaRewind` |  |
| `MediaNext` |  |
| `MediaPrev` |  |
| `MediaStop` |  |
| `MediaEject` |  |
| `MediaPlayPause` |  |
| `MediaSelect` |  |
| `AcNew` |  |
| `AcOpen` |  |
| `AcClose` |  |
| `AcExit` |  |
| `AcSave` |  |
| `AcPrint` |  |
| `AcProperties` |  |
| `AcSearch` |  |
| `AcHome` |  |
| `AcBack` |  |
| `AcForward` |  |
| `AcStop` |  |
| `AcRefresh` |  |
| `AcBookmarks` |  |
| `Sleep` |  |
| `Count` |  |

---

<a id="emittershape"></a>

### EmitterShape

```java
enum EmitterShape
```

Emitter shape enumeration that matches the C++ EmitterShape::Enum.

| Value | Description |
|-------|-------------|
| `Sphere` |  |
| `Hemisphere` |  |
| `Circle` |  |
| `Box` |  |
| `Rect` |  |

---

<a id="emitterdirection"></a>

### EmitterDirection

```java
enum EmitterDirection
```

Emitter direction enumeration that matches the C++ EmitterDirection::Enum.

| Value | Description |
|-------|-------------|
| `Up` |  |
| `Outward` |  |
| `Inward` |  |

---

<a id="particlesimbackend"></a>

### ParticleSimBackend

```java
enum ParticleSimBackend
```

Particle simulation/pack backend. Matches C++ ps_soa::particle_sim_backend.

| Value | Description |
|-------|-------------|
| `CPU` |  |
| `GPU` |  |

---

<a id="alignment"></a>

### Alignment

```java
enum Alignment
```

Flags for horizontal and vertical text alignment, matching ace::align.

| Value | Description |
|-------|-------------|
| `Invalid` |  |
| `Left` |  |
| `Center` |  |
| `Right` |  |
| `HorizontalMask` |  |
| `Top` |  |
| `Middle` |  |
| `Bottom` |  |
| `VerticalMask` |  |
| `Capline` |  |
| `Midline` |  |
| `Baseline` |  |
| `TypographicMask` |  |
| `VerticalTextMask` |  |

---

<a id="textstyleflags"></a>

### TextStyleFlags

```java
enum TextStyleFlags
```

Text style flags for decorations and effects, matching gfx::text_style_flags.

| Value | Description |
|-------|-------------|
| `Normal` |  |
| `Overline` |  |
| `Underline` |  |
| `StrikeThrough` |  |
| `Background` |  |
| `Foreground` |  |

---

<a id="forcemode"></a>

### ForceMode

```java
enum ForceMode
```

Specifies how forces are applied to physics components.

| Value | Description |
|-------|-------------|
| `Force` | Add a continuous force to the rigidbody, using its mass. |
| `Acceleration` | Add a continuous acceleration to the rigidbody, ignoring its mass. |
| `Impulse` | Add an instant force impulse to the rigidbody, using its mass. |
| `VelocityChange` | Add an instant velocity change to the rigidbody, ignoring its mass. |

---

<a id="eventphase"></a>

### EventPhase

```java
enum EventPhase
```

Phase of UI event dispatch in the capture/target/bubble model.

| Value | Description |
|-------|-------------|
| `None` |  |
| `Capture` |  |
| `Target` |  |
| `Bubble` |  |

## Functions

| Return | Name | Description |
|--------|------|-------------|
| `delegate void` | [`UIEventCallback`](#uieventcallback)  | Callback invoked when a UI event is dispatched to a subscribed element. |

---

<a id="uieventcallback"></a>

### UIEventCallback

```java
delegate void UIEventCallback(UIEventBase ev)
```

Callback invoked when a UI event is dispatched to a subscribed element.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `ev` | `[UIEventBase](Unravel-Core-UIEventBase.md#uieventbase)` | The event being dispatched. |

