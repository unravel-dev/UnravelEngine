using System;

namespace Unravel.Core
{
    /// <summary>
    /// A text field: an &lt;input&gt; (text, password...) or a &lt;textarea&gt;. Its text is
    /// <see cref="UIElement.value"/>; "change" fires while the user types.
    /// </summary>
    public class UIInput : UIElement
    {
        internal UIInput(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// Selects all the text.
        /// </summary>
        public void Select()
        {
            internal_m2n_ui_element_wrapper_select_text(GetHandle(), -1, -1);
        }

        /// <summary>
        /// Selects the characters from @p start up to, not including, @p end.
        /// </summary>
        public void SetSelectionRange(int start, int end)
        {
            internal_m2n_ui_element_wrapper_select_text(GetHandle(), Math.Max(0, start), Math.Max(0, end));
        }
    }
}
