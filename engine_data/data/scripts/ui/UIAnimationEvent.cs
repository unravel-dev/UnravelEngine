using System;

namespace Unravel.Core
{
    /// <summary>
    /// An "animationend" or "transitionend": an animation of one property finished. A keyframe
    /// animation of several properties sends one event per property.
    /// </summary>
    public class UIAnimationEvent : UIEventBase
    {
        /// <summary>
        /// The animated property, e.g. "opacity".
        /// </summary>
        public string property = "";

        /// <summary>
        /// True for "transitionend" (a 'transition' property), false for "animationend".
        /// </summary>
        public bool isTransition => eventType == "transitionend";

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            property = text ?? "";
        }
    }
}
