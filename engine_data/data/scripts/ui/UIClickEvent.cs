using System;

namespace Unravel.Core
{
    /// <summary>
    /// A "click" or "dblclick": pointer data plus the click count.
    /// </summary>
    public class UIClickEvent : UIPointerEvent
    {
        /// <summary>
        /// 1 for "click", 2 for "dblclick".
        /// </summary>
        public int clickCount = 1;

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            base.FillFrom(payload, text);
            clickCount = payload.clickCount;
        }
    }
}
