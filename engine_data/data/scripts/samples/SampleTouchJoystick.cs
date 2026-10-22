using System.Globalization;
using Unravel.Core;

namespace Unravel.Samples
{
    /// <summary>
    /// On-screen controls for touch screens: a stick and up to four face buttons that drive the
    /// <see cref="VirtualGamepad"/>, so a game that reads gamepad-mapped actions plays by touch without
    /// touch code of its own. With the default input settings the left stick moves "Horizontal" and
    /// "Vertical" and the bottom face button (South) is "Jump"; map more actions to gamepad buttons in the
    /// project's input settings.
    /// </summary>
    /// <remarks>
    /// Add it to an entity of its own. It draws with engine:/data/ui/sample_touch_joystick.rhtml and adds a
    /// UI Document for it when the entity has none; give the entity a UI Document with a copy of that file
    /// to restyle the controls. The stick and the buttons take their fingers through
    /// <see cref="PointerZone"/>s, so dragging the stick neither turns a mouse-look camera nor clicks, and
    /// the mouse drives them as well: set Show to Always to try them in the editor.
    /// </remarks>
    public class SampleTouchJoystick : ScriptComponent
    {
        /// <summary>When the controls show.</summary>
        public enum Visibility
        {
            /// <summary>On a touch screen (<see cref="Input.touchSupported"/>).</summary>
            TouchScreen,
            /// <summary>Always, to try them with the mouse.</summary>
            Always,
        }

        [Header("Stick")]
        [Tooltip("Drive the right stick instead of the left one. The stick then sits on the right and the buttons on the left.")]
        public bool RightStick;

        [Tooltip("How far the knob travels from the stick's center, as a share of the screen's shorter side.")]
        public float Radius = 0.11f;

        [Tooltip("The share of the knob's travel a push needs before the stick moves.")]
        public float DeadZone = 0.15f;

        [Tooltip("A finger landing anywhere in the stick's area brings the stick under it. Off, only a press on the stick takes.")]
        public bool Floating = true;

        [Tooltip("Width of the stick's area from its side of the screen, as a share of the screen width.")]
        public float AreaWidth = 0.4f;

        [Tooltip("Height of the stick's area from the bottom of the screen, as a share of the screen height.")]
        public float AreaHeight = 0.65f;

        [Header("Buttons")]
        [Tooltip("Label of the bottom face button (South, which is Jump by default). An empty label hides the button.")]
        public string SouthLabel = "A";

        [Tooltip("Label of the right face button (East). An empty label hides the button.")]
        public string EastLabel = "B";

        [Tooltip("Label of the left face button (West). An empty label hides the button.")]
        public string WestLabel = "X";

        [Tooltip("Label of the top face button (North). An empty label hides the button.")]
        public string NorthLabel = "Y";

        [Tooltip("Diameter of a button, as a share of the screen's shorter side.")]
        public float ButtonSize = 0.13f;

        [Header("Display")]
        [Tooltip("When the controls show: on a touch screen, or always (to try them with the mouse).")]
        public Visibility Show = Visibility.TouchScreen;

        [Tooltip("While the controls show, a tap beside them is not a mouse click (Input.simulateMouseWithTouches is off).")]
        public bool TapsAreNotClicks = true;

        private const string LayoutPath = "engine:/data/ui/sample_touch_joystick.rhtml";
        /// <summary>Space between the safe area's edges and the controls, as a share of the shorter side.</summary>
        private const float EdgeMargin = 0.05f;
        /// <summary>Base and knob diameters, in knob travels.</summary>
        private const float BaseDiameter = 2.3f;
        private const float KnobDiameter = 0.95f;
        /// <summary>Distance of each face button from the cluster's center, in button diameters.</summary>
        private const float ButtonSpread = 0.85f;
        /// <summary>A button's zone is a little larger than the button, for a thumb that lands near its edge.</summary>
        private const float ButtonReach = 1.15f;
        /// <summary>Label height, in button diameters.</summary>
        private const float LabelSize = 0.38f;
        /// <summary>A longer label shrinks until its characters' heights add up to this many button diameters.</summary>
        private const float LabelFit = 1.1f;

        /// <summary>An absolutely placed element; it writes its RCSS only when its place changes.</summary>
        private sealed class Disc
        {
            public UIElement element;
            private Vector2 center = new Vector2(float.NaN, float.NaN);
            private float diameter = float.NaN;

            public void Place(Vector2 newCenter, float newDiameter)
            {
                if (element == null || (newCenter == center && newDiameter == diameter))
                {
                    return;
                }
                center = newCenter;
                diameter = newDiameter;
                element.style["left"] = Px(newCenter.x - newDiameter * 0.5f);
                element.style["top"] = Px(newCenter.y - newDiameter * 0.5f);
                element.style["width"] = Px(newDiameter);
                element.style["height"] = Px(newDiameter);
                element.style["border-radius"] = Px(newDiameter * 0.5f);
            }
        }

        /// <summary>A face button: where it sits relative to the cluster's center, its zone and its element.</summary>
        private sealed class FaceButton
        {
            public GamepadButton button;
            public Vector2 direction;
            public string label;
            public string elementId;
            public PointerZone zone;
            public Disc disc = new Disc();
            public bool down;
            public string shownLabel;
        }

        private UIDocumentComponent document;
        private bool isBound;
        private bool isShown;
        private bool wasSimulatingMouse = true;
        private PointerZone stickZone;
        private readonly Disc stickBase = new Disc();
        private readonly Disc stickKnob = new Disc();
        private FaceButton[] buttons = new FaceButton[0];
        private Vector2 screenSize;
        private Rect safeArea;
        private float travel;
        private Vector2 restCenter;
        private Vector2 stickCenter;
        private bool stickHeld;

        public override void OnStart()
        {
            document = owner.GetComponent<UIDocumentComponent>() ?? owner.AddComponent<UIDocumentComponent>();
            if (document.asset == null)
            {
                document.asset = Assets.GetAsset<VisualTree>(LayoutPath);
                if (document.asset == null)
                {
                    Log.Error($"SampleTouchJoystick could not load {LayoutPath}: the controls stay hidden.");
                }
            }
            stickZone = new PointerZone();
            buttons = new[]
            {
                MakeButton(GamepadButton.South, new Vector2(0.0f, 1.0f), "button-south"),
                MakeButton(GamepadButton.East, new Vector2(1.0f, 0.0f), "button-east"),
                MakeButton(GamepadButton.West, new Vector2(-1.0f, 0.0f), "button-west"),
                MakeButton(GamepadButton.North, new Vector2(0.0f, -1.0f), "button-north"),
            };
        }

        public override void OnUpdate()
        {
            if (!TryBind())
            {
                return;
            }
            bool show = Show == Visibility.Always || Input.touchSupported;
            if (show != isShown)
            {
                SetShown(show);
            }
            if (!isShown)
            {
                return;
            }
            UpdateLayout();
            UpdateStick();
            UpdateButtons();
        }

        public override void OnDisable()
        {
            if (isShown)
            {
                SetShown(false);
            }
        }

        public override void OnDestroy()
        {
            OnDisable();
            stickZone?.Dispose();
            foreach (var faceButton in buttons)
            {
                faceButton.zone.Dispose();
            }
        }

        private FaceButton MakeButton(GamepadButton button, Vector2 direction, string elementId)
        {
            return new FaceButton { button = button, direction = direction, elementId = elementId, zone = new PointerZone() };
        }

        /// <summary>Finds the elements once the document has loaded.</summary>
        private bool TryBind()
        {
            if (isBound)
            {
                return true;
            }
            if (document == null || !document.loaded || document.document == null)
            {
                return false;
            }
            UIDocument layout = document.document;
            stickBase.element = layout.GetElementById("stick-base");
            stickKnob.element = layout.GetElementById("stick-knob");
            foreach (var faceButton in buttons)
            {
                faceButton.disc.element = layout.GetElementById(faceButton.elementId);
            }
            isBound = true;
            // Hidden until the first frame decides: a desktop without Show Always never sees the controls.
            document.visible = false;
            return true;
        }

        private void SetShown(bool show)
        {
            isShown = show;
            if (document != null)
            {
                document.visible = show;
            }
            if (show)
            {
                wasSimulatingMouse = Input.simulateMouseWithTouches;
                if (TapsAreNotClicks)
                {
                    Input.simulateMouseWithTouches = false;
                }
                screenSize = Vector2.zero;
                return;
            }
            // Hidden controls hold nothing: the zones take no presses, the gamepad lets go.
            stickZone.rect = Rect.zero;
            VirtualGamepad.SetStick(RightStick, Vector2.zero);
            foreach (var faceButton in buttons)
            {
                faceButton.zone.rect = Rect.zero;
                faceButton.down = false;
                VirtualGamepad.SetButton(faceButton.button, false);
            }
            Input.simulateMouseWithTouches = wasSimulatingMouse;
        }

        /// <summary>Lays the controls out for the screen's size and the safe area.</summary>
        private void UpdateLayout()
        {
            var size = new Vector2(Screen.width, Screen.height);
            Rect safe = Screen.safeArea;
            if (size == screenSize && safe == safeArea)
            {
                return;
            }
            screenSize = size;
            safeArea = safe;
            float unit = Mathf.Min(size.x, size.y);
            float margin = EdgeMargin * unit;
            travel = Radius * unit;
            float baseRadius = travel * BaseDiameter * 0.5f;
            float bottom = safe.y + safe.height - margin;
            float stickX = RightStick ? safe.x + safe.width - margin - baseRadius : safe.x + margin + baseRadius;
            restCenter = new Vector2(stickX, bottom - baseRadius);
            stickCenter = restCenter;
            stickZone.rect = Floating ? GetStickArea(size, safe) : Square(restCenter, baseRadius * 2.0f);

            float buttonDiameter = ButtonSize * unit;
            float clusterReach = buttonDiameter * (ButtonSpread + 0.5f);
            float clusterX = RightStick ? safe.x + margin + clusterReach : safe.x + safe.width - margin - clusterReach;
            var clusterCenter = new Vector2(clusterX, bottom - clusterReach);
            foreach (var faceButton in buttons)
            {
                LayOutButton(faceButton, clusterCenter, buttonDiameter);
            }
        }

        /// <summary>The part of the screen where a finger brings the floating stick under it.</summary>
        private Rect GetStickArea(Vector2 size, Rect safe)
        {
            float width = AreaWidth * size.x;
            float top = size.y * (1.0f - AreaHeight);
            float left = RightStick ? safe.x + safe.width - width : safe.x;
            return new Rect(left, top, width, safe.y + safe.height - top);
        }

        private void LayOutButton(FaceButton faceButton, Vector2 clusterCenter, float diameter)
        {
            faceButton.label = LabelOf(faceButton.button);
            bool visible = !string.IsNullOrEmpty(faceButton.label);
            Vector2 center = clusterCenter + faceButton.direction * (diameter * ButtonSpread);
            faceButton.zone.rect = visible ? Square(center, diameter * ButtonReach) : Rect.zero;
            UIElement element = faceButton.disc.element;
            if (element == null)
            {
                return;
            }
            element.visible = visible;
            faceButton.disc.Place(center, diameter);
            int length = string.IsNullOrEmpty(faceButton.label) ? 1 : faceButton.label.Length;
            element.style["font-size"] = Px(diameter * Mathf.Min(LabelSize, LabelFit / length));
            // One line as tall as the space inside the border centers the label vertically.
            element.style["line-height"] = Px(element.contentRect.height);
            if (faceButton.shownLabel != faceButton.label)
            {
                faceButton.shownLabel = faceButton.label;
                element.text = faceButton.label ?? "";
            }
        }

        private string LabelOf(GamepadButton button)
        {
            switch (button)
            {
                case GamepadButton.South:
                    return SouthLabel;
                case GamepadButton.East:
                    return EastLabel;
                case GamepadButton.West:
                    return WestLabel;
                default:
                    return NorthLabel;
            }
        }

        private void UpdateStick()
        {
            Vector2 knob = Vector2.zero;
            Vector2 value = Vector2.zero;
            bool held = stickZone.TryGetPointer(out Touch pointer) && IsTouching(pointer.Phase);
            if (held)
            {
                if (!stickHeld)
                {
                    stickCenter = Floating ? KeepOnScreen(pointer.Position) : restCenter;
                }
                knob = Vector2.ClampMagnitude(pointer.Position - stickCenter, travel);
                value = ApplyDeadZone(knob / travel);
            }
            else
            {
                stickCenter = restCenter;
            }
            stickHeld = held;
            VirtualGamepad.SetStick(RightStick, value);
            stickBase.Place(stickCenter, travel * BaseDiameter);
            stickKnob.Place(stickCenter + knob, travel * KnobDiameter);
            stickBase.element?.SetClass("active", held);
            stickKnob.element?.SetClass("active", held);
        }

        private void UpdateButtons()
        {
            foreach (var faceButton in buttons)
            {
                bool down = faceButton.zone.TryGetPointer(out Touch pointer) && IsTouching(pointer.Phase);
                if (down == faceButton.down)
                {
                    continue;
                }
                faceButton.down = down;
                VirtualGamepad.SetButton(faceButton.button, down);
                faceButton.disc.element?.SetClass("pressed", down);
            }
        }

        /// <summary>A floating stick's center, moved in so that the whole base stays on screen.</summary>
        private Vector2 KeepOnScreen(Vector2 position)
        {
            float baseRadius = travel * BaseDiameter * 0.5f;
            float x = Mathf.Clamp(position.x, safeArea.x + baseRadius, safeArea.x + safeArea.width - baseRadius);
            float y = Mathf.Clamp(position.y, safeArea.y + baseRadius, safeArea.y + safeArea.height - baseRadius);
            return new Vector2(x, y);
        }

        /// <summary>Rescales a stick value so it starts from 0 at the dead zone's edge and reaches 1 at full travel.</summary>
        private Vector2 ApplyDeadZone(Vector2 value)
        {
            float magnitude = value.magnitude;
            if (magnitude <= DeadZone || magnitude <= 0.0f)
            {
                return Vector2.zero;
            }
            float scaled = Mathf.Clamp01((magnitude - DeadZone) / (1.0f - DeadZone));
            return value * (scaled / magnitude);
        }

        private static bool IsTouching(TouchPhase phase)
        {
            return phase != TouchPhase.Ended && phase != TouchPhase.Canceled;
        }

        private static Rect Square(Vector2 center, float side)
        {
            return new Rect(center.x - side * 0.5f, center.y - side * 0.5f, side, side);
        }

        private static string Px(float value)
        {
            return value.ToString("0.#", CultureInfo.InvariantCulture) + "px";
        }
    }
}
