using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// Provides static methods to handle user input actions such as button presses and axis values.
    /// </summary>
    public static class Input
    {
        /// <summary>
        /// Gets the current mouse position in screen coordinates.
        /// </summary>
        public static Vector2 mousePosition
        {
            get
            {
                return internal_m2n_input_get_mouse_position();
            }
        }

        /// <summary>
        /// Gets the number of fingers on the touch screen this frame, the ones that left it this frame
        /// included. A touch also moves and presses the mouse, so mouse input keeps working on a touch
        /// screen.
        /// </summary>
        public static int touchCount
        {
            get
            {
                return internal_m2n_input_get_touch_count();
            }
        }

        /// <summary>
        /// Whether the game runs on a touch screen: on a phone or a tablet, or a finger has touched the
        /// screen since the application started.
        /// </summary>
        public static bool touchSupported
        {
            get
            {
                return internal_m2n_input_is_touch_supported();
            }
        }

        /// <summary>
        /// Whether a finger presses the mouse's left button where it touches (the first finger, as the
        /// platform makes mouse events from it). On by default, so a game made for the mouse takes taps
        /// as clicks; turn it off in a game with on-screen controls, so a tap beside them is no click.
        /// Either way a finger's motion never reads as mouse motion ("Mouse X", "Mouse Y"): read
        /// <see cref="Touch.DeltaPosition"/>. Play ending turns it back on.
        /// </summary>
        public static bool simulateMouseWithTouches
        {
            get
            {
                return internal_m2n_input_get_simulate_mouse_with_touches();
            }
            set
            {
                internal_m2n_input_set_simulate_mouse_with_touches(value);
            }
        }

        /// <summary>
        /// Gets a finger on the touch screen, in the order the fingers touched it. Fingers held by a
        /// <see cref="PointerZone"/> or pressed on a UI element are not listed.
        /// </summary>
        /// <param name="index">From 0 to <see cref="touchCount"/> - 1.</param>
        /// <returns>The finger at the index.</returns>
        public static Touch GetTouch(int index)
        {
            if (index < 0 || index >= touchCount)
            {
                throw new ArgumentOutOfRangeException(nameof(index), index, "There is no touch at this index.");
            }
            return internal_m2n_input_get_touch(index);
        }

        /// <summary>
        /// Gets the analog value of an input axis.
        /// </summary>
        /// <param name="action">The name of the action or axis to query.</param>
        /// <returns>The analog value of the specified axis.</returns>
        public static float GetAxis(string action)
        {
            return GetAnalogValue(action);
        }

        /// <summary>
        /// Gets the analog value associated with the specified input action.
        /// </summary>
        /// <param name="action">The name of the action to query.</param>
        /// <returns>The analog value associated with the action.</returns>
        public static float GetAnalogValue(string action)
        {
            return internal_m2n_input_get_analog_value(action);
        }

        /// <summary>
        /// Gets the digital (boolean) value associated with the specified input action.
        /// </summary>
        /// <param name="action">The name of the action to query.</param>
        /// <returns><c>true</c> if the action is active; otherwise, <c>false</c>.</returns>
        public static bool GetDigitalValue(string action)
        {
            return internal_m2n_input_get_digital_value(action);
        }

        /// <summary>
        /// Checks if the specified action's button was pressed during the current frame.
        /// </summary>
        /// <param name="action">The name of the action to query.</param>
        /// <returns><c>true</c> if the button was pressed during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsPressed(string action)
        {
            return internal_m2n_input_is_pressed(action);
        }

        /// <summary>
        /// Checks if the specified action's button was released during the current frame.
        /// </summary>
        /// <param name="action">The name of the action to query.</param>
        /// <returns><c>true</c> if the button was released during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsReleased(string action)
        {
            return internal_m2n_input_is_released(action);
        }

        /// <summary>
        /// Checks if the specified action's button is currently being held down.
        /// </summary>
        /// <param name="action">The name of the action to query.</param>
        /// <returns><c>true</c> if the button is currently being held down; otherwise, <c>false</c>.</returns>
        public static bool IsDown(string action)
        {
            return internal_m2n_input_is_down(action);
        }

        /// <summary>
        /// Checks if the specified key was pressed during the current frame.
        /// </summary>
        /// <param name="code">The key code to query.</param>
        /// <returns><c>true</c> if the key was pressed during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsPressed(KeyCode code)
        {
            return internal_m2n_input_is_key_pressed(code);
        }

        /// <summary>
        /// Checks if the specified key was released during the current frame.
        /// </summary>
        /// <param name="code">The key code to query.</param>
        /// <returns><c>true</c> if the key was released during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsReleased(KeyCode code)
        {
            return internal_m2n_input_is_key_released(code);
        }

        /// <summary>
        /// Checks if the specified key is currently being held down.
        /// </summary>
        /// <param name="code">The key code to query.</param>
        /// <returns><c>true</c> if the key is currently being held down; otherwise, <c>false</c>.</returns>
        public static bool IsDown(KeyCode code)
        {
            return internal_m2n_input_is_key_down(code);
        }

        /// <summary>
        /// Checks if the specified mouse button was pressed during the current frame.
        /// </summary>
        /// <param name="button">The mouse button to query.</param>
        /// <returns><c>true</c> if the button was pressed during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsPressed(MouseButton button)
        {
            return internal_m2n_input_is_mouse_button_pressed(button);
        }

        /// <summary>
        /// Checks if the specified mouse button was released during the current frame.
        /// </summary>
        /// <param name="button">The mouse button to query.</param>
        /// <returns><c>true</c> if the button was released during this frame; otherwise, <c>false</c>.</returns>
        public static bool IsReleased(MouseButton button)
        {
            return internal_m2n_input_is_mouse_button_released(button);
        }

        /// <summary>
        /// Checks if the specified mouse button is currently being held down.
        /// </summary>
        /// <param name="button">The mouse button to query.</param>
        /// <returns><c>true</c> if the button is currently being held down; otherwise, <c>false</c>.</returns>
        public static bool IsDown(MouseButton button)
        {
            return internal_m2n_input_is_mouse_button_down(button);
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern float internal_m2n_input_get_analog_value(string action);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_get_digital_value(string action);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_pressed(string action);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_released(string action);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_down(string action);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_key_pressed(KeyCode code);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_key_released(KeyCode code);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_key_down(KeyCode code);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_mouse_button_pressed(MouseButton button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_mouse_button_released(MouseButton button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_mouse_button_down(MouseButton button);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_input_get_mouse_position();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_input_get_touch_count();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Touch internal_m2n_input_get_touch(int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_is_touch_supported();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_input_get_simulate_mouse_with_touches();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_input_set_simulate_mouse_with_touches(bool simulate);
    }
}
