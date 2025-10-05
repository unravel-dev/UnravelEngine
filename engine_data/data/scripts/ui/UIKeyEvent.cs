using System;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a keyboard-related UI event with key-specific properties.
    /// Simplified to only contain key code and modifier keys.
    /// </summary>
    public class UIKeyEvent : UIEventBase
    {
        /// <summary>
        /// The key that was pressed or released.
        /// </summary>
        public KeyCode keyCode = KeyCode.Unknown;

        /// <summary>
        /// Convenience property for the key.
        /// </summary>
        public KeyCode Key => keyCode;

        /// <summary>
        /// Whether any modifier keys were held during the event.
        /// </summary>
        public bool HasModifiers => ctrlKey || shiftKey || altKey || metaKey;

        /// <summary>
        /// Whether the Ctrl key was held.
        /// </summary>
        public bool ctrlKey = false;

        /// <summary>
        /// Whether the Shift key was held.
        /// </summary>
        public bool shiftKey = false;

        /// <summary>
        /// Whether the Alt key was held.
        /// </summary>
        public bool altKey = false;

        /// <summary>
        /// Whether the Meta key (Windows/Cmd) was held.
        /// </summary>
        public bool metaKey = false;
    }
}
