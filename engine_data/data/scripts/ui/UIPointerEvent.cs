using System;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a pointer-related UI event with pointer-specific properties.
    /// Generic enough to handle mouse, touch, pen, and other pointer devices.
    /// </summary>
    public class UIPointerEvent : UIEventBase
    {
        /// <summary>
        /// The X coordinate of the pointer relative to the element.
        /// </summary>
        public float x = 0.0f;

        /// <summary>
        /// The Y coordinate of the pointer relative to the element.
        /// </summary>
        public float y = 0.0f;

        /// <summary>
        /// Which button was pressed (0=primary, 1=middle, 2=secondary).
        /// Only available for button events, -1 for non-button events.
        /// </summary>
        public int button = -1;

        /// <summary>
        /// Whether the Ctrl key was held during the event.
        /// </summary>
        public bool ctrlKey = false;

        /// <summary>
        /// Whether the Shift key was held during the event.
        /// </summary>
        public bool shiftKey = false;

        /// <summary>
        /// Whether the Alt key was held during the event.
        /// </summary>
        public bool altKey = false;

        /// <summary>
        /// Whether the Meta key (Windows/Cmd) was held during the event.
        /// </summary>
        public bool metaKey = false;

        /// <summary>
        /// The horizontal scroll delta for wheel/scroll events.
        /// Only available for scroll events.
        /// </summary>
        public float deltaX = 0.0f;

        /// <summary>
        /// The vertical scroll delta for wheel/scroll events.
        /// Only available for scroll events.
        /// </summary>
        public float deltaY = 0.0f;
    }
}
