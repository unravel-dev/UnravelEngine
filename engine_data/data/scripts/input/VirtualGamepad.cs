using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// A gamepad that scripts drive, for on-screen controls such as a touch stick
    /// (<see cref="Unravel.Samples.SampleTouchJoystick"/>). The input settings read it as they read a
    /// connected gamepad, so <see cref="Input.GetAxis"/> and <see cref="Input.IsPressed(string)"/> answer to
    /// it for every action mapped to a gamepad axis or button: by default the left stick moves
    /// "Horizontal" and "Vertical", and <see cref="GamepadButton.South"/> is "Jump".
    /// </summary>
    /// <remarks>
    /// What a script sets reaches the game with the next frame, as a device's input does, so every script
    /// of a frame reads the same state whatever order the scripts run in. A press let go within one frame
    /// still reads pressed for a frame. Play ending lets go of everything.
    /// </remarks>
    public static class VirtualGamepad
    {
        /// <summary>
        /// Sets an axis, clamped from -1 to 1. Sticks read negative to the left and up, as a gamepad's.
        /// </summary>
        /// <param name="axis">The axis to set.</param>
        /// <param name="value">From -1 to 1.</param>
        public static void SetAxis(GamepadAxis axis, float value)
        {
            internal_m2n_virtual_gamepad_set_axis(axis, value);
        }

        /// <summary>
        /// Sets both axes of a stick. <paramref name="value"/> points the way the stick leans on screen: x
        /// to the right, y down.
        /// </summary>
        /// <param name="rightStick">The right stick instead of the left one.</param>
        /// <param name="value">Each component from -1 to 1.</param>
        public static void SetStick(bool rightStick, Vector2 value)
        {
            internal_m2n_virtual_gamepad_set_axis(rightStick ? GamepadAxis.RightX : GamepadAxis.LeftX, value.x);
            internal_m2n_virtual_gamepad_set_axis(rightStick ? GamepadAxis.RightY : GamepadAxis.LeftY, value.y);
        }

        /// <summary>
        /// Holds a button down or lets it go.
        /// </summary>
        /// <param name="button">The button to set.</param>
        /// <param name="down"><c>true</c> to hold it down, <c>false</c> to let it go.</param>
        public static void SetButton(GamepadButton button, bool down)
        {
            internal_m2n_virtual_gamepad_set_button(button, down);
        }

        /// <summary>
        /// Centers every axis and lets go of every button, at once.
        /// </summary>
        public static void ReleaseAll()
        {
            internal_m2n_virtual_gamepad_release_all();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_virtual_gamepad_set_axis(GamepadAxis axis, float value);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_virtual_gamepad_set_button(GamepadButton button, bool down);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_virtual_gamepad_release_all();
    }
}
