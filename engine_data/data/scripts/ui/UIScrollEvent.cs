using System;

namespace Unravel.Core
{
    /// <summary>
    /// A "scroll": the element's content scrolled. Read the new position from
    /// <see cref="UIElement.scrollTop"/> and <see cref="UIElement.scrollLeft"/> of <see cref="UIEventBase.target"/>.
    /// </summary>
    public class UIScrollEvent : UIEventBase
    {
    }
}
