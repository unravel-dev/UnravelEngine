using System;

namespace Unravel.Core
{
    /// <summary>
    /// A drag event: "dragstart", "drag" and "dragend" on the dragged element (give it the RCSS
    /// 'drag: drag' or 'drag: clone'), "dragover", "dragmove", "dragout" and "dragdrop" on the
    /// elements it passes over or is dropped on.
    /// </summary>
    public class UIDragEvent : UIPointerEvent
    {
        private IntPtr dragElementHandle = IntPtr.Zero;

        /// <summary>
        /// The dragged element, on the events of the elements it passes over or is dropped on; null otherwise.
        /// </summary>
        public UIElement dragElement => UIEventManager.GetOrCreateWrapper(dragElementHandle, documentOwner);

        internal override void FillFrom(in UIEventPayload payload, string text)
        {
            base.FillFrom(payload, text);
            dragElementHandle = new IntPtr(payload.relatedHandle);
        }
    }
}
