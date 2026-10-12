using System;

namespace Unravel.Core
{
    /// <summary>
    /// A &lt;progress&gt; bar. <see cref="value"/> runs from 0 to <see cref="max"/>.
    /// </summary>
    public class UIProgress : UIElement
    {
        internal UIProgress(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// How far it is filled, from 0 to <see cref="max"/>.
        /// </summary>
        public new float value
        {
            get => internal_m2n_ui_element_wrapper_get_progress(GetHandle()).x;
            set => internal_m2n_ui_element_wrapper_set_progress(GetHandle(), value, max);
        }

        /// <summary>
        /// The value of a full bar (default 1).
        /// </summary>
        public float max
        {
            get => internal_m2n_ui_element_wrapper_get_progress(GetHandle()).y;
            set => internal_m2n_ui_element_wrapper_set_progress(GetHandle(), this.value, value);
        }
    }
}
