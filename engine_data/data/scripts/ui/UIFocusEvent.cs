using System;

namespace Unravel.Core
{
    /// <summary>
    /// A "focus" or "blur": an element gained or lost keyboard focus. These events do not bubble;
    /// subscribe on the element itself.
    /// </summary>
    public class UIFocusEvent : UIEventBase
    {
        /// <summary>
        /// True for "focus", false for "blur".
        /// </summary>
        public bool isFocused => eventType == "focus";
    }
}
