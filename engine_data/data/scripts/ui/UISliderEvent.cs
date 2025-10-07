using System;

namespace Unravel.Core
{
    /// <summary>
    /// Represents a slider UI event for handling slider value changes.
    /// This is separate from key events to handle composed slider value changes properly.
    /// </summary>
    public class UISliderEvent : UIEventBase
    {
        /// <summary>
        /// The value that was input.
        /// </summary>
        public float value = 0;

        /// <summary>
        /// Whether the Min value was held during the event.
        /// </summary>
        public float minValue = 0;

        /// <summary>
        /// Whether the Max value was held during the event.
        /// </summary>
        public float maxValue = 0;

        /// <summary>
        /// Whether the Step value was held during the event.
        /// </summary>
        public float step = 0;
    }
}
