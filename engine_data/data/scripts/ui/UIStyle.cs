using System;

namespace Unravel.Core
{
    /// <summary>
    /// The inline style of an element (<see cref="UIElement.style"/>). Values are RCSS text:
    /// <c>el.style["opacity"] = "0.5"</c>; setting null removes the inline value, so the stylesheet
    /// applies again. The numeric shortcuts write without formatting text, for per-frame changes.
    /// </summary>
    public sealed class UIStyle
    {
        // Units of the numeric shortcuts; the values mirror ui_style_unit in ui_bindings.cpp.
        private const int UnitNumber = 0;
        private const int UnitDp = 1;

        private readonly UIElement element;

        internal UIStyle(UIElement element)
        {
            this.element = element;
        }

        /// <summary>
        /// The inline value of a property, or null when it has none (also for shorthands such as
        /// "margin", which are stored as their parts). Setting an invalid declaration logs a warning.
        /// </summary>
        /// <param name="property">An RCSS property name, e.g. "background-color".</param>
        public string this[string property]
        {
            get
            {
                string value = element.GetInlineStyle(property);
                return string.IsNullOrEmpty(value) ? null : value;
            }
            set
            {
                if (value == null)
                {
                    element.RemoveInlineStyle(property);
                }
                else
                {
                    element.SetInlineStyle(property, value);
                }
            }
        }

        /// <summary>
        /// 'opacity', 0 to 1. Reads the value the element resolves to (inline, stylesheet or default).
        /// </summary>
        public float opacity
        {
            get => element.GetStyleNumber("opacity");
            set => element.SetStyleNumber("opacity", value, UnitNumber);
        }

        /// <summary>
        /// 'left' in dp. Reads NaN unless the element resolves to a plain length (not auto or a percentage).
        /// </summary>
        public float left
        {
            get => element.GetStyleNumber("left");
            set => element.SetStyleNumber("left", value, UnitDp);
        }

        /// <summary>
        /// 'top' in dp. Reads NaN unless the element resolves to a plain length (not auto or a percentage).
        /// </summary>
        public float top
        {
            get => element.GetStyleNumber("top");
            set => element.SetStyleNumber("top", value, UnitDp);
        }

        /// <summary>
        /// 'width' in dp. Reads NaN unless the element resolves to a plain length; for the laid-out
        /// size read <see cref="UIElement.rect"/>.
        /// </summary>
        public float width
        {
            get => element.GetStyleNumber("width");
            set => element.SetStyleNumber("width", value, UnitDp);
        }

        /// <summary>
        /// 'height' in dp. Reads NaN unless the element resolves to a plain length; for the laid-out
        /// size read <see cref="UIElement.rect"/>.
        /// </summary>
        public float height
        {
            get => element.GetStyleNumber("height");
            set => element.SetStyleNumber("height", value, UnitDp);
        }
    }
}
