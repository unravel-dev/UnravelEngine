using System;

namespace Unravel.Core
{
    /// <summary>
    /// An &lt;input type="checkbox"&gt; or &lt;input type="radio"&gt;. Its state is the 'checked'
    /// attribute; the base class <see cref="UIElement.value"/> is the value it submits.
    /// </summary>
    public class UIToggle : UIElement
    {
        internal UIToggle(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// Whether it is checked. Setting it sends "change"; see <see cref="SetCheckedWithoutNotify"/>.
        /// </summary>
        public bool isChecked
        {
            get => internal_m2n_ui_element_wrapper_is_checked(GetHandle());
            set => internal_m2n_ui_element_wrapper_set_checked(GetHandle(), value, true);
        }

        /// <summary>
        /// Sets <see cref="isChecked"/> without sending "change" to scripts.
        /// </summary>
        public void SetCheckedWithoutNotify(bool isOn)
        {
            internal_m2n_ui_element_wrapper_set_checked(GetHandle(), isOn, false);
        }
    }
}
