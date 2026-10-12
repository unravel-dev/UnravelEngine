using System;
using System.Runtime.CompilerServices;

namespace Unravel.Core
{

    /// <summary>
    /// Size of the view the game is shown in: the window in the player, the Game panel's
    /// resolution in the editor. Same pixel space as <see cref="Input.mousePosition"/>.
    /// </summary>
    /// <remarks>
    /// Zero until the game has been shown once. In the editor it follows the Game panel one frame
    /// late, and keeps its last value while the panel is hidden. A single camera's size is
    /// <see cref="CameraComponent.viewportSize"/>.
    /// </remarks>
    public static class Screen
    {
        /// <summary>
        /// Width of the game view in pixels.
        /// </summary>
        public static int width
        {
            get => (int)internal_m2n_screen_get_size().x;
        }

        /// <summary>
        /// Height of the game view in pixels.
        /// </summary>
        public static int height
        {
            get => (int)internal_m2n_screen_get_size().y;
        }

        /// <summary>
        /// Width divided by height of the game view, or 0 while the size is unknown.
        /// </summary>
        public static float aspect
        {
            get
            {
                Vector2 size = internal_m2n_screen_get_size();
                return size.y > 0.0f ? size.x / size.y : 0.0f;
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 internal_m2n_screen_get_size();
    }

}
