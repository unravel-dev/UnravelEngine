using System;

namespace Unravel.Core
{
    /// <summary>
    /// A &lt;button&gt;, or an &lt;input&gt; of type submit or button. React with
    /// <see cref="UIElement.clicked"/>; disable with <see cref="UIElement.disabled"/>.
    /// </summary>
    public class UIButton : UIElement
    {
        internal UIButton(IntPtr elementHandle, Entity owner) : base(elementHandle, owner)
        {
        }
    }
}
