using System;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// A &lt;select&gt; drop-down. The selected option's value is the base class
    /// <see cref="UIElement.value"/>. Adding and removing options never sends "change", even
    /// when it moves the selection.
    /// </summary>
    public class UISelect : UIElement
    {
        internal UISelect(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// The selected option's index, -1 for none. Setting it sends "change"; see
        /// <see cref="SetSelectedIndexWithoutNotify"/>.
        /// </summary>
        public int selectedIndex
        {
            get => internal_m2n_ui_element_wrapper_get_selected_index(GetHandle());
            set => internal_m2n_ui_element_wrapper_set_selected_index(GetHandle(), value, true);
        }

        /// <summary>
        /// Sets <see cref="selectedIndex"/> without sending "change" to scripts.
        /// </summary>
        public void SetSelectedIndexWithoutNotify(int index)
        {
            internal_m2n_ui_element_wrapper_set_selected_index(GetHandle(), index, false);
        }

        /// <summary>
        /// The &lt;option&gt; elements in order, as a new list.
        /// </summary>
        public IReadOnlyList<UIElement> options =>
            UIEventManager.GetOrCreateWrappers(internal_m2n_ui_element_wrapper_get_options(GetHandle()), Owner);

        /// <summary>
        /// Adds an option.
        /// </summary>
        /// <param name="rml">What the option shows (markup).</param>
        /// <param name="optionValue">The value it selects.</param>
        /// <param name="before">The index to insert at; -1 adds it last.</param>
        /// <returns>The option's index.</returns>
        public int AddOption(string rml, string optionValue, int before = -1)
        {
            return internal_m2n_ui_element_wrapper_add_option(GetHandle(), rml ?? "", optionValue ?? "", before);
        }

        /// <summary>
        /// Removes the option at @p index.
        /// </summary>
        public void RemoveOption(int index)
        {
            if (index >= 0)
            {
                internal_m2n_ui_element_wrapper_remove_option(GetHandle(), index);
            }
        }

        /// <summary>
        /// Removes every option.
        /// </summary>
        public void ClearOptions()
        {
            internal_m2n_ui_element_wrapper_remove_option(GetHandle(), -1);
        }
    }
}
