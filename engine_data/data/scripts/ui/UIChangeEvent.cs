using System;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a UI change event for handling string value changes.
    /// This is for text inputs, dropdowns, and other elements that provide string values.
    /// </summary>
    public class UIChangeEvent : UIEventBase
    {
        /// <summary>
        /// The string value that was changed.
        /// </summary>
        public string value;

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            value = text ?? "";
        }
    }
}
