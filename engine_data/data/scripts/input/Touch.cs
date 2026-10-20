using System.Runtime.InteropServices;

namespace Unravel.Core
{
    /// <summary>
    /// Where a finger is in its life on the touch screen, as one frame sees it.
    /// </summary>
    public enum TouchPhase : int
    {
        /// <summary>The finger touched the screen this frame.</summary>
        Began,
        /// <summary>The finger moved this frame.</summary>
        Moved,
        /// <summary>The finger touches the screen and did not move this frame.</summary>
        Stationary,
        /// <summary>The finger left the screen this frame.</summary>
        Ended,
        /// <summary>The system took the finger away, for a gesture of its own; not a release to act on.</summary>
        Canceled,
    }

    /// <summary>
    /// A finger on the touch screen. See <see cref="Input.touchCount"/> and <see cref="Input.GetTouch"/>.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct Touch
    {
        /// <summary>
        /// Stays the same while the finger touches the screen: the lowest number no other finger has.
        /// </summary>
        public int FingerId;

        /// <summary>
        /// Where the finger is in its life this frame.
        /// </summary>
        public TouchPhase Phase;

        /// <summary>
        /// The finger's position in screen coordinates, as <see cref="Input.mousePosition"/>.
        /// </summary>
        public Vector2 Position;

        /// <summary>
        /// How far the finger moved this frame, in screen coordinates.
        /// </summary>
        public Vector2 DeltaPosition;

        /// <summary>
        /// How hard the finger presses, from 0 to 1, as the device reports it.
        /// </summary>
        public float Pressure;
    }
}
