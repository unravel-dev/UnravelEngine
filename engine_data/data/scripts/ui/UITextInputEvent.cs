using System;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a text input UI event for handling text entry.
    /// This is separate from key events to handle composed text input properly.
    /// </summary>
    public class UITextInputEvent : UIEventBase
    {
        /// <summary>
        /// The text that was input.
        /// </summary>
        public string text = "";

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
    }
}
