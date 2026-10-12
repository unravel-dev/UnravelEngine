using System;
using System.Collections.Generic;

namespace Unravel.Core
{
    /// <summary>
    /// A &lt;tabset&gt; of &lt;tab&gt; headers and &lt;panel&gt; pages.
    /// </summary>
    public class UITabSet : UIElement
    {
        private readonly Dictionary<Action<int>, UISubscription> tabChangedHandlers = new Dictionary<Action<int>, UISubscription>();

        internal UITabSet(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }

        /// <summary>
        /// The index of the shown tab.
        /// </summary>
        public int activeTab
        {
            get => (int)internal_m2n_ui_element_wrapper_get_tabs(GetHandle()).x;
            set => internal_m2n_ui_element_wrapper_set_active_tab(GetHandle(), value);
        }

        /// <summary>
        /// The number of tabs.
        /// </summary>
        public int tabCount => (int)internal_m2n_ui_element_wrapper_get_tabs(GetHandle()).y;

        /// <summary>
        /// Raised with the new <see cref="activeTab"/> when the shown tab changes.
        /// </summary>
        public event Action<int> tabChanged
        {
            add
            {
                if (value != null && !tabChangedHandlers.ContainsKey(value))
                {
                    Action<int> handler = value;
                    tabChangedHandlers[value] = On<UIEventBase>("tabchange", e => handler(activeTab));
                }
            }
            remove
            {
                if (value != null && tabChangedHandlers.TryGetValue(value, out UISubscription subscription))
                {
                    subscription.Dispose();
                    tabChangedHandlers.Remove(value);
                }
            }
        }

        /// <summary>
        /// Sets the header and the page of tab @p index (markup), adding the tab when it is new.
        /// </summary>
        public void SetTab(int index, string tabRml, string panelRml)
        {
            internal_m2n_ui_element_wrapper_set_tab(GetHandle(), index, tabRml ?? "", panelRml ?? "");
        }

        /// <summary>
        /// Removes tab @p index with its page.
        /// </summary>
        public void RemoveTab(int index)
        {
            internal_m2n_ui_element_wrapper_remove_tab(GetHandle(), index);
        }
    }
}
