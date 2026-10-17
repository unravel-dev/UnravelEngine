using System.Runtime.CompilerServices;

namespace Unravel.Core
{
    /// <summary>
    /// How the cursor is held while the game has input.
    /// </summary>
    public enum CursorLockMode
    {
        /// <summary>The cursor moves freely.</summary>
        None,
        /// <summary>The cursor stays at the center of the game view, hidden; mouse axes still report motion.</summary>
        Locked,
        /// <summary>The cursor cannot leave the game view.</summary>
        Confined,
    }

    /// <summary>
    /// The mouse cursor: lock it for mouse look, keep it inside the game view, or hide it.
    /// </summary>
    /// <remarks>
    /// The request holds only while the game has input: its window has focus (in the editor, while play
    /// runs and the Game view has focus). In the editor Escape frees the cursor until the Game view is
    /// clicked again, and stopping play gives it back; the request itself resets when play stops.
    /// </remarks>
    public static class Cursor
    {
        /// <summary>
        /// How the cursor is held. Locked also hides it.
        /// </summary>
        public static CursorLockMode lockState
        {
            get
            {
                return (CursorLockMode)internal_m2n_cursor_get_lock_state();
            }
            set
            {
                internal_m2n_cursor_set_lock_state((int)value);
            }
        }

        /// <summary>
        /// Whether the cursor is shown (a locked cursor is hidden either way).
        /// </summary>
        public static bool visible
        {
            get
            {
                return internal_m2n_cursor_get_visible();
            }
            set
            {
                internal_m2n_cursor_set_visible(value);
            }
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_cursor_get_lock_state();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cursor_set_lock_state(int mode);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool internal_m2n_cursor_get_visible();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void internal_m2n_cursor_set_visible(bool visible);
    }
}
