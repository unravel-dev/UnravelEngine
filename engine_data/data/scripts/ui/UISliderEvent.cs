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
        /// The element's 'min' attribute.
        /// </summary>
        public float minValue = 0;

        /// <summary>
        /// The element's 'max' attribute.
        /// </summary>
        public float maxValue = 0;

        /// <summary>
        /// The element's 'step' attribute.
        /// </summary>
        public float step = 0;

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            value = payload.value;
            minValue = payload.minValue;
            maxValue = payload.maxValue;
            step = payload.step;
        }
    }
}
